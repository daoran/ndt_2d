/*
 * Copyright 2023 Michael Ferguson
 * All Rights Reserved
 */

#ifndef NDT_2D__NDT_SCAN_MATCHER_HPP_
#define NDT_2D__NDT_SCAN_MATCHER_HPP_

#include <memory>
#include <vector>
#include <rclcpp/rclcpp.hpp>
#include <ndt_2d/ndt_model.hpp>

namespace ndt_2d
{

class ScanMatcherNDT
{
public:
  /**
   * @brief Create an NDT scan matcher instance.
   * @param node Node instance to use for getting parameters.
   */
  explicit ScanMatcherNDT(rclcpp::Node * node);
  virtual ~ScanMatcherNDT() = default;

  /**
   * @brief Add scans to the internal NDT map.
   * @param begin Starting iterator of scans for NDT map building.
   * @param end Ending iterator of scans for NDT map building.
   */
  void addScans(const std::vector<ScanPtr>::const_iterator & begin,
                const std::vector<ScanPtr>::const_iterator & end);

  /**
   * @brief Match a scan against the internal NDT map.
   * @param scan Scan to match against internal NDT map.
   * @param pose The corrected pose that best matches scan to NDT map.
   * @param covariance Covariance matrix for the match.
   * @param scan_points_to_use Number of points to match from the scan.
   * @returns The likelihood score when scan is at corrected pose.
   */
  double matchScan(const ScanPtr & scan, Pose2d & pose,
                   Eigen::Matrix3d & covariance,
                   size_t scan_points_to_use) const;

  /**
   * @brief Score a scan against the internal NDT map.
   * @param scan Scan to score against internal NDT map.
   */
  double scoreScan(const ScanPtr & scan) const;

  /**
   * @brief Score a scan against the internal NDT map.
   * @param scan Scan to score against internal NDT map.
   * @param pose The pose of the scan within the internal NDT map.
   */
  double scoreScan(const ScanPtr & scan, const Pose2d & pose) const;

  /**
   * @brief Score a set of points against the internal NDT map.
   * @param points Points to score against internal NDT map.
   * @param pose The pose of the points within the internal NDT map.
   */
  double scorePoints(const std::vector<Point> & points, const Pose2d & pose) const;

  /**
   * @brief Reset the internal NDT map, removing all scans.
   */
  void reset();

  /**
   * @brief Set the maximum range for the laser scanner.
   */
  void setRangeMax(double range_max);

protected:
  // Resolution of the NDT map
  double resolution_;

  // Search parameters
  double angular_res_, angular_size_;
  double linear_res_, linear_size_;

  // Max range of laser scanner
  double range_max_;

  std::unique_ptr<NDT> ndt_;
};

}  // namespace ndt_2d

#endif  // NDT_2D__NDT_SCAN_MATCHER_HPP_
