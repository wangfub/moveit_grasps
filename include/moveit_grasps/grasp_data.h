/*
 * Software License Agreement (Modified BSD License)
 *
 *  Copyright (c) 2014, University of Colorado, Boulder, PAL Robotics, S.L.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Univ of CO, Boulder, PAL Robotics, S.L.
 *     nor the names of its contributors may be used to endorse or
 *     promote products derived from this software without specific
 *     prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/* Authors: Dave Coleman, Bence Magyar
   Description: Data class used by the grasp generator.
*/

#ifndef MOVEIT_GRASPS__GRASP_DATA_H_
#define MOVEIT_GRASPS__GRASP_DATA_H_

// Ros
#include <ros/node_handle.h>

// Msgs
#include <geometry_msgs/Pose.h>
#include <trajectory_msgs/JointTrajectory.h>

// MoveIt
#include <moveit/macros/class_forward.h>
#include <moveit/robot_state/robot_state.h>
#include <moveit/robot_model/link_model.h>

namespace moveit_grasps
{
MOVEIT_CLASS_FORWARD(GraspData);

// Map various arms to end effector grasp datas
typedef std::map<const robot_model::JointModelGroup*, moveit_grasps::GraspDataPtr> GraspDatas;

enum EndEffectorType
{
  FINGER = 1,
  SUCTION = 2
};

struct SuctionVoxel
{
  SuctionVoxel()
  : center_point_(Eigen::Vector3d(0, 0, 0))
  , top_left_(Eigen::Vector3d(0, 0, 0))
  , top_right_(Eigen::Vector3d(0, 0, 0))
  , bottom_left_(Eigen::Vector3d(0, 0, 0))
  , bottom_right_(Eigen::Vector3d(0, 0, 0)) {}

  SuctionVoxel(Eigen::Vector3d center_point)
  : SuctionVoxel()
  {
    center_point_ = center_point;
  }

  SuctionVoxel(Eigen::Vector3d center_point, double x_width, double y_width)
  : SuctionVoxel(center_point)
  {
    top_left = center_point + Eigen::Vector3d(-x_width/2.0, y_width/2.0, 0);
    top_right = center_point + Eigen::Vector3d(x_width/2.0, y_width/2.0, 0);
    bottom_left = center_point + Eigen::Vector3d(-x_width/2.0, -y_width/2.0, 0);
    bottom_right = center_point + Eigen::Vector3d(x_width/2.0, -y_width/2.0, 0);
  }

  Eigen::Vector3d center_point_;
  Eigen::Vector3d top_left_;
  Eigen::Vector3d top_right_;
  Eigen::Vector3d bottom_left_;
  Eigen::Vector3d bottom_right_;
};

class GraspData
{
public:
  /**
   * \brief Loads grasp data from a yaml file (load from roslaunch)
   * \param node handle - allows for namespacing
   * \param end effector name - which side of a two handed robot to load data for. should correspond to SRDF EE names
   */
  GraspData(const ros::NodeHandle& nh, const std::string& end_effector, moveit::core::RobotModelConstPtr robot_model);

  /**
   * \brief Helper function for constructor
   * \return true on success
   */
  bool loadGraspData(const ros::NodeHandle& nh, const std::string& end_effector);

  /**
   * \brief Alter a robot state so that the end effector corresponding to this grasp data is in pre-grasp state (OPEN)
   * \param joint state of robot
   * \return true on success
   */
  bool setRobotStatePreGrasp(robot_state::RobotStatePtr& robot_state);

  /**
   * \brief Alter a robot state so that the end effector corresponding to this grasp data is in grasp state (CLOSED)
   * \param joint state of robot
   * \return true on success
   */
  bool setRobotStateGrasp(robot_state::RobotStatePtr& robot_state);

  /**
   * \brief Alter a robot state so that the end effector corresponding to this grasp data is in a grasp posture
   * \param joint state of robot
   * \param posture - what state to set the end effector
   * \return true on success
   */
  bool setRobotState(robot_state::RobotStatePtr& robot_state, const trajectory_msgs::JointTrajectory& posture);

  /**
   * \brief Set the width between fingers as a percentage of object size and max finger width
   * \return true on success
   */
  bool setGraspWidth(const double& percent_open, const double& min_finger_width,
                     trajectory_msgs::JointTrajectory& grasp_posture);

  /**
   * \brief Convert width between fingers to joint positions
   * \return true on success
   */
  bool fingerWidthToGraspPosture(const double& distance_btw_fingers, trajectory_msgs::JointTrajectory& grasp_posture);

  /**
   * \brief Convert joint positions to full grasp posture
   * \return true on success
   */
  bool jointPositionsToGraspPosture(std::vector<double> joint_positions,
                                    trajectory_msgs::JointTrajectory& grasp_posture);

  /**
   * \brief Debug data to console
   */
  void print();

  /**
   * \brief
   */
  bool getSuctionRegionDims(std::size_t suction_voxel_index_x, std::size_t suction_voxel_index_y, SuctionVoxelDimensions& dimensions);

public:
  // A representation of the gripper type as an integer. See EndEffectorType for values
  EndEffectorType end_effector_type_;

  Eigen::Isometry3d grasp_pose_to_eef_pose_;  // Convert generic grasp pose to this end effector's frame of reference
  trajectory_msgs::JointTrajectory pre_grasp_posture_;  // when the end effector is in "open" position
  trajectory_msgs::JointTrajectory grasp_posture_;      // when the end effector is in "close" position
  std::string base_link_;                               // name of global frame with z pointing up

  const robot_model::JointModelGroup* ee_jmg_;   // this end effector
  const robot_model::JointModelGroup* arm_jmg_;  // the arm that attaches to this end effector
  const robot_model::RobotModelConstPtr robot_model_;

  // Duplicate end effector data copied from RobotModel
  // the last link in the kinematic chain before the end effector, e.g. "/gripper_roll_link" class
  const robot_model::LinkModel* parent_link_;

  int angle_resolution_;  // generate grasps at increments of: angle_resolution * pi / 180

  double grasp_resolution_;
  double grasp_depth_resolution_;  // generate grasps at this depth resolution along grasp_max_depth_
  double grasp_min_depth_;         // minimum amount fingers must overlap object
  double grasp_max_depth_;  // Maximum distance from tip of end effector inwords that an object can be for a grasp

  // grasp approach and retreat parameters
  double approach_distance_desired_;  // this is in addition to the grasp_max_depth
  double retreat_distance_desired_;   // this is in addition to the grasp_max_depth
  double lift_distance_desired_;
  double grasp_padding_on_approach_;

  /////////////////////////////////////
  // Finger gripper specific parameters
  /////////////////////////////////////
  // For calculating the ratio between the distance between fingers and the joint values
  double max_grasp_width_;
  double max_finger_width_;
  double min_finger_width_;
  double gripper_finger_width_;  // parameter used to ensure generated grasps will overlap object

  //////////////////////////////////////
  // Suction gripper specific parameters
  //////////////////////////////////////
  double active_suction_range_x_;
  double active_suction_range_y_;

  int suction_regions_x_;
  int suction_regions_y_;
  std::vector<SuctionVoxel> suction_voxels_;
};

}  // namespace

#endif
