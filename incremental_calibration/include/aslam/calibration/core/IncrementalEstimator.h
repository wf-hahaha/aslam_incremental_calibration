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

/** \file IncrementalEstimator.h
    \brief This file defines the IncrementalEstimator class, which implements
           an incremental estimator for robotic calibration problems.
  */

#ifndef ASLAM_CALIBRATION_CORE_INCREMENTAL_ESTIMATOR_H
#define ASLAM_CALIBRATION_CORE_INCREMENTAL_ESTIMATOR_H

#include <vector>

#include <boost/shared_ptr.hpp>

#include <Eigen/Core>

namespace sm {

  class PropertyTree;

}
namespace aslam {
  namespace backend {

    class SparseQrLinearSystemSolver;
    class GaussNewtonTrustRegionPolicy;
    class Optimizer2;
    template<typename I> class CompressedColumnMatrix;
    struct SolutionReturnValue;

  }
  namespace calibration {

    class OptimizationProblem;
    class IncrementalOptimizationProblem;

    /** The class IncrementalEstimator implements an incremental estimator
        for robotic calibration problems.
        \brief Incremental estimator
      */
    class IncrementalEstimator {
    public:
      /** \name Types definitions
        @{
        */
      /// Optimization problem type
      typedef OptimizationProblem Batch;
      /// Optimization problem type (shared pointer)
      typedef boost::shared_ptr<OptimizationProblem> BatchSP;
      /// Incremental optimization problem (shared pointer)
      typedef boost::shared_ptr<IncrementalOptimizationProblem>
        IncrementalOptimizationProblemSP;
      /// Self type
      typedef IncrementalEstimator Self;
      /// Solver type
      typedef aslam::backend::SparseQrLinearSystemSolver LinearSolver;
      /// Trust region type
      typedef aslam::backend::GaussNewtonTrustRegionPolicy TrustRegionPolicy;
      /// Optimizer type
      typedef aslam::backend::Optimizer2 Optimizer;
      /// Optimizer type (shared_ptr)
      typedef boost::shared_ptr<Optimizer> OptimizerSP;
      /// Options for the incremental estimator
      struct Options {
        Options() :
            _miTol(0.5),
            _qrTol(0.02),
            _verbose(true),
            _colNorm(true),
            _maxIterations(20),
            _normTol(1e-8),
            _epsTolSVD(1e-4) {
        }
        /// Mutual information threshold
        double _miTol;
        /// QR treshold for rank-deficiency
        double _qrTol;
        /// Verbosity of the optimizer
        bool _verbose;
        /// Perform column normalization
        bool _colNorm;
        /// Maximum number of iterations for the optimizer
        size_t _maxIterations;
        /// Tolerance for zero 2-norm column
        double _normTol;
        /// EPS tolerance for SVD tolerance computation
        double _epsTolSVD;
      };
      /// Return value when adding a batch
      struct ReturnValue {
        /// True if the batch was accepted
        bool _batchAccepted;
        /// MI that the batch contributed
        double _mi;
        /// Rank that this batch lead
        size_t _rank;
        /// Tolerance used for this batch
        double _qrTol;
        /// Number of iterations
        size_t _numIterations;
        /// Cost function at start
        double _JStart;
        /// Cost function at end
        double _JFinal;
        /// Elapsed time for processing this batch [s]
        double _elapsedTime;
        /// Current memory usage in bytes for the linear solver
        size_t _cholmodMemoryUsage;
        /// Null space of the marginalized system
        Eigen::MatrixXd _NS;
        /// Column space of the marginalized system
        Eigen::MatrixXd _CS;
        /// Covariance of the marginalized system
        Eigen::MatrixXd _Sigma;
        /// Projected covariance of the marginalized system
        Eigen::MatrixXd _SigmaP;
        /// Marginalized Fisher information matrix
        Eigen::MatrixXd _Omega;
      };
      /** @}
        */

      /** \name Constructors/destructor
        @{
        */
      /// Constructs estimator with group to marginalize and options
      IncrementalEstimator(size_t groupId, const Options& options = Options());
      /// Constructs estimator with configuration in property tree
      IncrementalEstimator(const sm::PropertyTree& config);
      /// Copy constructor
      IncrementalEstimator(const Self& other) = delete;
      /// Copy assignment operator
      IncrementalEstimator& operator = (const Self& other) = delete;
      /// Move constructor
      IncrementalEstimator(Self&& other) = delete;
      /// Move assignment operator
      IncrementalEstimator& operator = (Self&& other) = delete;
      /// Destructor
      virtual ~IncrementalEstimator();
      /** @}
        */

      /** \name Methods
        @{
        */
      /// Adds a measurement batch to the estimator
      ReturnValue addBatch(const BatchSP& batch, bool force = false);
      /// Removes a measurement batch from the estimator
      void removeBatch(size_t idx);
      /// Removes a measurement batch from the estimator
      void removeBatch(const BatchSP& batch);
      /// The number of batches
      size_t getNumBatches() const;
      /// Re-runs the optimizer
      ReturnValue reoptimize();
      /** @}
        */

      /** \name Accessors
        @{
        */
      /// Returns the incremental optimization problem
      const IncrementalOptimizationProblem* getProblem() const;
      /// Returns the current options
      const Options& getOptions() const;
      /// Returns the current options
      Options& getOptions();
      /// Returns the last computed mutual information
      double getMutualInformation() const;
      /// Return the marginalized group ID
      size_t getMargGroupId() const;
      /// Returns the current Jacobian tranpose if available
      const aslam::backend::CompressedColumnMatrix<ssize_t>&
        getJacobianTranspose() const;
      /// Returns the current estimated numerical rank
      size_t getRank() const;
      /// Returns the current estimated numerical rank deficiency
      size_t getRankDeficiency() const;
      /// Returns the current estimated marginal numerical rank
      size_t getMarginalRank() const;
      /// Returns the current estimated marginal numerical rank deficiency
      size_t getMarginalRankDeficiency() const;
      /// Returns the current tolerance for the QR decomposition
      double getQRTol() const;
      /// Returns the current memory usage for the linear solver
      size_t getCholmodMemoryUsage() const;
      /// Returns the current marginalized null space
      const Eigen::MatrixXd& getMarginalizedNullSpace() const;
      /// Returns the current marginalized column space
      const Eigen::MatrixXd& getMarginalizedColumnSpace() const;
      /// Returns the current marginalized covariance
      const Eigen::MatrixXd& getMarginalizedCovariance() const;
      /// Returns the current projected marginalized covariance
      const Eigen::MatrixXd& getProjectedMarginalizedCovariance() const;
      /// Returns the current marginalized Fisher information matrix
      const Eigen::MatrixXd& getMarginalizedInformationMatrix() const;
      /** @}
        */

    protected:
      /** \name Protected methods
        @{
        */
      /// Runs an optimization with current setup
      aslam::backend::SolutionReturnValue optimize();
      /// Ensures the marginalized variables are well located
      void orderMarginalizedDesignVariables();
      /// Inits the optimizer
      void initOptimizer();
      /// Restores the linear solver
      void restoreLinearSolver();
      /** @}
        */

      /** \name Protected members
        @{
        */
      /// Underlying optimization problem
      IncrementalOptimizationProblemSP _problem;
      /// Group ID to marginalize
      size_t _margGroupId;
      /// Mutual information
      double _mi;
      /// Sum of the log of the singular values up to the numerical rank
      double _svLogSum;
      /// Options
      Options _options;
      /// Underlying optimizer
      OptimizerSP _optimizer;
      /// Null space of the marginalized system
      Eigen::MatrixXd _NS;
      /// Column space of the marginalized system
      Eigen::MatrixXd _CS;
      /// Covariance of the marginalized system
      Eigen::MatrixXd _Sigma;
      /// Projected covariance of the marginalized system
      Eigen::MatrixXd _SigmaP;
      /// Marginalized Fisher information matrix
      Eigen::MatrixXd _Omega;
      /// Current estimated numerical rank
      size_t _nRank;
      /// Current QR tolerance
      double _qrTol;
      /** @}
        */

    };

  }
}

#endif // ASLAM_CALIBRATION_CORE_INCREMENTAL_ESTIMATOR_H
