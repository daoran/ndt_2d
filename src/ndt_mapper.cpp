/*
 * Copyright 2023 Michael Ferguson
 * All Rights Reserved
 */

#include <angles/angles.h>
#include <tf2/utils.h>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <cmath>
#include <chrono>
#include <ndt_2d/ndt_model.hpp>
#include <ndt_2d/ndt_mapper.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <geometry_msgs/msg/transform_stamped.hpp>
#include <tf2_geometry_msgs/tf2_geometry_msgs.hpp>

namespace ndt_2d
{

Mapper::Mapper(const rclcpp::NodeOptions & options)
: rclcpp::Node("ndt_2d_mapper", options),
  map_update_available_(false),
  logger_(rclcpp::get_logger("ndt_2d_mapper"))
{
  map_resolution_ = this->declare_parameter<double>("resolution", 0.05);
  ndt_resolution_ = this->declare_parameter<double>("ndt_resolution", 0.25);
  minimum_travel_distance_ = this->declare_parameter<double>("minimum_travel_distance", 0.1);
  minimum_travel_rotation_ = this->declare_parameter<double>("minimum_travel_rotation", 1.0);
  rolling_depth_ = this->declare_parameter<int>("rolling_depth", 10);
  odom_frame_ = this->declare_parameter<std::string>("odom_frame", "odom");

  tf2_buffer_ = std::make_shared<tf2_ros::Buffer>(this->get_clock());
  tf2_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf2_buffer_);
  tf2_broadcaster_ = std::make_shared<tf2_ros::TransformBroadcaster>(this);

  map_pub_ = this->create_publisher<nav_msgs::msg::OccupancyGrid>("map", 1);
  laser_sub_ = this->create_subscription<sensor_msgs::msg::LaserScan>(
    "scan", 1, std::bind(&Mapper::laserCallback, this, std::placeholders::_1));

  map_publish_timer_ = this->create_wall_timer(std::chrono::milliseconds(250),
    std::bind(&Mapper::mapPublishCallback, this));
}

Mapper::~Mapper()
{
}

void Mapper::laserCallback(const sensor_msgs::msg::LaserScan::ConstSharedPtr& msg)
{
  // Find pose of the robot in odometry frame
  geometry_msgs::msg::PoseStamped odom_pose_tf;
  odom_pose_tf.header = msg->header;
  odom_pose_tf.pose.orientation.w = 1.0;
  try
  {
    tf2_buffer_->transform(odom_pose_tf, odom_pose_tf, odom_frame_);
  }
  catch (const tf2::TransformException& ex)
  {
    RCLCPP_ERROR(logger_, "Could not transform odom pose.");
    return;
  }

  // Convert pose to ndt_2d style
  Pose2d odom_pose, corrected_pose;
  odom_pose.x = odom_pose_tf.pose.position.x;
  odom_pose.y = odom_pose_tf.pose.position.y;
  odom_pose.theta = tf2::getYaw(odom_pose_tf.pose.orientation);

  // Make sure we have traveled far enough
  if (!odom_poses_.empty())
  {
    // Calculate delta in odometry frame
    double dx = odom_pose.x - odom_poses_.back().x;
    double dy = odom_pose.y - odom_poses_.back().y;
    double dth = angles::shortest_angular_distance(odom_poses_.back().theta, odom_pose.theta);
    double dist = (dx * dx) + (dy * dy);
    if (dist < minimum_travel_distance_ * minimum_travel_distance_ &&
        std::fabs(dth) < minimum_travel_rotation_)
    {
      return;
    }
    // Odometry and corrected frame may not be aligned - determine heading between them
    double heading = angles::shortest_angular_distance(odom_poses_.back().theta,
                                                       corrected_poses_.back().theta);
    // Now apply odometry delta, corrected by heading, to get initial corrected pose
    corrected_pose.x = corrected_poses_.back().x + (dx * cos(heading)) - (dy * sin(heading));
    corrected_pose.y = corrected_poses_.back().y + (dx * sin(heading)) + (dy * cos(heading));
    corrected_pose.theta = angles::normalize_angle(corrected_poses_.back().theta + dth);

    std::cout << "Odom pose: " << odom_pose.x << " " << odom_pose.y
              << " " << odom_pose.theta << std::endl;
    std::cout << "Corrected: " << corrected_pose.x << " " << corrected_pose.y
              << " " << corrected_pose.theta << std::endl;
  }
  else
  {
    // Initialize correction - start robot at origin of map
    corrected_pose.x = 0.0;
    corrected_pose.y = 0.0;
    corrected_pose.theta = 0.0;
    std::cout << "Odom pose: " << odom_pose.x << " " << odom_pose.y << " "
              << odom_pose.theta << std::endl;
  }
  RCLCPP_INFO(logger_, "Adding scan to map");

  // Convert ROS msg into NDT scan
  ScanPtr scan(new Scan());
  scan->points.reserve(msg->ranges.size());
  for (size_t i = 0; i < msg->ranges.size(); ++i)
  {
    // Filter out NANs
    if (std::isnan(msg->ranges[i])) continue;
    // Project point and push into scan
    double angle = msg->angle_min + i * msg->angle_increment;
    Point point(cos(angle) * msg->ranges[i],
                sin(angle) * msg->ranges[i]);
    scan->points.push_back(point);
  }

  // Build an NDT of the last several scans
  double ndt_size = 10.0;
  NDT ndt(ndt_resolution_, ndt_size, ndt_size, -0.5 * ndt_size, -0.5 * ndt_size);
  size_t start = (scans_.size() > rolling_depth_) ? scans_.size() - rolling_depth_ : 0;
  for (size_t i = start; i < scans_.size(); ++i)
  {
    ndt.addScan(scans_[i], corrected_poses_[i]);
  }
  ndt.compute();

  // TODO(fergs): Search for best correlation of new scan against NDT
  // TODO(fergs): Update corrected_pose

  {
    // TODO(fergs): lock this when timer is converted to thread
    scans_.push_back(scan);
    odom_poses_.push_back(odom_pose);
    corrected_poses_.push_back(corrected_pose);
    map_update_available_ = true;
  }
}

void Mapper::publishTransform()
{
  // Latest corrected pose gives us map -> robot
  Pose2d & corrected = corrected_poses_.back();
  Eigen::Isometry3d map_to_robot(Eigen::Translation3d(corrected.x, corrected.y, 0.0) *
                                 Eigen::AngleAxisd(corrected.theta, Eigen::Vector3d::UnitZ()));

  // Latest odom pose gives us odom -> robot
  Pose2d & odom = odom_poses_.back();
  Eigen::Isometry3d odom_to_robot(Eigen::Translation3d(odom.x, odom.y, 0.0) *
                                  Eigen::AngleAxisd(odom.theta, Eigen::Vector3d::UnitZ()));

  // Compute map -> odom
  Eigen::Isometry3d map_to_odom(map_to_robot * odom_to_robot.inverse());

  // Publish TF between map and odom
  geometry_msgs::msg::TransformStamped transform;
  transform.header.stamp = this->now();
  transform.header.frame_id = "map";
  transform.child_frame_id = "odom";
  transform.transform.translation.x = map_to_odom.translation().x();
  transform.transform.translation.y = map_to_odom.translation().y();
  Eigen::Quaterniond q(map_to_odom.rotation());
  transform.transform.rotation.z = q.z();
  transform.transform.rotation.w = q.w();
  tf2_broadcaster_->sendTransform(transform);
}

// TODO(fergs): This should move to a separate thread
void Mapper::mapPublishCallback()
{
  if (!map_update_available_)
  {
    if (!odom_poses_.empty())
    {
      publishTransform();
    }
    // No map update to publish
    return;
  }

  size_t num_scans;
  {
    // TODO(fergs): lock this when timer is converted to thread
    map_update_available_ = false;
    num_scans = scans_.size();
  }

  // Size of the NDT grid
  // TODO(fergs) do this dynamically
  double sx = 10.0;
  double sy = 10.0;

  // Build an NDT from all scans
  NDT ndt(ndt_resolution_, sx, sy, -0.5 * sx, -0.5 * sy);
  for (size_t i = 0; i < num_scans; ++i)
  {
    ndt.addScan(scans_[i], corrected_poses_[i]);
  }
  ndt.compute();

  // Build map message by sampling from NDT
  nav_msgs::msg::OccupancyGrid grid;
  grid.header.frame_id = "map";
  grid.header.stamp = this->now();
  grid.info.resolution = map_resolution_;
  grid.info.width = sx / map_resolution_;
  grid.info.height = sy / map_resolution_;
  grid.info.origin.position.x = -0.5 * sx;
  grid.info.origin.position.y = -0.5 * sy;
  grid.data.assign(grid.info.width * grid.info.height, 0);
  for (size_t x = 0; x < grid.info.width; ++x)
  {
    for (size_t y = 0; y < grid.info.height; ++y)
    {
      double mx = x * map_resolution_ + grid.info.origin.position.x;
      double my = y * map_resolution_ + grid.info.origin.position.y;
      ndt_2d::Point point(mx, my);
      double l = ndt.likelihood(point);
      if (l > 0.0 && l < 50.0)
      {
        grid.data[x + (y * grid.info.width)] = 100;
      }
    }
  }
  map_pub_->publish(grid);
  publishTransform();
}

}  // namespace ndt_2d

#include "rclcpp_components/register_node_macro.hpp"
RCLCPP_COMPONENTS_REGISTER_NODE(ndt_2d::Mapper)
