/*
 * Copyright (c) 2018 Pilz GmbH & Co. KG
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gtest/gtest.h>

#include "pilz_trajectory_generation/trajectory_generator_lin.h"
#include "pilz_trajectory_generation/joint_limits_aggregator.h"
#include "test_utils.h"
#include "pilz_industrial_motion_testutils/xml_testdata_loader.h"
#include "pilz_industrial_motion_testutils/motion_plan_request_director.h"

#include <moveit/robot_model_loader/robot_model_loader.h>
#include <moveit/robot_model/robot_model.h>
#include <moveit/robot_state/robot_state.h>
#include <moveit/kinematic_constraints/utils.h>
#include <moveit/robot_state/conversions.h>
#include <eigen_conversions/eigen_msg.h>

#include <ros/console.h>

const std::string PARAM_MODEL_NO_GRIPPER_NAME {"robot_description"};
const std::string PARAM_MODEL_WITH_GRIPPER_NAME {"robot_description_pg70"};

//parameters from parameter server
const std::string TEST_DATA_FILE_NAME("testdata_file_name");
const std::string PARAM_PLANNING_GROUP_NAME("planning_group");
const std::string TARGET_LINK_HCD("target_link_hand_computed_data");
const std::string RANDOM_TEST_TRIAL_NUM("random_trial_number");
const std::string JOINT_POSITION_TOLERANCE("joint_position_tolerance");
const std::string JOINT_VELOCITY_TOLERANCE("joint_velocity_tolerance");
const std::string POSE_TRANSFORM_MATRIX_NORM_TOLERANCE("pose_norm_tolerance");
const std::string ROTATION_AXIS_NORM_TOLERANCE("rot_axis_norm_tolerance");
const std::string VELOCITY_SCALING_FACTOR("velocity_scaling_factor");
const std::string OTHER_TOLERANCE("other_tolerance");

using namespace pilz;

/**
 * @brief Parameterized unittest of trajectory generator LIN to enable tests against
 * different robot models.The parameter is the name of robot model parameter on the
 * ros parameter server.
 */
class TrajectoryGeneratorLINTest: public testing::TestWithParam<std::string>
{
protected:

  /**
   * @brief Create test scenario for lin trajectory generator
   *
   */
  virtual void SetUp();

  bool checkLinResponse(const planning_interface::MotionPlanRequest& req,
                        const planning_interface::MotionPlanResponse& res);

protected:
  // ros stuff
  ros::NodeHandle ph_ {"~"};
  robot_model::RobotModelConstPtr robot_model_ {
    robot_model_loader::RobotModelLoader(GetParam()).getModel()};

  // lin trajectory generator using model without gripper
  std::unique_ptr<TrajectoryGenerator> lin_;
  // test data provider
  std::unique_ptr<pilz_industrial_motion_testutils::TestdataLoader> tdp_;
  // motion plan request director
  pilz_industrial_motion_testutils::MotionPlanRequestDirector req_director_;

  // test parameters from parameter server
  std::string planning_group_, target_link_hcd_, test_data_file_name_;
  int random_trial_num_;
  double joint_position_tolerance_, joint_velocity_tolerance_, pose_norm_tolerance_,
  rot_axis_norm_tolerance_, velocity_scaling_factor_, other_tolerance_;
  LimitsContainer planner_limits_;

};



void TrajectoryGeneratorLINTest::SetUp()
{
  // get the parameters
  ASSERT_TRUE(ph_.getParam(TEST_DATA_FILE_NAME, test_data_file_name_));
  ASSERT_TRUE(ph_.getParam(PARAM_PLANNING_GROUP_NAME, planning_group_));
  ASSERT_TRUE(ph_.getParam(TARGET_LINK_HCD, target_link_hcd_));
  ASSERT_TRUE(ph_.getParam(RANDOM_TEST_TRIAL_NUM, random_trial_num_));
  ASSERT_TRUE(ph_.getParam(JOINT_POSITION_TOLERANCE, joint_position_tolerance_));
  ASSERT_TRUE(ph_.getParam(JOINT_VELOCITY_TOLERANCE, joint_velocity_tolerance_));
  ASSERT_TRUE(ph_.getParam(POSE_TRANSFORM_MATRIX_NORM_TOLERANCE, pose_norm_tolerance_));
  ASSERT_TRUE(ph_.getParam(ROTATION_AXIS_NORM_TOLERANCE, rot_axis_norm_tolerance_));
  ASSERT_TRUE(ph_.getParam(VELOCITY_SCALING_FACTOR, velocity_scaling_factor_));
  ASSERT_TRUE(ph_.getParam(OTHER_TOLERANCE, other_tolerance_));

  testutils::checkRobotModel(robot_model_, planning_group_, target_link_hcd_);

  // load the test data provider
  tdp_.reset(new pilz_industrial_motion_testutils::XmlTestdataLoader{test_data_file_name_});
  ASSERT_NE(nullptr, tdp_) << "Failed to load test data by provider.";

  tdp_->setRobotModel(robot_model_);

  // create the limits container
  // TODO, move this also into test data set
  pilz::JointLimitsContainer joint_limits =
      pilz::JointLimitsAggregator::getAggregatedLimits(ph_, robot_model_->getActiveJointModels());
  CartesianLimit cart_limits;
  cart_limits.setMaxRotationalVelocity(0.5*M_PI);
  cart_limits.setMaxTranslationalAcceleration(2);
  cart_limits.setMaxTranslationalDeceleration(2);
  cart_limits.setMaxTranslationalVelocity(1);
  planner_limits_.setJointLimits(joint_limits);
  planner_limits_.setCartesianLimits(cart_limits);

  // initialize the LIN trajectory generator
  lin_.reset(new TrajectoryGeneratorLIN(robot_model_, planner_limits_));
  ASSERT_NE(nullptr, lin_) << "Failed to create LIN trajectory generator.";
}

bool TrajectoryGeneratorLINTest::checkLinResponse(const planning_interface::MotionPlanRequest& req,
                                                  const planning_interface::MotionPlanResponse& res)
{
  moveit_msgs::MotionPlanResponse res_msg;
  res.getMessage(res_msg);
  if(!testutils::isGoalReached(robot_model_,
                               res_msg.trajectory.joint_trajectory,
                               req,
                               pose_norm_tolerance_))
  {
    return false;
  }

  if(!testutils::checkCartesianLinearity(robot_model_,
                                         res_msg.trajectory.joint_trajectory,
                                         req,
                                         pose_norm_tolerance_,
                                         rot_axis_norm_tolerance_))
  {
    return false;
  }

  if(!testutils::checkJointTrajectory(res_msg.trajectory.joint_trajectory,
                                      planner_limits_.getJointLimitContainer()))
  {
    return false;
  }

  return true;
}


// Instantiate the test cases for robot model with and without gripper
INSTANTIATE_TEST_CASE_P(InstantiationName, TrajectoryGeneratorLINTest, ::testing::Values(
                          PARAM_MODEL_NO_GRIPPER_NAME,
                          PARAM_MODEL_WITH_GRIPPER_NAME
                          ));

/**
 * @brief test the lin planner with invalid motion plan request which has non zero start velocity
 */
TEST_P(TrajectoryGeneratorLINTest, nonZeroStartVelocity)
{
  planning_interface::MotionPlanRequest req {tdp_->getLinJoint("lin2").toRequest()};

  // add non-zero velocity in the start state
  req.start_state.joint_state.velocity.push_back(1.0);

  // try to generate the result
  planning_interface::MotionPlanResponse res;
  EXPECT_FALSE(lin_->generate(req,res));
  EXPECT_EQ(res.error_code_.val, moveit_msgs::MoveItErrorCodes::INVALID_ROBOT_STATE);
}

/**
 * @brief test the lin planner with joint space goal
 */
TEST_P(TrajectoryGeneratorLINTest, jointSpaceGoal)
{
  planning_interface::MotionPlanRequest lin_joint_req {tdp_->getLinJoint("lin2").toRequest()};

  // generate the LIN trajectory
  planning_interface::MotionPlanResponse res;
  ASSERT_TRUE(lin_->generate(lin_joint_req, res));
  EXPECT_TRUE(res.error_code_.val == moveit_msgs::MoveItErrorCodes::SUCCESS);

  // check the resulted trajectory
  EXPECT_TRUE(checkLinResponse(lin_joint_req, res));
}

/**
 * @brief test the lin planner with joint space goal with start velocity almost zero
 */
TEST_P(TrajectoryGeneratorLINTest, jointSpaceGoalNearZeroStartVelocity)
{
  planning_interface::MotionPlanRequest lin_joint_req {tdp_->getLinJoint("lin2").toRequest()};

  // Set velocity near zero
  lin_joint_req.start_state.joint_state.velocity
    = std::vector<double>(lin_joint_req.start_state.joint_state.position.size(), 1e-16);

  // generate the LIN trajectory
  planning_interface::MotionPlanResponse res;
  ASSERT_TRUE(lin_->generate(lin_joint_req, res));
  EXPECT_TRUE(res.error_code_.val == moveit_msgs::MoveItErrorCodes::SUCCESS);

  // check the resulted trajectory
  EXPECT_TRUE(checkLinResponse(lin_joint_req, res));
}

/**
 * @brief test the lin planner with Cartesian goal
 */
TEST_P(TrajectoryGeneratorLINTest, cartesianSpaceGoal)
{
  // construct motion plan request
  moveit_msgs::MotionPlanRequest lin_cart_req {tdp_->getLinCart("lin2").toRequest()};

  // generate lin trajectory
  planning_interface::MotionPlanResponse res;
  ASSERT_TRUE(lin_->generate(lin_cart_req, res));
  EXPECT_TRUE(res.error_code_.val == moveit_msgs::MoveItErrorCodes::SUCCESS);

  // check the resulted trajectory
  EXPECT_TRUE(checkLinResponse(lin_cart_req, res));
}

/**
 * @brief test the trapezoid shape of the planning trajectory in Cartesian space
 * 
 * The test checks both translational and rotational acceleration for a trapezoid velocity profile.
 * Due to the way the acceleration is calculated 1 or 2 intermediate points occur that are neither 
 * acceleration, constant or deceleration.
 */
TEST_P(TrajectoryGeneratorLINTest, cartesianTrapezoidProfile)
{
  // construct motion plan request
  moveit_msgs::MotionPlanRequest lin_joint_req {tdp_->getLinJoint("lin2").toRequest()};

  /// +++++++++++++++++++++++
  /// + plan LIN trajectory +
  /// +++++++++++++++++++++++
  planning_interface::MotionPlanResponse res;
  ASSERT_TRUE(lin_->generate(lin_joint_req, res, 0.01));
  EXPECT_EQ(res.error_code_.val, moveit_msgs::MoveItErrorCodes::SUCCESS);

  // We require the following such that the test makes sense
  EXPECT_GT(res.trajectory_->getWayPointCount(), 9u);

  /// ++++++++++++++++++++++++++++++
  /// + check the trapzoid profile +
  /// ++++++++++++++++++++++++++++++


  // variables to find the way point at given time
  robot_state::RobotState waypoint_state(robot_model_);

  std::vector<double> accelerations_transl;
  std::vector<double> accelerations_rot;

  // Iterate over waypoints collect accelerations
  for(size_t i = 2; i < res.trajectory_->getWayPointCount(); ++i)
  {
    auto waypoint_pose_0 = res.trajectory_->getWayPoint(i-2).getFrameTransform(target_link_hcd_);
    auto waypoint_pose_1 = res.trajectory_->getWayPoint(i-1).getFrameTransform(target_link_hcd_);
    auto waypoint_pose_2 = res.trajectory_->getWayPoint(i).getFrameTransform(target_link_hcd_);

    auto t1 = res.trajectory_->getWayPointDurationFromPrevious(i-1);
    auto t2 = res.trajectory_->getWayPointDurationFromPrevious(i);

    //***********************
    // Translational part
    //***********************
    auto vel1 = (waypoint_pose_1.translation() - waypoint_pose_0.translation()).norm() / t1;
    auto vel2 = (waypoint_pose_2.translation() - waypoint_pose_1.translation()).norm() / t2;
    auto acc_transl = (vel2 - vel1) / (t1 + t2);
    accelerations_transl.push_back(acc_transl);

    //***********************
    // Rotational part
    //***********************
    Eigen::AngleAxisd axis_wp12((waypoint_pose_0 * waypoint_pose_1.inverse()).rotation());
    Eigen::AngleAxisd axis_wp23((waypoint_pose_1 * waypoint_pose_2.inverse()).rotation());
    // Check that rotation axis stays the same
    ASSERT_LT((axis_wp12.axis() - axis_wp23.axis()).norm(), 1e-8);

    Eigen::Quaterniond orientation1(waypoint_pose_0.rotation());
    Eigen::Quaterniond orientation2(waypoint_pose_1.rotation());
    Eigen::Quaterniond orientation3(waypoint_pose_2.rotation());

    double angular_vel1 = orientation1.angularDistance(orientation2) / t1;
    double angular_vel2 = orientation2.angularDistance(orientation3) / t2;
    double angular_acc = (angular_vel2 - angular_vel1) / (t1+t2);
    accelerations_rot.push_back(angular_acc);
  }

  // Check the accelerations
  ASSERT_TRUE(testutils::hasTrapezoidVelocity(accelerations_transl, 1e-6));
  ASSERT_TRUE(testutils::hasTrapezoidVelocity(accelerations_rot, 1e-6));

  // check last point for vel=acc=0
  for(size_t idx = 0; idx < res.trajectory_->getLastWayPointPtr()->getVariableCount(); ++idx)
  {
    EXPECT_NEAR(0.0, res.trajectory_->getLastWayPointPtr()->getVariableVelocity(idx), other_tolerance_);
    EXPECT_NEAR(0.0, res.trajectory_->getLastWayPointPtr()->getVariableAcceleration(idx), other_tolerance_);
  }

}

/**
 * @brief Check that lin planner returns 'false' if
 * calculated lin trajectory violates velocity/acceleration or deceleration limits.
 *
 *
 * Test Sequence:
 *    1. Call function with lin request violating velocity/acceleration or deceleration limits.
 *
 * Expected Results:
 *    1. Function returns 'false'.
 */
TEST_P(TrajectoryGeneratorLINTest, LinPlannerLimitViolation)
{
  // construct motion plan request
  moveit_msgs::MotionPlanRequest lin_joint_req {tdp_->getLinJoint("lin2").toRequest()};

  // Increase the acceleration
  lin_joint_req.max_acceleration_scaling_factor = 1.0;

  // generate the LIN trajectory
  planning_interface::MotionPlanResponse res;
  ASSERT_FALSE(lin_->generate(lin_joint_req, res));
}

/**
 * @brief test joint linear movement with equal goal and start
 *
 * Test Sequence:
 *    1. Call function with lin request start = goal
 *
 * Expected Results:
 *    1. trajectory generation is successful.
 */
TEST_P(TrajectoryGeneratorLINTest, LinStartEqualsGoal)
{
  // construct motion plan request
  moveit_msgs::MotionPlanRequest lin_joint_req {tdp_->getLinJoint("lin2").toRequest()};

  moveit::core::RobotState start_state(robot_model_);
  jointStateToRobotState(lin_joint_req.start_state.joint_state, start_state);


  for(size_t i = 0; i < lin_joint_req.goal_constraints[0].joint_constraints.size(); i++)
  {
    lin_joint_req.goal_constraints[0].joint_constraints[i].position 
      = start_state.getVariablePosition(lin_joint_req.goal_constraints[0].joint_constraints[i].joint_name);
  }

  // generate the LIN trajectory
  planning_interface::MotionPlanResponse res;
  ASSERT_TRUE(lin_->generate(lin_joint_req, res));
  EXPECT_TRUE(res.error_code_.val == moveit_msgs::MoveItErrorCodes::SUCCESS);

  // check the resulted trajectory
  EXPECT_TRUE(checkLinResponse(lin_joint_req, res));
}

/**
 * @brief Checks that constructor throws an exception if no limits are given.
 *
 * Test Sequence:
 *    1. Call Ctor without set limits.
 *
 * Expected Results:
 *    1. Ctor throws exception.
 */
TEST_P(TrajectoryGeneratorLINTest, CtorNoLimits)
{
  pilz::LimitsContainer planner_limits;

  EXPECT_THROW(pilz::TrajectoryGeneratorLIN(robot_model_, planner_limits),
               pilz::TrajectoryGeneratorInvalidLimitsException);
}

/**
 * @brief Checks that generate() function returns 'false' if called with an
 * incorrect number of joints.
 *
 * Test Sequence:
 *    1. Call functions with incorrect number of joints.
 *
 * Expected Results:
 *    1. Function returns 'false'.
 */
TEST_P(TrajectoryGeneratorLINTest, IncorrectJointNumber)
{
  // construct motion plan request
  moveit_msgs::MotionPlanRequest lin_joint_req {tdp_->getLinJoint("lin2").toRequest()};

  // Ensure that request consists of an incorrect number of joints.
  lin_joint_req.goal_constraints.front().joint_constraints.pop_back();

  // generate the LIN trajectory
  planning_interface::MotionPlanResponse res;
  ASSERT_FALSE(lin_->generate(lin_joint_req, res));
  EXPECT_TRUE(res.error_code_.val == moveit_msgs::MoveItErrorCodes::INVALID_GOAL_CONSTRAINTS);
}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "unittest_trajectory_generator_lin");
  ros::NodeHandle nh;
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
