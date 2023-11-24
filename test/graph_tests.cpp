/*
 * Copyright 2023 Michael Ferguson
 * All Rights Reserved
 */

#include <gtest/gtest.h>
#include <ndt_2d/graph.hpp>
#include <rcpputils/filesystem_helper.hpp>

TEST(GraphTests, read_write_test)
{
  const std::string BAG_NAME = "test_graph";
  rcpputils::fs::remove_all(BAG_NAME);

  {
    ndt_2d::Graph graph;

    ndt_2d::ScanPtr scan0 = std::make_shared<ndt_2d::Scan>();
    scan0->id = 0;
    scan0->points.resize(3);
    scan0->points[0].x = 2.0;
    scan0->points[0].y = 3.0;
    scan0->points[1].x = 3.0;
    scan0->points[1].y = 3.0;
    scan0->points[2].x = 4.0;
    scan0->points[2].y = 4.0;
    scan0->pose.x = 0.0;
    scan0->pose.y = 1.0;
    scan0->pose.theta = 0.0;
    graph.scans.push_back(scan0);

    ndt_2d::ScanPtr scan1 = std::make_shared<ndt_2d::Scan>();
    scan1->id = 1;
    scan1->points.resize(3);
    scan1->points[0].x = 1.0;
    scan1->points[0].y = 1.5;
    scan1->points[1].x = 2.0;
    scan1->points[1].y = 1.5;
    scan1->points[2].x = 3.0;
    scan1->points[2].y = 2.5;
    scan1->pose.x = 1.0;
    scan1->pose.y = 2.5;
    scan1->pose.theta = 0.05;
    graph.scans.push_back(scan1);

    ndt_2d::ConstraintPtr constraint = std::make_shared<ndt_2d::Constraint>();
    constraint->begin = 0;
    constraint->end = 1;
    constraint->transform(0) = 1.0;
    constraint->transform(1) = 1.5;
    constraint->transform(2) = 0.0;
    graph.odom_constraints.push_back(constraint);

    graph.save(BAG_NAME);
  }

  ndt_2d::Graph new_graph(BAG_NAME);
  EXPECT_EQ(2, new_graph.scans.size());
  EXPECT_EQ(3, new_graph.scans[0]->points.size());
  EXPECT_EQ(3, new_graph.scans[1]->points.size());
  EXPECT_EQ(1, new_graph.odom_constraints.size());
  EXPECT_EQ(0, new_graph.loop_constraints.size());
}

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}