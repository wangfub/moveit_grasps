/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2015, University of Colorado, Boulder
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
 *   * Neither the name of the Univ of CO, Boulder nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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
 *********************************************************************/

/* Author: Dave Coleman <dave@picknik.ai>, Andy McEvoy
   Desc:   Generates geometric grasps for cuboids and blocks, not using physics or contact wrenches
*/

#include <moveit_grasps/grasp_generator.h>

#include <rosparam_shortcuts/rosparam_shortcuts.h>

namespace
{
void debugFailedOpenGripper(double percent_open, double min_finger_open_on_approach, double object_width,
                            double grasp_padding_on_approach)
{
  ROS_ERROR_STREAM_NAMED("grasp_generator", "Unable to set grasp width to "
                                                << percent_open << " % open. Stats:"
                                                << "\n min_finger_open_on_approach: \t " << min_finger_open_on_approach
                                                << "\n object_width: \t " << object_width
                                                << "\n grasp_padding_on_approach_: \t " << grasp_padding_on_approach);
}

}  // namespace

namespace moveit_grasps

{
// Constructor
GraspGenerator::GraspGenerator(moveit_visual_tools::MoveItVisualToolsPtr visual_tools, bool verbose)
  : ideal_grasp_pose_(Eigen::Isometry3d::Identity())
  , visual_tools_(visual_tools)
  , verbose_(verbose)
  , nh_("~/moveit_grasps/generator")
  , grasp_score_weights_(GraspScoreWeights())
{
  // Load visulization settings
  const std::string parent_name = "grasps";  // for namespacing logging messages
  std::size_t error = 0;

  error += !rosparam_shortcuts::get(parent_name, nh_, "verbose", verbose_);
  error += !rosparam_shortcuts::get(parent_name, nh_, "show_prefiltered_grasps", show_prefiltered_grasps_);
  error += !rosparam_shortcuts::get(parent_name, nh_, "show_prefiltered_grasps_speed", show_prefiltered_grasps_speed_);
  error += !rosparam_shortcuts::get(parent_name, nh_, "debug_top_grasps", debug_top_grasps_);
  error += !rosparam_shortcuts::get(parent_name, nh_, "show_grasp_overhang", show_grasp_overhang_);

  // Load scoring weights
  rosparam_shortcuts::shutdownIfError(parent_name, error);
}

void GraspGenerator::setIdealGraspPoseRPY(const std::vector<double>& ideal_grasp_orientation_rpy)
{
  ROS_ASSERT_MSG(ideal_grasp_orientation_rpy.size() == 3, "setIdealGraspPoseRPY must be set with a vector of length 3");

  // copy the ideal_grasp_pose.translation() so that we only change the orientation.
  Eigen::Vector3d ideal_grasp_pose_translation(ideal_grasp_pose_.translation());

  // Set ideal grasp pose (currently only uses orientation of pose)
  ideal_grasp_pose_ = Eigen::Isometry3d::Identity();
  ideal_grasp_pose_ = ideal_grasp_pose_ * Eigen::AngleAxisd(ideal_grasp_orientation_rpy[0], Eigen::Vector3d::UnitX());
  ideal_grasp_pose_ = ideal_grasp_pose_ * Eigen::AngleAxisd(ideal_grasp_orientation_rpy[1], Eigen::Vector3d::UnitY());
  ideal_grasp_pose_ = ideal_grasp_pose_ * Eigen::AngleAxisd(ideal_grasp_orientation_rpy[2], Eigen::Vector3d::UnitZ());

  ideal_grasp_pose_.translation() = ideal_grasp_pose_translation;
}

bool GraspGenerator::generateCuboidAxisGrasps(const Eigen::Isometry3d& cuboid_pose, double depth, double width,
                                              double height, grasp_axis_t axis,
                                              const moveit_grasps::GraspDataPtr grasp_data,
                                              const GraspCandidateConfig& grasp_candidate_config,
                                              std::vector<GraspCandidatePtr>& grasp_candidates)
{
  double finger_depth = grasp_data->grasp_max_depth_ - grasp_data->grasp_min_depth_;
  double length_along_a, length_along_b, length_along_c;
  double delta_a, delta_b, delta_f;
  double alpha_x, alpha_y, alpha_z;
  Eigen::Vector3d object_size(depth, width, height);

  double object_width;
  std::vector<Eigen::Isometry3d> grasp_poses;

  Eigen::Isometry3d grasp_pose = cuboid_pose;
  Eigen::Vector3d a_dir, b_dir, c_dir;

  if (axis == X_AXIS)
  {
    length_along_a = width;
    length_along_b = height;
    length_along_c = depth;
    a_dir = grasp_pose.rotation() * Eigen::Vector3d::UnitY();
    b_dir = grasp_pose.rotation() * Eigen::Vector3d::UnitZ();
    c_dir = grasp_pose.rotation() * Eigen::Vector3d::UnitX();
    alpha_x = -M_PI / 2.0;
    alpha_y = 0;
    alpha_z = -M_PI / 2.0;
    object_width = depth;
  }
  else if (axis == Y_AXIS)
  {
    length_along_a = depth;
    length_along_b = height;
    length_along_c = width;
    a_dir = grasp_pose.rotation() * Eigen::Vector3d::UnitX();
    b_dir = grasp_pose.rotation() * Eigen::Vector3d::UnitZ();
    c_dir = grasp_pose.rotation() * Eigen::Vector3d::UnitY();
    alpha_x = 0;
    alpha_y = M_PI / 2.0;
    alpha_z = M_PI;
    object_width = width;
  }
  else  // Z_AXIS
  {
    length_along_a = depth;
    length_along_b = width;
    length_along_c = height;
    a_dir = grasp_pose.rotation() * Eigen::Vector3d::UnitX();
    b_dir = grasp_pose.rotation() * Eigen::Vector3d::UnitY();
    c_dir = grasp_pose.rotation() * Eigen::Vector3d::UnitZ();
    alpha_x = M_PI / 2.0;
    alpha_y = M_PI / 2.0;
    alpha_z = 0;
    object_width = height;
  }

  double rotation_angles[3];
  rotation_angles[0] = alpha_x;
  rotation_angles[1] = alpha_y;
  rotation_angles[2] = alpha_z;

  a_dir = a_dir.normalized();
  b_dir = b_dir.normalized();
  c_dir = c_dir.normalized();

  // Add grasps at corners, grasps are centroid aligned
  double offset = 0.001;  // back the palm off of the object slightly
  Eigen::Vector3d corner_translation_a;
  Eigen::Vector3d corner_translation_b;
  double angle_res = grasp_data->angle_resolution_ * M_PI / 180.0;
  std::size_t num_radial_grasps = ceil((M_PI / 2.0) / angle_res);
  Eigen::Vector3d translation;

  if (grasp_candidate_config.enable_corner_grasps_)
  {
    ROS_DEBUG_STREAM_NAMED("cuboid_axis_grasps", "adding corner grasps...");
    corner_translation_a = 0.5 * (length_along_a + offset) * a_dir;
    corner_translation_b = 0.5 * (length_along_b + offset) * b_dir;

    if (num_radial_grasps <= 0)
      num_radial_grasps = 1;

    // move to corner 0.5 * ( -a, -b)
    translation = -corner_translation_a - corner_translation_b;
    addCornerGraspsHelper(cuboid_pose, rotation_angles, translation, 0.0, num_radial_grasps, grasp_poses);

    // move to corner 0.5 * ( -a, +b)
    translation = -corner_translation_a + corner_translation_b;
    addCornerGraspsHelper(cuboid_pose, rotation_angles, translation, -M_PI / 2.0, num_radial_grasps, grasp_poses);

    // move to corner 0.5 * ( +a, +b)
    translation = corner_translation_a + corner_translation_b;
    addCornerGraspsHelper(cuboid_pose, rotation_angles, translation, M_PI, num_radial_grasps, grasp_poses);

    // move to corner 0.5 * ( +a, -b)
    translation = corner_translation_a - corner_translation_b;
    addCornerGraspsHelper(cuboid_pose, rotation_angles, translation, M_PI / 2.0, num_radial_grasps, grasp_poses);
  }
  std::size_t num_corner_grasps = grasp_poses.size();

  // Create grasps along faces of cuboid, grasps are axis aligned
  std::size_t num_grasps_along_a;
  std::size_t num_grasps_along_b;
  double rotation;
  Eigen::Vector3d a_translation;
  Eigen::Vector3d b_translation;
  Eigen::Vector3d delta;

  // get exact deltas for sides from desired delta
  num_grasps_along_a = floor((length_along_a - grasp_data->gripper_finger_width_) / grasp_data->grasp_resolution_) + 1;
  num_grasps_along_b = floor((length_along_b - grasp_data->gripper_finger_width_) / grasp_data->grasp_resolution_) + 1;

  // if the gripper fingers are wider than the object we're trying to grasp, try with gripper aligned with
  // top/center/bottom of object
  // note that current implementation limits objects that are the same size as the gripper_finger_width to 1 grasp
  if (num_grasps_along_a <= 0)
  {
    delta_a = length_along_a - grasp_data->gripper_finger_width_ / 2.0;
    num_grasps_along_a = 3;
  }
  if (num_grasps_along_b <= 0)
  {
    delta_b = length_along_b - grasp_data->gripper_finger_width_ / 2.0;
    num_grasps_along_b = 3;
  }

  if (num_grasps_along_a == 1)
    delta_a = 0;
  else
    delta_a = (length_along_a - grasp_data->gripper_finger_width_) / static_cast<double>(num_grasps_along_a - 1);

  if (num_grasps_along_b == 1)
    delta_b = 0;
  else
    delta_b = (length_along_b - grasp_data->gripper_finger_width_) / static_cast<double>(num_grasps_along_b - 1);

  ROS_DEBUG_STREAM_NAMED("cuboid_axis_grasps", "delta_a : delta_b = " << delta_a << " : " << delta_b);
  ROS_DEBUG_STREAM_NAMED("cuboid_axis_grasps", "num_grasps_along_a : num_grasps_along_b  = "
                                                   << num_grasps_along_a << " : " << num_grasps_along_b);

  // TODO(mlautman): There is a bug with face grasps allowing the grasp generator to generate grasps where the gripper
  // fingers
  //                 are in collision with the object being grasped
  if (grasp_candidate_config.enable_face_grasps_)
  {
    ROS_DEBUG_STREAM_NAMED("cuboid_axis_grasps", "adding face grasps...");

    a_translation = -(0.5 * (length_along_a + offset) * a_dir) -
                    0.5 * (length_along_b - grasp_data->gripper_finger_width_) * b_dir - delta_b * b_dir;
    b_translation = -0.5 * (length_along_a - grasp_data->gripper_finger_width_) * a_dir - delta_a * a_dir -
                    (0.5 * (length_along_b + offset) * b_dir);

    // grasps along -a_dir face
    delta = delta_b * b_dir;
    rotation = 0.0;
    addFaceGraspsHelper(cuboid_pose, rotation_angles, a_translation, delta, rotation, num_grasps_along_b, grasp_poses);

    // grasps along +b_dir face
    rotation = -M_PI / 2.0;
    delta = -delta_a * a_dir;
    addFaceGraspsHelper(cuboid_pose, rotation_angles, -b_translation, delta, rotation, num_grasps_along_b, grasp_poses);

    // grasps along +a_dir face
    rotation = M_PI;
    delta = -delta_b * b_dir;
    addFaceGraspsHelper(cuboid_pose, rotation_angles, -a_translation, delta, rotation, num_grasps_along_b, grasp_poses);

    // grasps along -b_dir face
    rotation = M_PI / 2.0;
    delta = delta_a * a_dir;
    addFaceGraspsHelper(cuboid_pose, rotation_angles, b_translation, delta, rotation, num_grasps_along_b, grasp_poses);
  }

  // add grasps at variable angles
  ROS_DEBUG_STREAM_NAMED("cuboid_axis_grasps", "adding variable angle grasps...");
  Eigen::Isometry3d base_pose;
  std::size_t num_grasps = grasp_poses.size();
  if (grasp_candidate_config.enable_variable_angle_grasps_)
  {
    for (std::size_t i = num_corner_grasps; i < num_grasps;
         i++)  // corner grasps at zero depth don't need variable angles
    {
      base_pose = grasp_poses[i];

      grasp_pose = base_pose * Eigen::AngleAxisd(angle_res, Eigen::Vector3d::UnitY());
      std::size_t max_iterations = M_PI / angle_res + 1;
      std::size_t iterations = 0;
      while (graspIntersectionHelper(cuboid_pose, depth, width, height, grasp_pose, grasp_data))
      {
        grasp_poses.push_back(grasp_pose);
        // visual_tools_->publishZArrow(grasp_pose, rviz_visual_tools::BLUE, rviz_visual_tools::XSMALL, 0.02);
        grasp_pose *= Eigen::AngleAxisd(angle_res, Eigen::Vector3d::UnitY());
        // ros::Duration(0.2).sleep();
        iterations++;
        if (iterations > max_iterations)
        {
          ROS_WARN_STREAM_NAMED("cuboid_axis_grasps", "exceeded max iterations while creating variable angle grasps");
          break;
        }
      }

      iterations = 0;
      grasp_pose = base_pose * Eigen::AngleAxisd(-angle_res, Eigen::Vector3d::UnitY());
      while (graspIntersectionHelper(cuboid_pose, depth, width, height, grasp_pose, grasp_data))
      {
        grasp_poses.push_back(grasp_pose);
        // visual_tools_->publishZArrow(grasp_pose, rviz_visual_tools::CYAN, rviz_visual_tools::XSMALL, 0.02);
        grasp_pose *= Eigen::AngleAxisd(-angle_res, Eigen::Vector3d::UnitY());
        // ros::Duration(0.2).sleep();
        iterations++;
        if (iterations > max_iterations)
        {
          ROS_WARN_STREAM_NAMED("cuboid_axis_grasps", "exceeded max iterations while creating variable angle grasps");
          break;
        }
      }
    }
  }

  if (grasp_candidate_config.enable_edge_grasps_)
  {
    // Add grasps along edges
    // move grasp pose to edge of cuboid
    double a_sign = 1.0;
    double b_sign = 1.0;
    double a_rot_sign = 1.0;
    double b_rot_sign = 1.0;

    if (axis == Y_AXIS)
    {
      a_sign = -1.0;
      b_rot_sign = -1.0;
    }

    if (axis == Z_AXIS)
    {
      a_sign = -1.0;
      b_sign = -1.0;
      a_rot_sign = -1.0;
      b_rot_sign = -1.0;
    }

    a_translation = -0.5 * (length_along_a + offset) * a_dir -
                    0.5 * (length_along_b - grasp_data->gripper_finger_width_) * b_dir - delta_b * b_dir -
                    0.5 * (length_along_c + offset) * c_dir * a_sign;
    b_translation = -0.5 * (length_along_a - grasp_data->gripper_finger_width_) * a_dir - delta_a * a_dir -
                    (0.5 * (length_along_b + offset) * b_dir) - 0.5 * (length_along_c + offset) * c_dir * b_sign;

    // grasps along -a_dir face
    delta = delta_b * b_dir;
    rotation = 0.0;
    addEdgeGraspsHelper(cuboid_pose, rotation_angles, a_translation, delta, rotation, num_grasps_along_b, grasp_poses,
                        -M_PI / 4.0 * a_rot_sign);

    // grasps along +b_dir face
    rotation = -M_PI / 2.0;
    delta = -delta_a * a_dir;
    addEdgeGraspsHelper(cuboid_pose, rotation_angles, -b_translation, delta, rotation, num_grasps_along_b, grasp_poses,
                        M_PI / 4.0 * b_rot_sign);

    // grasps along +a_dir face
    rotation = M_PI;
    delta = -delta_b * b_dir;
    addEdgeGraspsHelper(cuboid_pose, rotation_angles, -a_translation, delta, rotation, num_grasps_along_b, grasp_poses,
                        M_PI / 4.0 * a_rot_sign);

    // grasps along -b_dir face
    rotation = M_PI / 2.0;
    delta = delta_a * a_dir;
    addEdgeGraspsHelper(cuboid_pose, rotation_angles, b_translation, delta, rotation, num_grasps_along_b, grasp_poses,
                        -M_PI / 4.0 * b_rot_sign);
  }
  // Add grasps at variable depths
  ROS_DEBUG_STREAM_NAMED("cuboid_axis_grasps", "adding depth grasps...");
  std::size_t num_depth_grasps = ceil(finger_depth / grasp_data->grasp_depth_resolution_);
  if (num_depth_grasps <= 0)
    num_depth_grasps = 1;
  delta_f = finger_depth / static_cast<double>(num_depth_grasps);

  num_grasps = grasp_poses.size();
  Eigen::Vector3d grasp_dir;
  Eigen::Isometry3d depth_pose;

  for (std::size_t i = 0; i < num_grasps; i++)
  {
    grasp_dir = grasp_poses[i].rotation() * Eigen::Vector3d::UnitZ();
    depth_pose = grasp_poses[i];
    for (std::size_t j = 0; j < num_depth_grasps; j++)
    {
      depth_pose.translation() += delta_f * grasp_dir;
      grasp_poses.push_back(depth_pose);
    }
  }

  // add grasps in both directions
  ROS_DEBUG_STREAM_NAMED("cuboid_axis_grasps", "adding bi-directional grasps...");
  num_grasps = grasp_poses.size();
  for (std::size_t i = 0; i < num_grasps; i++)
  {
    grasp_pose = grasp_poses[i];
    grasp_pose *= Eigen::AngleAxisd(M_PI, Eigen::Vector3d::UnitZ());
    grasp_poses.push_back(grasp_pose);
  }

  // compute min/max distances to object
  ROS_DEBUG_STREAM_NAMED("cuboid_axis_grasps", "computing min/max grasp distance...");
  num_grasps = grasp_poses.size();
  min_grasp_distance_ = std::numeric_limits<double>::max();
  max_grasp_distance_ = std::numeric_limits<double>::min();
  min_translations_ = Eigen::Vector3d(std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                                      std::numeric_limits<double>::max());
  max_translations_ = Eigen::Vector3d(std::numeric_limits<double>::min(), std::numeric_limits<double>::min(),
                                      std::numeric_limits<double>::min());
  double grasp_distance;

  for (std::size_t i = 0; i < num_grasps; i++)
  {
    grasp_pose = grasp_poses[i];
    grasp_distance = (grasp_pose.translation() - cuboid_pose.translation()).norm();
    if (grasp_distance > max_grasp_distance_)
      max_grasp_distance_ = grasp_distance;

    if (grasp_distance < min_grasp_distance_)
      min_grasp_distance_ = grasp_distance;

    for (std::size_t j = 0; j < 3; j++)
    {
      if (grasp_pose.translation()[j] < min_translations_[j])
        min_translations_[j] = grasp_pose.translation()[j];

      if (grasp_pose.translation()[j] > max_translations_[j])
        max_translations_[j] = grasp_pose.translation()[j];
    }
  }

  ROS_DEBUG_STREAM_NAMED("grasp_generator.add", "min/max distance = " << min_grasp_distance_ << ", "
                                                                      << max_grasp_distance_);

  // add all poses as possible grasps
  std::size_t num_grasps_added = 0;

  for (std::size_t i = 0; i < grasp_poses.size(); i++)
  {
    if (!addGrasp(grasp_poses[i], grasp_data, grasp_candidates, cuboid_pose, object_size, object_width))
    {
      ROS_DEBUG_STREAM_NAMED("grasp_generator.add", "Unable to add grasp - function returned false");
    }
    else
      num_grasps_added++;
  }
  ROS_INFO_STREAM_NAMED("grasp_generator.add", "\033[1;36madded " << num_grasps_added << " of " << grasp_poses.size()
                                                                  << " grasp poses created\033[0m");
  return true;
}

std::size_t GraspGenerator::addFaceGraspsHelper(Eigen::Isometry3d pose, double rotation_angles[3],
                                                Eigen::Vector3d translation, Eigen::Vector3d delta,
                                                double alignment_rotation, std::size_t num_grasps,
                                                std::vector<Eigen::Isometry3d>& grasp_poses)
{
  std::size_t num_grasps_added = 0;
  ROS_DEBUG_STREAM_NAMED("cuboid_axis_grasps.helper", "delta = \n" << delta);
  ROS_DEBUG_STREAM_NAMED("cuboid_axis_grasps.helper", "num_grasps = " << num_grasps);

  Eigen::Isometry3d grasp_pose = pose;
  grasp_pose *= Eigen::AngleAxisd(rotation_angles[0], Eigen::Vector3d::UnitX()) *
                Eigen::AngleAxisd(rotation_angles[1], Eigen::Vector3d::UnitY()) *
                Eigen::AngleAxisd(rotation_angles[2], Eigen::Vector3d::UnitZ());
  grasp_pose *= Eigen::AngleAxisd(alignment_rotation, Eigen::Vector3d::UnitY());
  grasp_pose.translation() += translation;

  for (std::size_t i = 0; i < num_grasps; i++)
  {
    grasp_pose.translation() += delta;
    grasp_poses.push_back(grasp_pose);
    num_grasps_added++;
  }
  ROS_DEBUG_STREAM_NAMED("cuboid_axis_grasps.helper",
                         "num_grasps_added : grasp_poses.size() = " << num_grasps_added << " : " << grasp_poses.size());
  return true;
}

std::size_t GraspGenerator::addEdgeGraspsHelper(Eigen::Isometry3d pose, double rotation_angles[3],
                                                Eigen::Vector3d translation, Eigen::Vector3d delta,
                                                double alignment_rotation, std::size_t num_grasps,
                                                std::vector<Eigen::Isometry3d>& grasp_poses, double corner_rotation)
{
  std::size_t num_grasps_added = 0;
  ROS_DEBUG_STREAM_NAMED("cuboid_axis_grasps.helper", "delta = \n" << delta);
  ROS_DEBUG_STREAM_NAMED("cuboid_axis_grasps.helper", "num_grasps = " << num_grasps);

  Eigen::Isometry3d grasp_pose = pose;
  grasp_pose *= Eigen::AngleAxisd(rotation_angles[0], Eigen::Vector3d::UnitX()) *
                Eigen::AngleAxisd(rotation_angles[1], Eigen::Vector3d::UnitY()) *
                Eigen::AngleAxisd(rotation_angles[2], Eigen::Vector3d::UnitZ());
  grasp_pose *= Eigen::AngleAxisd(alignment_rotation, Eigen::Vector3d::UnitY());

  // rotate towards cuboid
  grasp_pose *= Eigen::AngleAxisd(corner_rotation, Eigen::Vector3d::UnitX());
  grasp_pose.translation() += translation;

  for (std::size_t i = 0; i < num_grasps; i++)
  {
    grasp_pose.translation() += delta;
    grasp_poses.push_back(grasp_pose);
    num_grasps_added++;
  }
  ROS_DEBUG_STREAM_NAMED("cuboid_axis_grasps.helper",
                         "num_grasps_added : grasp_poses.size() = " << num_grasps_added << " : " << grasp_poses.size());
  return true;
}

std::size_t GraspGenerator::addCornerGraspsHelper(Eigen::Isometry3d pose, double rotation_angles[3],
                                                  Eigen::Vector3d translation, double corner_rotation,
                                                  std::size_t num_radial_grasps,
                                                  std::vector<Eigen::Isometry3d>& grasp_poses)
{
  std::size_t num_grasps_added = 0;
  double delta_angle = (M_PI / 2.0) / static_cast<double>(num_radial_grasps + 1);
  ROS_DEBUG_STREAM_NAMED("cuboid_axis_grasps.helper", "delta_angle = " << delta_angle);
  ROS_DEBUG_STREAM_NAMED("cuboid_axis_grasps.helper", "num_radial_grasps = " << num_radial_grasps);

  // rotate & translate pose to be aligned with edge of cuboid
  Eigen::Isometry3d grasp_pose = pose;
  grasp_pose *= Eigen::AngleAxisd(rotation_angles[0], Eigen::Vector3d::UnitX()) *
                Eigen::AngleAxisd(rotation_angles[1], Eigen::Vector3d::UnitY()) *
                Eigen::AngleAxisd(rotation_angles[2], Eigen::Vector3d::UnitZ());
  grasp_pose *= Eigen::AngleAxisd(corner_rotation, Eigen::Vector3d::UnitY());
  grasp_pose.translation() += translation;

  for (std::size_t i = 0; i < num_radial_grasps; i++)
  {
    // Eigen::Vector3d grasp_dir = grasp_pose.rotation() * Eigen::Vector3d::UnitZ();
    // Eigen::Isometry3d radial_pose = grasp_pose;
    grasp_pose *= Eigen::AngleAxisd(delta_angle, Eigen::Vector3d::UnitY());
    grasp_poses.push_back(grasp_pose);
    num_grasps_added++;
  }
  ROS_DEBUG_STREAM_NAMED("cuboid_axis_grasps.helper",
                         "num_grasps_added : grasp_poses.size() = " << num_grasps_added << " : " << grasp_poses.size());
  return num_grasps_added;
}

bool GraspGenerator::graspIntersectionHelper(Eigen::Isometry3d cuboid_pose, double depth, double width, double height,
                                             Eigen::Isometry3d grasp_pose, const GraspDataPtr grasp_data)
{
  // TODO(davetcoleman): add parameter to enable vizualization commented lines after further testing

  // get line segment from grasp point to fingertip
  Eigen::Vector3d point_a = grasp_pose.translation();
  Eigen::Vector3d point_b = point_a + grasp_pose.rotation() * Eigen::Vector3d::UnitZ() * grasp_data->grasp_max_depth_;

  // translate points into cuboid coordinate system
  point_a = cuboid_pose.inverse() * point_a;  // T_cuboid-world * p_world = p_cuboid
  point_b = cuboid_pose.inverse() * point_b;

  // if (verbose_)
  // {
  //   visual_tools_->publishCuboid(visual_tools_->convertPose(Eigen::Isometry3d::Identity()), depth, width, height,
  //   rviz_visual_tools::TRANSLUCENT);
  //   visual_tools_->publishAxis(Eigen::Isometry3d::Identity());
  //   visual_tools_->publishSphere(point_a, rviz_visual_tools::WHITE, 0.005);
  //   visual_tools_->publishSphere(point_b, rviz_visual_tools::GREY, 0.005);
  //   visual_tools_->publishLine(point_a, point_b, rviz_visual_tools::BLUE, rviz_visual_tools::XSMALL);
  // }

  double t, u, v;
  Eigen::Vector3d intersection;
  // check if line segment intersects XY faces of cuboid (z = +/- height/2)
  t = (height / 2.0 - point_a[2]) / (point_b[2] - point_a[2]);  // parameterization of line segment in 3d
  if (intersectionHelper(t, point_a[0], point_a[1], point_b[0], point_b[1], depth, width, u, v))
  {
    // if (verbose_)
    // {
    //   intersection[0]= u;
    //   intersection[1]= v;
    //   intersection[2]= height / 2.0;
    //   visual_tools_->publishSphere(intersection, rviz_visual_tools::BLUE, 0.005);
    // }
    return true;
  }

  t = (-height / 2.0 - point_a[2]) / (point_b[2] - point_a[2]);
  if (intersectionHelper(t, point_a[0], point_a[1], point_b[0], point_b[1], depth, width, u, v))
  {
    // if (verbose_)
    // {
    //   intersection[0]= u;
    //   intersection[1]= v;
    //   intersection[2]= -height / 2.0;
    //   visual_tools_->publishSphere(intersection, rviz_visual_tools::CYAN, 0.005);
    // }
    return true;
  }

  // check if line segment intersects XZ faces of cuboid (y = +/- width/2)
  t = (width / 2.0 - point_a[1]) / (point_b[1] - point_a[1]);
  if (intersectionHelper(t, point_a[0], point_a[2], point_b[0], point_b[2], depth, height, u, v))
  {
    // if (verbose_)
    // {
    //   intersection[0]= u;
    //   intersection[1]= width / 2.0;
    //   intersection[2]= v;
    //   visual_tools_->publishSphere(intersection, rviz_visual_tools::GREEN, 0.005);
    // }
    return true;
  }

  t = (-width / 2.0 - point_a[1]) / (point_b[1] - point_a[1]);
  if (intersectionHelper(t, point_a[0], point_a[2], point_b[0], point_b[2], depth, height, u, v))
  {
    // if (verbose_)
    // {
    //   intersection[0]= u;
    //   intersection[1]= -width / 2.0;
    //   intersection[2]= v;
    //   visual_tools_->publishSphere(intersection, rviz_visual_tools::LIME_GREEN, 0.005);
    // }
    return true;
  }

  // check if line segment intersects YZ faces of cuboid (x = +/- depth/2)
  t = (depth / 2.0 - point_a[0]) / (point_b[0] - point_a[0]);
  if (intersectionHelper(t, point_a[1], point_a[2], point_b[1], point_b[2], width, height, u, v))
  {
    // if (verbose_)
    // {
    //   intersection[0]= depth / 2.0;
    //   intersection[1]= u;
    //   intersection[2]= v;
    //   visual_tools_->publishSphere(intersection, rviz_visual_tools::RED, 0.005);
    // }
    return true;
  }

  t = (-depth / 2.0 - point_a[0]) / (point_b[0] - point_a[0]);
  if (intersectionHelper(t, point_a[1], point_a[2], point_b[1], point_b[2], width, height, u, v))
  {
    // if (verbose_)
    // {
    //   intersection[0]= -depth / 2.0;
    //   intersection[1]= u;
    //   intersection[2]= v;
    //   visual_tools_->publishSphere(intersection, rviz_visual_tools::PINK, 0.005);
    // }
    return true;
  }

  // no intersection found
  return false;
}

bool GraspGenerator::intersectionHelper(double t, double u1, double v1, double u2, double v2, double a, double b,
                                        double& u, double& v)
{
  // plane must cross through our line segment
  if (t >= 0 && t <= 1)
  {
    u = u1 + t * (u2 - u1);
    v = v1 + t * (v2 - v1);

    if (u >= -a / 2 && u <= a / 2 && v >= -b / 2 && v <= b / 2)
      return true;
  }

  return false;
}

bool GraspGenerator::addGrasp(const Eigen::Isometry3d& grasp_pose, const GraspDataPtr grasp_data,
                              std::vector<GraspCandidatePtr>& grasp_candidates, const Eigen::Isometry3d& object_pose,
                              const Eigen::Vector3d& object_size, double object_width)
{
  if (verbose_)
  {
    visual_tools_->publishZArrow(grasp_pose, rviz_visual_tools::GREEN, rviz_visual_tools::XXSMALL, 0.05);
    visual_tools_->trigger();
    ros::Duration(0.01).sleep();
  }

  // The new grasp
  moveit_msgs::Grasp new_grasp;

  // Approach and retreat - aligned with eef to grasp transform
  // set pregrasp
  moveit_msgs::GripperTranslation pre_grasp_approach;
  new_grasp.pre_grasp_approach.direction.header.stamp = ros::Time::now();
  new_grasp.pre_grasp_approach.desired_distance = grasp_data->grasp_max_depth_ + grasp_data->approach_distance_desired_;
  new_grasp.pre_grasp_approach.min_distance = 0;  // NOT IMPLEMENTED
  new_grasp.pre_grasp_approach.direction.header.frame_id = grasp_data->parent_link_->getName();

  Eigen::Vector3d grasp_approach_vector = -1 * grasp_data->grasp_pose_to_eef_pose_.translation();
  grasp_approach_vector = grasp_approach_vector / grasp_approach_vector.norm();

  new_grasp.pre_grasp_approach.direction.vector.x = grasp_approach_vector.x();
  new_grasp.pre_grasp_approach.direction.vector.y = grasp_approach_vector.y();
  new_grasp.pre_grasp_approach.direction.vector.z = grasp_approach_vector.z();

  // set postgrasp
  moveit_msgs::GripperTranslation post_grasp_retreat;
  new_grasp.post_grasp_retreat.direction.header.stamp = ros::Time::now();
  new_grasp.post_grasp_retreat.desired_distance = grasp_data->grasp_max_depth_ + grasp_data->retreat_distance_desired_;
  new_grasp.post_grasp_retreat.min_distance = 0;  // NOT IMPLEMENTED
  new_grasp.post_grasp_retreat.direction.header.frame_id = grasp_data->parent_link_->getName();
  new_grasp.post_grasp_retreat.direction.vector.x = -1 * grasp_approach_vector.x();
  new_grasp.post_grasp_retreat.direction.vector.y = -1 * grasp_approach_vector.y();
  new_grasp.post_grasp_retreat.direction.vector.z = -1 * grasp_approach_vector.z();

  // set grasp pose
  geometry_msgs::PoseStamped grasp_pose_msg;
  grasp_pose_msg.header.stamp = ros::Time::now();
  grasp_pose_msg.header.frame_id = grasp_data->base_link_;

  // name the grasp
  static std::size_t grasp_id = 0;
  new_grasp.id = "Grasp" + boost::lexical_cast<std::string>(grasp_id);
  grasp_id++;

  // Translate and rotate gripper to match standard orientation
  // origin on palm, z pointing outward, x perp to gripper close, y parallel to gripper close direction
  // Transform the grasp pose

  Eigen::Isometry3d eef_pose = grasp_pose * grasp_data->grasp_pose_to_eef_pose_;

  tf::poseEigenToMsg(eef_pose, grasp_pose_msg.pose);
  new_grasp.grasp_pose = grasp_pose_msg;

  // set grasp postures e.g. hand closed
  new_grasp.grasp_posture = grasp_data->grasp_posture_;

  if (grasp_data->end_effector_type_ == FINGER)
  {
    // set minimum opening of fingers for pre grasp approach
    double min_finger_open_on_approach = object_width + 2 * grasp_data->grasp_padding_on_approach_;
    double percent_open;

    // Create grasp with widest fingers possible ----------------------------------------------
    percent_open = 1.0;
    if (!grasp_data->setGraspWidth(percent_open, min_finger_open_on_approach, new_grasp.pre_grasp_posture))
    {
      debugFailedOpenGripper(percent_open, min_finger_open_on_approach, object_width,
                             grasp_data->grasp_padding_on_approach_);
      return false;
    }

    new_grasp.grasp_quality = scoreFingerGrasp(grasp_pose, grasp_data, object_pose, percent_open);

    // Show visualization for widest grasp

    grasp_candidates.push_back(GraspCandidatePtr(new GraspCandidate(new_grasp, grasp_data, object_pose)));

    // Create grasp with middle width fingers -------------------------------------------------
    percent_open = 0.5;
    if (!grasp_data->setGraspWidth(percent_open, min_finger_open_on_approach, new_grasp.pre_grasp_posture))
    {
      debugFailedOpenGripper(percent_open, min_finger_open_on_approach, object_width,
                             grasp_data->grasp_padding_on_approach_);
      return false;
    }
    new_grasp.grasp_quality = scoreFingerGrasp(grasp_pose, grasp_data, object_pose, percent_open);
    grasp_candidates.push_back(GraspCandidatePtr(new GraspCandidate(new_grasp, grasp_data, object_pose)));

    // Create grasp with fingers at minimum width ---------------------------------------------
    percent_open = 0.0;
    if (!grasp_data->setGraspWidth(percent_open, min_finger_open_on_approach, new_grasp.pre_grasp_posture))
    {
      debugFailedOpenGripper(percent_open, min_finger_open_on_approach, object_width,
                             grasp_data->grasp_padding_on_approach_);
      return false;
    }
    new_grasp.grasp_quality = scoreFingerGrasp(grasp_pose, grasp_data, object_pose, percent_open);
    grasp_candidates.push_back(GraspCandidatePtr(new GraspCandidate(new_grasp, grasp_data, object_pose)));

    return true;
  }

  if (grasp_data->end_effector_type_ == SUCTION)
  {
    new_grasp.grasp_quality = scoreSuctionGrasp(grasp_pose, grasp_data, object_pose, object_size);
    grasp_candidates.push_back(GraspCandidatePtr(new GraspCandidate(new_grasp, grasp_data, object_pose)));
    return true;
  }

  return false;
}

double GraspGenerator::scoreSuctionGrasp(const Eigen::Isometry3d& grasp_pose, const GraspDataPtr& grasp_data,
                                         const Eigen::Isometry3d& cuboid_pose, const Eigen::Vector3d& object_size)
{
  ROS_DEBUG_STREAM_NAMED("grasp_generator.scoreGrasp",
                         "Scoring grasp at: \n\tpose:  ("
                             << grasp_pose.translation().x() << ",\t" << grasp_pose.translation().y() << ",\t"
                             << grasp_pose.translation().z() << ")\t(" << grasp_pose.rotation().eulerAngles(0, 1, 2)(0)
                             << ",\t" << grasp_pose.rotation().eulerAngles(0, 1, 2)(1) << ",\t"
                             << grasp_pose.rotation().eulerAngles(0, 1, 2)(2) << ")\n\tideal: ("
                             << ideal_grasp_pose_.translation().x() << ",\t" << ideal_grasp_pose_.translation().y()
                             << ",\t" << ideal_grasp_pose_.translation().z() << ")\t("
                             << ideal_grasp_pose_.rotation().eulerAngles(0, 1, 2)(0) << ",\t"
                             << ideal_grasp_pose_.rotation().eulerAngles(0, 1, 2)(1) << ",\t"
                             << ideal_grasp_pose_.rotation().eulerAngles(0, 1, 2)(2) << ")");

  // get portion of score based on the orientation
  Eigen::Isometry3d ideal_grasp = getIdealGraspPose();
  // Move the ideal top grasp to the box location
  ideal_grasp.translation() = cuboid_pose.translation();
  Eigen::Vector3d orientation_scores = GraspScorer::scoreRotationsFromDesired(grasp_pose, ideal_grasp);

  // get portion of score based on the translation
  Eigen::Vector3d translation_scores = GraspScorer::scoreGraspTranslation(grasp_pose, ideal_grasp);

  // Score suction grasp overhang
  Eigen::Vector2d overhang_score;
  if (show_grasp_overhang_)
    overhang_score = GraspScorer::scoreGraspOverhang(grasp_pose, grasp_data, cuboid_pose, object_size, visual_tools_);
  else
    overhang_score = GraspScorer::scoreGraspOverhang(grasp_pose, grasp_data, cuboid_pose, object_size);

  std::size_t num_scores = 8;
  double weights[num_scores] = {
    grasp_score_weights_.orientation_x_score_weight_, grasp_score_weights_.orientation_y_score_weight_,
    grasp_score_weights_.orientation_z_score_weight_, grasp_score_weights_.translation_x_score_weight_,
    grasp_score_weights_.translation_y_score_weight_, grasp_score_weights_.translation_z_score_weight_,
    grasp_score_weights_.overhang_score_weight_,      grasp_score_weights_.overhang_score_weight_
  };

  double scores[num_scores] = { orientation_scores[0], orientation_scores[1], orientation_scores[2],
                                translation_scores[0], translation_scores[1], translation_scores[2],
                                overhang_score[0],     overhang_score[1] };

  double total_score = 0;
  double weight_total = 0;
  for (std::size_t i = 0; i < num_scores; i++)
  {
    total_score += weights[i] * scores[i];
    weight_total += weights[i];
  }
  total_score /= weight_total;

  ROS_DEBUG_STREAM_NAMED("grasp_generator.scoreGrasp",
                         "Grasp score: \n "
                             << "\torientation_score.x = " << orientation_scores[0] << "\n"
                             << "\torientation_score.y = " << orientation_scores[1] << "\n"
                             << "\torientation_score.z = " << orientation_scores[2] << "\n"
                             << "\ttranslation_score.x = " << translation_scores[0] << "\n"
                             << "\ttranslation_score.y = " << translation_scores[1] << "\n"
                             << "\ttranslation_score.z = " << translation_scores[2] << "\n"
                             << "\toverhang_score.x = " << overhang_score[0] << "\n"
                             << "\toverhang_score.y = " << overhang_score[1] << "\n"
                             << "\tweights             = " << weights[0] << ", " << weights[1] << ", " << weights[2]
                             << ", " << weights[3] << ", " << weights[4] << ", " << weights[5] << ", " << weights[6]
                             << ", " << weights[7] << "\n"
                             << "\ttotal_score = " << total_score);
  return total_score;
}

double GraspGenerator::scoreFingerGrasp(const Eigen::Isometry3d& grasp_pose, const GraspDataPtr& grasp_data,
                                        const Eigen::Isometry3d& object_pose, double percent_open)
{
  ROS_DEBUG_STREAM_NAMED("grasp_generator.scoreGrasp", "starting to score grasp...");

  // get portion of score based on the gripper's opening width on approach
  double width_score = GraspScorer::scoreGraspWidth(grasp_data, percent_open);

  // get portion of score based on the pinchers being down
  Eigen::Vector3d orientation_scores = GraspScorer::scoreRotationsFromDesired(grasp_pose, ideal_grasp_pose_);

  // get portion of score based on the distance of the grasp pose to the object pose

  // NOTE: when this function is called we've lost the references to the acutal size of the object.
  // max_distance should be the length of the fingers minus some minimum amount that the fingers need to grip an object
  // since we don't know the distance from the centoid of the object to the edge of the object, this is set as an
  // arbitrary number given our target object set
  double distance_score =
      GraspScorer::scoreDistanceToPalm(grasp_pose, grasp_data, object_pose, min_grasp_distance_, max_grasp_distance_);

  // should really change this to be like orienation_scores so we can score any translation
  Eigen::Vector3d translation_scores =
      GraspScorer::scoreGraspTranslation(grasp_pose, min_translations_, max_translations_);

  // want minimum translation
  translation_scores *= -1.0;
  translation_scores += Eigen::Vector3d(1.0, 1.0, 1.0);

  // Get total score
  std::size_t num_scores = 8;
  double weights[num_scores], scores[num_scores];
  weights[0] = grasp_score_weights_.width_score_weight_;
  weights[1] = grasp_score_weights_.orientation_x_score_weight_;
  weights[2] = grasp_score_weights_.orientation_y_score_weight_;
  weights[3] = grasp_score_weights_.orientation_z_score_weight_;
  weights[4] = grasp_score_weights_.depth_score_weight_;
  weights[5] = grasp_score_weights_.translation_x_score_weight_;
  weights[6] = grasp_score_weights_.translation_y_score_weight_;
  weights[7] = grasp_score_weights_.translation_z_score_weight_;

  // Every score is normalized to be in the same range, so new scoring features should also be normalized
  scores[0] = width_score;
  scores[1] = orientation_scores[0];
  scores[2] = orientation_scores[1];
  scores[3] = orientation_scores[2];
  scores[4] = distance_score;
  scores[5] = translation_scores[0];
  scores[6] = translation_scores[1];
  scores[7] = translation_scores[2];

  double total_score = 0;
  double high_score = 0;
  for (std::size_t i = 0; i < num_scores; i++)
  {
    total_score += weights[i] * scores[i];
    high_score += weights[i];
  }
  total_score /= high_score;

  if (verbose_)
  {
    ROS_DEBUG_STREAM_NAMED("grasp_generator.scoreGrasp",
                           "Grasp score: \n "
                               << "\twidth_score         = " << width_score << "\n"
                               << "\torientation_score.x = " << orientation_scores[0] << "\n"
                                                                                         "\torientation_score.y = "
                               << orientation_scores[1] << "\n"
                                                           "\torientation_score.z = "
                               << orientation_scores[2] << "\n"
                                                           "\tdistance_score      = "
                               << distance_score << "\n"
                               << "\ttranslation_score.x = " << translation_scores[0] << "\n"
                                                                                         "\ttranslation_score.y = "
                               << translation_scores[1] << "\n"
                                                           "\ttranslation_score.z = "
                               << translation_scores[2] << "\n"
                                                           "\tweights             = "
                               << weights[0] << ", " << weights[1] << ", " << weights[2] << ", " << weights[3] << ", "
                               << weights[4] << ", " << weights[5] << ", " << weights[6] << ", " << weights[7] << "\n"
                               << "\ttotal_score         = " << total_score);
    visual_tools_->publishSphere(grasp_pose.translation(), rviz_visual_tools::PINK, 0.01 * total_score);

    if (false)
    {
      char entry[256];
      ROS_INFO_STREAM_NAMED("grasp_generator", "\033[1;36m\nPress 'Enter' to continue. Enter 'y' to skip scoring "
                                               "info...\033[0m");
      std::cin.getline(entry, 256);
      if (entry[0] == 'y')
        verbose_ = false;
    }
  }

  return total_score;
}

bool GraspGenerator::generateGrasps(const Eigen::Isometry3d& cuboid_pose, double depth, double width, double height,
                                    const moveit_grasps::GraspDataPtr grasp_data,
                                    std::vector<GraspCandidatePtr>& grasp_candidates,
                                    const GraspCandidateConfig grasp_candidate_config)
{
  if (grasp_data->end_effector_type_ == FINGER)
    return generateFingerGrasps(cuboid_pose, depth, width, height, grasp_data, grasp_candidates,
                                grasp_candidate_config);
  if (grasp_data->end_effector_type_ == SUCTION)
    return generateSuctionGrasps(cuboid_pose, depth, width, height, grasp_data, grasp_candidates,
                                 grasp_candidate_config);
  else
    return false;
}

bool GraspGenerator::generateSuctionGrasps(const Eigen::Isometry3d& cuboid_top_pose, double depth, double width,
                                           double height, const moveit_grasps::GraspDataPtr grasp_data,
                                           std::vector<GraspCandidatePtr>& grasp_candidates,
                                           const GraspCandidateConfig grasp_candidate_config)
{
  grasp_candidates.clear();
  std::vector<Eigen::Isometry3d> grasp_poses;
  ////////////////
  // Re-orient the cuboid center top grasp so to be as close as possible to the ideal grasp
  ////////////////

  // Move the ideal grasp pose to the center of the top of the box
  Eigen::Isometry3d ideal_grasp = getIdealGraspPose();
  Eigen::Isometry3d cuboid_center_top_grasp(cuboid_top_pose);
  // Move the ideal top grasp to the correct location
  ideal_grasp.translation() = cuboid_center_top_grasp.translation();
  setIdealGraspPose(ideal_grasp);
  Eigen::Vector3d object_size(depth, width, height);

  if (debug_top_grasps_)
  {
    visual_tools_->publishAxis(cuboid_top_pose, rviz_visual_tools::SMALL, "cuboid_top_pose");
    visual_tools_->publishAxis(ideal_grasp, rviz_visual_tools::SMALL, "ideal_grasp");
    visual_tools_->trigger();
  }

  ROS_DEBUG_STREAM_NAMED("grasp_generator", "cuboid_direction:\n" << cuboid_center_top_grasp.rotation() << "\n");
  ROS_DEBUG_STREAM_NAMED("grasp_generator", "ideal_grasp:\n" << ideal_grasp.rotation() << "\n");

  // If the ideal top grasp Z axis is in the opposite direction of the top pose then we rotate around X to flip the
  // orientation vector
  double dot_prodZ = (cuboid_center_top_grasp.rotation() * Eigen::Vector3d::UnitZ())
                         .dot(ideal_grasp.rotation() * Eigen::Vector3d::UnitZ());
  if (dot_prodZ < 0)
  {
    ROS_DEBUG_NAMED("grasp_generator", "flipping Z");
    cuboid_center_top_grasp = cuboid_center_top_grasp * Eigen::AngleAxisd(M_PI, Eigen::Vector3d::UnitX());
    ROS_DEBUG_STREAM_NAMED("grasp_generator", "New cuboid_direction:\n" << cuboid_center_top_grasp.rotation() << "\n");
  }

  // If the ideal top grasp X axis is opposite the top pose then we rotate around Z
  double dot_prodX = (cuboid_center_top_grasp.rotation() * Eigen::Vector3d::UnitX())
                         .dot(ideal_grasp.rotation() * Eigen::Vector3d::UnitX());
  if (dot_prodX < 0)
  {
    ROS_DEBUG_NAMED("generateSuctionGrasps", "flipping X");
    cuboid_center_top_grasp = cuboid_center_top_grasp * Eigen::AngleAxisd(M_PI, Eigen::Vector3d::UnitZ());
    ROS_DEBUG_STREAM_NAMED("grasp_generator", "New cuboid_direction:\n" << cuboid_center_top_grasp.rotation() << "\n");
  }

  ////////////////
  // Create grasp candidate poses.
  ////////////////
  // First add the center point to ensure that it is a candidate

  Eigen::Isometry3d center_grasp_pose =
      cuboid_center_top_grasp * Eigen::Translation3d(0, 0, grasp_data->grasp_min_depth_);

  if (debug_top_grasps_)
  {
    ROS_DEBUG_STREAM_NAMED("grasp_generator", "\n\tWidth:\t" << width << "\n\tDepth:\t" << depth << "\n\tHeight\t"
                                                             << height);
    visual_tools_->publishAxis(center_grasp_pose, rviz_visual_tools::SMALL, "center_grasp_pose");
    visual_tools_->trigger();
  }
  grasp_poses.push_back(center_grasp_pose);

  // We define min, max and inc for each for loop here for readability

  // if X range is less than y range then we use x range for the xy range
  double x_increment = grasp_data->grasp_resolution_;
  double y_increment = grasp_data->grasp_resolution_;
  double x_min = x_increment;
  double y_min = y_increment;
  double x_max, y_max;


  x_max = depth / 2.0 - grasp_data->active_suction_range_x_ / 2.0;

  if (depth - grasp_data->active_suction_range_x_ < width - grasp_data->active_suction_range_y_)
  {
    xy_max = depth / 2.0 - grasp_data->active_suction_range_x_ / 2.0;
  }
  else
  {
    xy_max = width / 2.0 - grasp_data->active_suction_range_y_ / 2.0;
  }

  double z_increment = grasp_data->grasp_depth_resolution_;
  double z_min = z_increment;
  double z_max = grasp_data->grasp_max_depth_ - grasp_data->grasp_min_depth_;

  double yaw_increment = M_PI * (grasp_data->angle_resolution_ / 180.0);
  double yaw_min = yaw_increment;
  double yaw_max = 2.0 * M_PI;

  // For each range (X, Y, Z, Yaw) create copies of the grasp poses for each value in the range
  std::size_t num_grasps;

  // Add rotated suction grasps (Yaw)
  num_grasps = grasp_poses.size();
  for (std::size_t i = 0; i < num_grasps; ++i)
  {
    for (double yaw = yaw_min; yaw < yaw_max; yaw += yaw_increment)
    {
      Eigen::Isometry3d grasp_pose = grasp_poses[i] * Eigen::AngleAxisd(yaw, Eigen::Vector3d::UnitZ());
      grasp_poses.push_back(grasp_pose);
    }
  }

  // Add Depth grasps (Z-axis)
  num_grasps = grasp_poses.size();
  for (std::size_t i = 0; i < num_grasps; ++i)
  {
    for (double z = z_min; z <= z_max; z += z_increment)
    {
      Eigen::Isometry3d grasp_pose = grasp_poses[i] * Eigen::Translation3d(0, 0, z);
      grasp_poses.push_back(grasp_pose);
    }
  }

  // Add Y translation grasps
  num_grasps = grasp_poses.size();
  for (std::size_t i = 0; i < num_grasps; ++i)
  {
    for (double y = xy_min; y <= xy_max; y += xy_increment)
    {
      Eigen::Isometry3d grasp_pose;

      grasp_pose = grasp_poses[i] * Eigen::Translation3d(0, y, 0);
      grasp_poses.push_back(grasp_pose);

      grasp_pose = grasp_poses[i] * Eigen::Translation3d(0, -y, 0);
      grasp_poses.push_back(grasp_pose);
    }
  }

  // Add X translation grasps
  num_grasps = grasp_poses.size();
  for (std::size_t i = 0; i < num_grasps; ++i)
  {
    for (double x = xy_min; x <= xy_max; x += xy_increment)
    {
      Eigen::Isometry3d grasp_pose;

      grasp_pose = grasp_poses[i] * Eigen::Translation3d(x, 0, 0);
      grasp_poses.push_back(grasp_pose);

      grasp_pose = grasp_poses[i] * Eigen::Translation3d(-x, 0, 0);
      grasp_poses.push_back(grasp_pose);
    }
  }

  num_grasps = grasp_poses.size();

  for (std::size_t i = 0; i < num_grasps; ++i)
  {
    addGrasp(grasp_poses[i], grasp_data, grasp_candidates, cuboid_top_pose, object_size, 0);
    if (debug_top_grasps_)
    {
      visual_tools_->publishAxis(grasp_poses[i], rviz_visual_tools::MEDIUM, "pose");
    }
  }
  if (debug_top_grasps_)
  {
    Eigen::Isometry3d ideal_copy = ideal_grasp_pose_;
    ideal_copy.translation() += Eigen::Vector3d(0.0, 0.0, 1.0);
    visual_tools_->publishAxisLabeled(ideal_copy, "ideal grasp orientation", rviz_visual_tools::MEDIUM);
    visual_tools_->trigger();
  }

  if (!grasp_candidates.size())
    ROS_WARN_STREAM_NAMED("grasp_generator", "Generated 0 grasps");
  else
    ROS_INFO_STREAM_NAMED("grasp_generator", "Generated " << grasp_candidates.size() << " grasps");

  // Visualize animated grasps that have been generated
  if (show_prefiltered_grasps_)
  {
    ROS_DEBUG_STREAM_NAMED("grasp_generator", "Animating all generated (candidate) grasps before filtering");
    visualizeAnimatedGrasps(grasp_candidates, grasp_data->ee_jmg_, show_prefiltered_grasps_speed_);
  }

  return true;
}

bool GraspGenerator::generateFingerGrasps(const Eigen::Isometry3d& cuboid_pose, double depth, double width,
                                          double height, const moveit_grasps::GraspDataPtr grasp_data,
                                          std::vector<GraspCandidatePtr>& grasp_candidates,
                                          const GraspCandidateConfig grasp_candidate_config)
{
  // Generate grasps over axes that aren't too wide to grip
  // Most default type of grasp is X axis
  GraspCandidateConfig grasp_candidate_config_copy(grasp_candidate_config);
  if (grasp_candidate_config_copy.generate_x_axis_grasps_)
  {
    ROS_DEBUG_STREAM_NAMED("grasp_generator", "Generating grasps around x-axis of cuboid");
    if (depth > grasp_data->max_grasp_width_)  // depth = size along x-axis
    {
      grasp_candidate_config_copy.disableAllGraspTypes();
      grasp_candidate_config_copy.enable_edge_grasps_ = grasp_candidate_config.enable_edge_grasps_;
      grasp_candidate_config_copy.enable_corner_grasps_ = grasp_candidate_config.enable_corner_grasps_;
    }
    generateCuboidAxisGrasps(cuboid_pose, depth, width, height, X_AXIS, grasp_data, grasp_candidate_config_copy,
                             grasp_candidates);
  }

  grasp_candidate_config_copy = grasp_candidate_config;
  if (grasp_candidate_config_copy.generate_y_axis_grasps_)
  {
    ROS_DEBUG_STREAM_NAMED("grasp_generator", "Generating grasps around y-axis of cuboid");
    if (width > grasp_data->max_grasp_width_)  // width = size along y-axis
    {
      grasp_candidate_config_copy.disableAllGraspTypes();
      grasp_candidate_config_copy.enable_edge_grasps_ = grasp_candidate_config.enable_edge_grasps_;
      grasp_candidate_config_copy.enable_corner_grasps_ = grasp_candidate_config.enable_corner_grasps_;
    }
    generateCuboidAxisGrasps(cuboid_pose, depth, width, height, Y_AXIS, grasp_data, grasp_candidate_config_copy,
                             grasp_candidates);
  }

  grasp_candidate_config_copy = grasp_candidate_config;
  if (grasp_candidate_config_copy.generate_z_axis_grasps_)
  {
    ROS_DEBUG_STREAM_NAMED("grasp_generator", "Generating grasps around z-axis of cuboid");
    if (height > grasp_data->max_grasp_width_)  // height = size along z-axis
    {
      grasp_candidate_config_copy.disableAllGraspTypes();
      grasp_candidate_config_copy.enable_edge_grasps_ = grasp_candidate_config.enable_edge_grasps_;
      grasp_candidate_config_copy.enable_corner_grasps_ = grasp_candidate_config.enable_corner_grasps_;
    }
    generateCuboidAxisGrasps(cuboid_pose, depth, width, height, Z_AXIS, grasp_data, grasp_candidate_config_copy,
                             grasp_candidates);
  }

  if (!grasp_candidates.size())
    ROS_WARN_STREAM_NAMED("grasp_generator", "Generated 0 grasps");
  else
    ROS_INFO_STREAM_NAMED("grasp_generator", "Generated " << grasp_candidates.size() << " grasps");

  // Visualize animated grasps that have been generated
  if (show_prefiltered_grasps_)
  {
    ROS_DEBUG_STREAM_NAMED("grasp_generator", "Animating all generated (candidate) grasps before filtering");
    visualizeAnimatedGrasps(grasp_candidates, grasp_data->ee_jmg_, show_prefiltered_grasps_speed_);
  }

  return true;
}

Eigen::Vector3d GraspGenerator::getPreGraspDirection(const moveit_msgs::Grasp& grasp, const std::string& ee_parent_link)
{
  // Grasp Pose Variables
  Eigen::Isometry3d grasp_pose_eigen;
  tf::poseMsgToEigen(grasp.grasp_pose.pose, grasp_pose_eigen);

  // The direction of the pre-grasp in the frame of the parent link
  Eigen::Vector3d pre_grasp_approach_direction =
      Eigen::Vector3d(grasp.pre_grasp_approach.direction.vector.x, grasp.pre_grasp_approach.direction.vector.y,
                      grasp.pre_grasp_approach.direction.vector.z);

  // Decide if we need to change the approach_direction to the local frame of the end effector orientation
  if (grasp.pre_grasp_approach.direction.header.frame_id == ee_parent_link)
  {
    // Apply/compute the approach_direction vector in the local frame of the grasp_pose orientation
    return grasp_pose_eigen.rotation() * pre_grasp_approach_direction;
  }
  return pre_grasp_approach_direction;
}

geometry_msgs::PoseStamped GraspGenerator::getPreGraspPose(const GraspCandidatePtr& grasp_candidate,
                                                           const std::string& ee_parent_link)
{
  // Grasp Pose Variables
  Eigen::Isometry3d grasp_pose_eigen;
  tf::poseMsgToEigen(grasp_candidate->grasp_.grasp_pose.pose, grasp_pose_eigen);

  // Get pre-grasp pose first
  Eigen::Isometry3d pre_grasp_pose_eigen = grasp_pose_eigen;  // Copy original grasp pose to pre-grasp pose

  // Approach direction
  Eigen::Vector3d pre_grasp_approach_direction_local = getPreGraspDirection(grasp_candidate->grasp_, ee_parent_link);

  // Update the grasp matrix usign the new locally-framed approach_direction
  pre_grasp_pose_eigen.translation() -=
      pre_grasp_approach_direction_local * grasp_candidate->grasp_.pre_grasp_approach.desired_distance;

  // Convert eigen pre-grasp position back to regular message
  geometry_msgs::PoseStamped pre_grasp_pose;
  tf::poseEigenToMsg(pre_grasp_pose_eigen, pre_grasp_pose.pose);

  // Copy original header to new grasp
  pre_grasp_pose.header = grasp_candidate->grasp_.grasp_pose.header;

  return pre_grasp_pose;
}

void GraspGenerator::getGraspWaypoints(const GraspCandidatePtr& grasp_candidate,
                                       EigenSTL::vector_Isometry3d& grasp_waypoints)
{
  Eigen::Isometry3d grasp_pose;
  tf::poseMsgToEigen(grasp_candidate->grasp_.grasp_pose.pose, grasp_pose);

  const geometry_msgs::PoseStamped pregrasp_pose_msg =
      GraspGenerator::getPreGraspPose(grasp_candidate, grasp_candidate->grasp_data_->parent_link_->getName());

  // Create waypoints
  Eigen::Isometry3d pregrasp_pose;
  tf::poseMsgToEigen(pregrasp_pose_msg.pose, pregrasp_pose);

  Eigen::Isometry3d lifted_grasp_pose = grasp_pose;
  lifted_grasp_pose.translation().z() += grasp_candidate->grasp_data_->lift_distance_desired_;

  // Solve for post grasp retreat
  Eigen::Isometry3d retreat_pose = lifted_grasp_pose;
  Eigen::Vector3d postgrasp_vector(grasp_candidate->grasp_.post_grasp_retreat.direction.vector.x,
                                   grasp_candidate->grasp_.post_grasp_retreat.direction.vector.y,
                                   grasp_candidate->grasp_.post_grasp_retreat.direction.vector.z);
  postgrasp_vector.normalize();

  retreat_pose.translation() +=
      retreat_pose.rotation() * postgrasp_vector * grasp_candidate->grasp_.post_grasp_retreat.desired_distance;

  grasp_waypoints.clear();
  grasp_waypoints.resize(4);
  grasp_waypoints[0] = pregrasp_pose;
  grasp_waypoints[1] = grasp_pose;
  grasp_waypoints[2] = lifted_grasp_pose;
  grasp_waypoints[3] = retreat_pose;
}

void GraspGenerator::publishGraspArrow(geometry_msgs::Pose grasp, const GraspDataPtr grasp_data,
                                       const rviz_visual_tools::colors& color, double approach_length)
{
  visual_tools_->publishArrow(grasp, color, rviz_visual_tools::MEDIUM);
}

bool GraspGenerator::visualizeAnimatedGrasps(const std::vector<GraspCandidatePtr>& grasp_candidates,
                                             const moveit::core::JointModelGroup* ee_jmg, double animation_speed)
{
  // Convert the grasp_candidates into a format moveit_visual_tools can use
  std::vector<moveit_msgs::Grasp> grasps;
  for (std::size_t i = 0; i < grasp_candidates.size(); ++i)
  {
    grasps.push_back(grasp_candidates[i]->grasp_);
  }

  return visual_tools_->publishAnimatedGrasps(grasps, ee_jmg, show_prefiltered_grasps_speed_);
}

}  // namespace moveit_grasps
