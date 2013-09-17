/******************************************************************************
 * Copyright (C) 2013 by Jerome Maye                                          *
 * jerome.maye@gmail.com                                                      *
 *                                                                            *
 * This program is free software; you can redistribute it and/or modify       *
 * it under the terms of the Lesser GNU General Public License as published by*
 * the Free Software Foundation; either version 3 of the License, or          *
 * (at your option) any later version.                                        *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * Lesser GNU General Public License for more details.                        *
 *                                                                            *
 * You should have received a copy of the Lesser GNU General Public License   *
 * along with this program. If not, see <http://www.gnu.org/licenses/>.       *
 ******************************************************************************/

/** \file optimizeNewSpline.cpp
    \brief This file optimizes the splines from a ROS bag file.
  */

#include <iostream>
#include <vector>
#include <string>

#include <Eigen/Core>

#include <boost/make_shared.hpp>

#include <rosbag/bag.h>
#include <rosbag/view.h>
#include <rosbag/message_instance.h>

#include <sm/kinematics/EulerAnglesYawPitchRoll.hpp>
#include <sm/kinematics/rotations.hpp>
#include <sm/kinematics/quaternion_algebra.hpp>

#include <sm/timing/TimestampCorrector.hpp>
#include <sm/timing/NsecTimeUtilities.hpp>

#include <aslam/backend/EuclideanPoint.hpp>
#include <aslam/backend/RotationQuaternion.hpp>
#include <aslam/backend/OptimizationProblem.hpp>
#include <aslam/backend/Optimizer2Options.hpp>
#include <aslam/backend/SparseQrLinearSystemSolver.hpp>
#include <aslam/backend/SparseQRLinearSolverOptions.h>
#include <aslam/backend/Optimizer2.hpp>
#include <aslam/backend/EuclideanExpression.hpp>
#include <aslam/backend/RotationExpression.hpp>
#include <aslam/backend/Vector2RotationQuaternionExpressionAdapter.hpp>
#include <aslam/backend/GaussNewtonTrustRegionPolicy.hpp>

#include <aslam/splines/OPTBSpline.hpp>
#include <aslam/splines/OPTUnitQuaternionBSpline.hpp>

#include <bsplines/BSplineFitter.hpp>
#include <bsplines/EuclideanBSpline.hpp>
#include <bsplines/UnitQuaternionBSpline.hpp>
#include <bsplines/SimpleTypeTimePolicy.hpp>

#include <poslv/VehicleNavigationSolutionMsg.h>
#include <poslv/VehicleNavigationPerformanceMsg.h>

#include <libposlv/geo-tools/Geo.h>

#include "aslam/calibration/car/ErrorTermPose.h"
#include "aslam/calibration/car/MeasurementsContainer.h"
#include "aslam/calibration/car/ApplanixNavigationMeasurement.h"
#include "aslam/calibration/car/utils.h"

using namespace aslam::calibration;
using namespace aslam::splines;
using namespace aslam::backend;
using namespace sm::kinematics;
using namespace sm::timing;
using namespace bsplines;

struct NsecTimePolicy :
  public SimpleTypeTimePolicy<NsecTime> {
  inline static NsecTime getOne() {
    return NsecTime(1e9);
  }
};

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <ros_bag_file>" << std::endl;
    return -1;
  }
  rosbag::Bag bag(argv[1]);
  std::vector<std::string> topics;
  topics.push_back(std::string("/poslv/vehicle_navigation_solution"));
  topics.push_back(std::string("/poslv/vehicle_navigation_performance"));
  rosbag::View view(bag, rosbag::TopicQuery(topics));
  std::cout << "Processing BAG file..." << std::endl;
  poslv::VehicleNavigationPerformanceMsgConstPtr lastVnp;
  bool firstVNS = true;
  double latRef = 0;
  double longRef = 0;
  double altRef = 0;
  size_t viewCounter = 0;
  MeasurementsContainer<ApplanixNavigationMeasurement>::Type measurements;
  TimestampCorrector<double> timestampCorrector;
  for (auto it = view.begin(); it != view.end(); ++it) {
    std::cout << std::fixed << std::setw(3)
      << viewCounter++ / (double)view.size() * 100 << " %" << '\r';
    if (it->isType<poslv::VehicleNavigationPerformanceMsg>()) {
      poslv::VehicleNavigationPerformanceMsgConstPtr vnp(
        it->instantiate<poslv::VehicleNavigationPerformanceMsg>());
      lastVnp = vnp;
    }
    if (it->isType<poslv::VehicleNavigationSolutionMsg>()) {
      if (!lastVnp)
        continue;
      poslv::VehicleNavigationSolutionMsgConstPtr vns(
        it->instantiate<poslv::VehicleNavigationSolutionMsg>());
      if (firstVNS) {
        latRef = vns->latitude;
        longRef = vns->longitude;
        altRef = vns->altitude;
        firstVNS = false;
      }
      double x_ecef, y_ecef, z_ecef;
      Geo::wgs84ToEcef(vns->latitude, vns->longitude, vns->altitude, x_ecef,
        y_ecef, z_ecef);
      double x_enu, y_enu, z_enu;
      Geo::ecefToEnu(x_ecef, y_ecef, z_ecef, latRef, longRef, altRef, x_enu,
        y_enu, z_enu);
      ApplanixNavigationMeasurement data;
      data.x = x_enu;
      data.y = y_enu;
      data.z = z_enu;
      data.yaw = angleMod(deg2rad(-vns->heading) + M_PI / 2);
      data.pitch = deg2rad(-vns->pitch);
      data.roll = deg2rad(vns->roll);
      Eigen::Vector3d linearVelocity =
        Geo::R_ENU_NED::getInstance().getMatrix() * Eigen::Vector3d(
        vns->northVelocity, vns->eastVelocity, vns->downVelocity);
      data.v_x = linearVelocity(0);
      data.v_y = linearVelocity(1);
      data.v_z = linearVelocity(2);
      data.om_x = deg2rad(vns->angularRateLong);
      data.om_y = -deg2rad(vns->angularRateTrans);
      data.om_z = -deg2rad(vns->angularRateDown);
      data.a_x = vns->accLong;
      data.a_y = -vns->accTrans;
      data.a_z = -vns->accDown;
      data.v = vns->speed;
      data.x_sigma2 = lastVnp->eastPositionRMSError *
        lastVnp->eastPositionRMSError;
      data.y_sigma2 = lastVnp->northPositionRMSError *
        lastVnp->northPositionRMSError;
      data.z_sigma2 = lastVnp->downPositionRMSError *
        lastVnp->downPositionRMSError;
      data.roll_sigma2 = deg2rad(lastVnp->rollRMSError) *
        deg2rad(lastVnp->rollRMSError);
      data.pitch_sigma2 = deg2rad(lastVnp->pitchRMSError) *
        deg2rad(lastVnp->pitchRMSError);
      data.yaw_sigma2 = deg2rad(lastVnp->headingRMSError) *
        deg2rad(lastVnp->headingRMSError);
      data.v_x_sigma2 = lastVnp->eastVelocityRMSError *
        lastVnp->eastVelocityRMSError;
      data.v_y_sigma2 = lastVnp->northVelocityRMSError *
        lastVnp->northVelocityRMSError;
      data.v_z_sigma2 = lastVnp->downVelocityRMSError *
        lastVnp->downVelocityRMSError;
      measurements.push_back(
        std::make_pair(round(timestampCorrector.correctTimestamp(
        secToNsec(vns->timeDistance.time1), vns->header.stamp.toNSec())),
        data));
    }
  }
  const size_t numMeasurements = measurements.size();
  std::vector<NsecTime> timestamps;
  timestamps.reserve(numMeasurements);
  std::vector<Eigen::Vector3d> transPoses;
  transPoses.reserve(numMeasurements);
  std::vector<Eigen::Vector4d> rotPoses;
  rotPoses.reserve(numMeasurements);
  const EulerAnglesYawPitchRoll ypr;
  for (auto it = measurements.cbegin(); it != measurements.cend(); ++it) {
    Eigen::Vector4d quat = r2quat(
      ypr.parametersToRotationMatrix(Eigen::Vector3d(it->second.yaw,
      it->second.pitch, it->second.roll)));
    if (!rotPoses.empty()) {
      const Eigen::Vector4d lastRotPose = rotPoses.back();
      quat = bestQuat(lastRotPose, quat);
    }
    timestamps.push_back(it->first);
    rotPoses.push_back(quat);
    transPoses.push_back(Eigen::Vector3d(it->second.x, it->second.y,
      it->second.z));
  }
  const double elapsedTime = (timestamps[numMeasurements - 1] - timestamps[0]) /
    (double)NsecTimePolicy::getOne();
  const int measPerSec = std::round(numMeasurements / elapsedTime);
  int numSegments;
  const double lambda = 0;
  const int measPerSecDesired = 5;
  if (measPerSec > measPerSecDesired)
    numSegments = std::ceil(measPerSecDesired * elapsedTime);
  else
    numSegments = numMeasurements;
  const int transSplineOrder = 4;
  const int rotSplineOrder = 4;
  OPTBSpline<EuclideanBSpline<Eigen::Dynamic, 3, NsecTimePolicy>::CONF>::BSpline
    translationSpline(EuclideanBSpline<Eigen::Dynamic, 3, NsecTimePolicy>::CONF(
    EuclideanBSpline<Eigen::Dynamic, 3, NsecTimePolicy>::CONF::ManifoldConf(3),
    transSplineOrder));
  BSplineFitter<OPTBSpline<EuclideanBSpline<Eigen::Dynamic, 3, NsecTimePolicy>::
    CONF>::BSpline>::initUniformSpline(translationSpline, timestamps,
    transPoses, numSegments, lambda);
  OPTBSpline<UnitQuaternionBSpline<Eigen::Dynamic, NsecTimePolicy>::CONF>::
    BSpline rotationSpline(UnitQuaternionBSpline<Eigen::Dynamic, NsecTimePolicy>
    ::CONF(UnitQuaternionBSpline<Eigen::Dynamic, NsecTimePolicy>::CONF::
    ManifoldConf(), rotSplineOrder));
  BSplineFitter<OPTBSpline<UnitQuaternionBSpline<Eigen::Dynamic, NsecTimePolicy>
    ::CONF>::BSpline>::initUniformSpline(rotationSpline, timestamps, rotPoses,
    numSegments, lambda);
  std::cout << "Outputting spline data before optimization..." << std::endl;
  std::ofstream applanixSplineFile("applanix-spline.txt");
  for (auto it = timestamps.cbegin(); it != timestamps.cend(); ++it) {
    auto translationExpressionFactory =
      translationSpline.getExpressionFactoryAt<2>(*it);
    auto rotationExpressionFactory =
      rotationSpline.getExpressionFactoryAt<1>(*it);
    Eigen::Matrix3d C_wi = Vector2RotationQuaternionExpressionAdapter::adapt(
      rotationExpressionFactory.getValueExpression()).toRotationMatrix();
    applanixSplineFile << std::fixed << std::setprecision(18)
      << *it << " "
      << translationExpressionFactory.getValueExpression().toValue().
        transpose() << " "
      << ypr.rotationMatrixToParameters(C_wi).transpose() << " "
      << translationExpressionFactory.getValueExpression(1).toValue().
        transpose() << " "
      << -(C_wi.transpose() *
        rotationExpressionFactory.getAngularVelocityExpression().toValue()).
        transpose() << " "
      << (C_wi.transpose() *
        translationExpressionFactory.getValueExpression(2).toValue()).
        transpose()
      << std::endl;
  }
  auto problem = boost::make_shared<OptimizationProblem>();
  for (size_t i = 0; i < translationSpline.numDesignVariables(); ++i) {
    translationSpline.designVariable(i)->setActive(true);
    problem->addDesignVariable(translationSpline.designVariable(i), false);
  }
  for (size_t i = 0; i < rotationSpline.numDesignVariables(); ++i) {
    rotationSpline.designVariable(i)->setActive(true);
    problem->addDesignVariable(rotationSpline.designVariable(i), false);
  }
  for (auto it = measurements.cbegin(); it != measurements.cend(); ++it) {
    ErrorTermPose::Input xm;
    xm.head<3>() = Eigen::Vector3d(it->second.x, it->second.y,
      it->second.z);
    xm.tail<3>() = Eigen::Vector3d(it->second.yaw, it->second.pitch,
      it->second.roll);
    ErrorTermPose::Covariance Q = ErrorTermPose::Covariance::Zero();
    Q(0, 0) = it->second.x_sigma2;
    Q(1, 1) = it->second.y_sigma2;
    Q(2, 2) = it->second.z_sigma2;
    Q(3, 3) = it->second.yaw_sigma2;
    Q(4, 4) = it->second.pitch_sigma2;
    Q(5, 5) = it->second.roll_sigma2;
    auto translationExpressionFactory =
      translationSpline.getExpressionFactoryAt<0>(it->first);
    auto rotationExpressionFactory =
      rotationSpline.getExpressionFactoryAt<0>(it->first);
    auto e_pose = boost::make_shared<ErrorTermPose>(
      TransformationExpression(
      Vector2RotationQuaternionExpressionAdapter::adapt(
      rotationExpressionFactory.getValueExpression()),
      translationExpressionFactory.getValueExpression()),
      xm, Q);
    problem->addErrorTerm(e_pose);
  }
  Optimizer2Options options;
  options.verbose = true;
  options.linearSystemSolver = boost::make_shared<SparseQrLinearSystemSolver>();
  options.trustRegionPolicy =
    boost::make_shared<GaussNewtonTrustRegionPolicy>();
  SparseQRLinearSolverOptions linearSolverOptions;
  linearSolverOptions.colNorm = true;
  Optimizer2 optimizer(options);
  optimizer.getSolver<SparseQrLinearSystemSolver>()->setOptions(
    linearSolverOptions);
  optimizer.setProblem(problem);
  optimizer.optimize();
  std::cout << "Outputting spline data after optimization..." << std::endl;
  std::ofstream applanixSplineOptFile("applanix-spline-opt.txt");
  for (auto it = timestamps.cbegin(); it != timestamps.cend(); ++it) {
    auto translationExpressionFactory =
      translationSpline.getExpressionFactoryAt<2>(*it);
    auto rotationExpressionFactory =
      rotationSpline.getExpressionFactoryAt<1>(*it);
    Eigen::Matrix3d C_wi = Vector2RotationQuaternionExpressionAdapter::adapt(
      rotationExpressionFactory.getValueExpression()).toRotationMatrix();
    applanixSplineOptFile << std::fixed << std::setprecision(18)
      << *it << " "
      << translationExpressionFactory.getValueExpression().toValue().
        transpose() << " "
      << ypr.rotationMatrixToParameters(C_wi).transpose() << " "
      << translationExpressionFactory.getValueExpression(1).toValue().
        transpose() << " "
      << -(C_wi.transpose() *
        rotationExpressionFactory.getAngularVelocityExpression().toValue()).
        transpose() << " "
      << (C_wi.transpose() *
        translationExpressionFactory.getValueExpression(2).toValue()).
        transpose()
      << std::endl;
  }
  return 0;
}