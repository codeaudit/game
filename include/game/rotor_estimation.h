#ifndef GAME_GAME_ROTOR_ESTIMATION_H_
#define GAME_GAME_ROTOR_ESTIMATION_H_

#include <iostream>
#include <boost/numpy.hpp>
#include <ceres/ceres.h>
#include <ceres/rotation.h>
#include <glog/logging.h>
#include <hep/ga.hpp>

#include "game/ceres_python_utils.h"
#include "game/types.h"

namespace np = boost::numpy;

using ceres::AutoDiffCostFunction;
using ceres::CostFunction;
using ceres::Problem;
using ceres::Solver;
using ceres::Solve;

namespace game {

struct RotorPlus {
  template <typename T>
  bool operator()(const T* x, const T* delta, T* x_plus_delta) const {
    const T squared_norm_delta =
        delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2];
    T q_delta[4];
    if (squared_norm_delta > T(0.0)) {
      T norm_delta = sqrt(squared_norm_delta);
      const T sin_delta_by_delta = sin(norm_delta) / norm_delta;
      q_delta[0] = cos(norm_delta);
      q_delta[1] = sin_delta_by_delta * delta[0];
      q_delta[2] = sin_delta_by_delta * delta[1];
      q_delta[3] = sin_delta_by_delta * delta[2];
    } else {
      // We do not just use q_delta = [1,0,0,0] here because that is a
      // constant and when used for automatic differentiation will
      // lead to a zero derivative. Instead we take a first order
      // approximation and evaluate it at zero.
      q_delta[0] = T(1.0);
      q_delta[1] = delta[0];
      q_delta[2] = delta[1];
      q_delta[3] = delta[2];
    }

    cga::Rotor<T> r =
        cga::Rotor<T>{q_delta[0], -q_delta[1], -q_delta[2], -q_delta[3]} *
        cga::Rotor<T>{x[0], x[1], x[2], x[3]};

    //    std::cout << r[0] << std::endl;
    //    std::cout << r[1] << std::endl;
    //    std::cout << r[2] << std::endl;
    //    std::cout << r[3] << std::endl;

    x_plus_delta[0] = r[0];
    x_plus_delta[1] = r[1];
    x_plus_delta[2] = r[2];
    x_plus_delta[3] = r[3];

//    ceres::QuaternionProduct(q_delta, x, x_plus_delta);

    return true;
  }
};

struct RotorEstimation {
  RotorEstimation(bp::dict options) {
    SetSolverOptions(options);
  }
  RotorEstimation(const RotorEstimation& rotor_estimation) {}
  RotorEstimation() {}


  void SetSolverOptions(const bp::dict &solver_options) {
    bp::extract<std::string> linear_solver_type(
        solver_options["linear_solver_type"]);
    if (linear_solver_type.check()) {
      ceres::StringToLinearSolverType(linear_solver_type(),
                                      &options_.linear_solver_type);
    }

    bp::extract<int> max_num_iterations(solver_options["max_num_iterations"]);
    if (max_num_iterations.check()) {
      options_.max_num_iterations = max_num_iterations();
    }

    bp::extract<int> num_threads(solver_options["num_threads"]);
    if (num_threads.check()) {
      options_.num_threads = num_threads();
    }

    bp::extract<int> num_linear_solver_threads(
        solver_options["num_linear_solver_threads"]);
    if (num_linear_solver_threads.check()) {
      options_.num_linear_solver_threads = num_linear_solver_threads();
    }

    bp::extract<double> parameter_tolerance(
        solver_options["parameter_tolerance"]);
    if (parameter_tolerance.check()) {
      options_.parameter_tolerance = parameter_tolerance();
    }

    bp::extract<double> function_tolerance(
        solver_options["function_tolerance"]);
    if (function_tolerance.check()) {
      options_.function_tolerance = function_tolerance();
    }

    bp::extract<std::string> trust_region_strategy_type(
        solver_options["trust_region_strategy_type"]);
    if (trust_region_strategy_type.check()) {
      ceres::StringToTrustRegionStrategyType(
          trust_region_strategy_type(), &options_.trust_region_strategy_type);
    }

    bp::extract<bool> minimizer_progress_to_stdout(
        solver_options["minimizer_progress_to_stdout"]);
    if (minimizer_progress_to_stdout.check()) {
      options_.minimizer_progress_to_stdout = minimizer_progress_to_stdout();
    }

    bp::extract<std::string> minimizer_type(solver_options["minimizer_type"]);
    if (minimizer_type.check()) {
      ceres::StringToMinimizerType(minimizer_type(), &options_.minimizer_type);
    }

    bp::extract<bp::list> trust_region_minimizer_iterations_to_dump(
        solver_options["trust_region_minimizer_iterations_to_dump"]);
    if (trust_region_minimizer_iterations_to_dump.check()) {
      std::vector<int> iterations_to_dump{};
      bp::list list = trust_region_minimizer_iterations_to_dump();
      for (int i = 0; i < bp::len(list); ++i) {
        iterations_to_dump.push_back(bp::extract<int>(list[i])());
      }
      options_.trust_region_minimizer_iterations_to_dump = iterations_to_dump;
    }

    bp::extract<std::string> trust_region_problem_dump_directory(
        solver_options["trust_region_problem_dump_directory"]);
    if (trust_region_problem_dump_directory.check()) {
      options_.trust_region_problem_dump_directory =
          trust_region_problem_dump_directory();
    }
  }

  template <typename T>
  static void NormalizeRotor(T* array) {
    auto scale =
        static_cast<T>(1.0) / sqrt(array[0] * array[0] + array[1] * array[1] +
                                   array[2] * array[2] + array[3] * array[3]);
    array[0] *= scale;
    array[1] *= scale;
    array[2] *= scale;
    array[3] *= scale;
  }

  struct CostFunctor {
    CostFunctor(const double* a, const double* b) : a_(a), b_(b) {}

    template <typename T>
    bool operator()(const T* const r /* 4 parameters */,
                    T* residual /* 1 parameter */) const {
      cga::EuclideanPoint<T> a{static_cast<T>(a_[0]), static_cast<T>(a_[1]),
                               static_cast<T>(a_[2])};
      cga::EuclideanPoint<T> b{static_cast<T>(b_[0]), static_cast<T>(b_[1]),
                               static_cast<T>(b_[2])};

      cga::Rotor<T> rot{r[0], r[1], r[2], r[3]};
      //      NormalizeRotor(&rot[0]);

      cga::EuclideanPoint<T> rar;
      rar = hep::grade<1>(rot * a * ~rot);

      cga::EuclideanVector<T> dist = rar - b;

      residual[0] = dist[0];
      residual[1] = dist[1];
      residual[2] = dist[2];

      return true;
    }

   private:
    const double* a_;
    const double* b_;
  };

  static ceres::CostFunction* Create(const double* a, const double* b) {
    return (new ceres::AutoDiffCostFunction<RotorEstimation::CostFunctor, 3, 4>(
        new RotorEstimation::CostFunctor(a, b)));
  }

  np::ndarray Run(np::ndarray parameters, np::ndarray a, np::ndarray b) {
    if (!(a.get_flags() & np::ndarray::C_CONTIGUOUS)) {
      throw "input array a must be contiguous";
    }

    if (!(b.get_flags() & np::ndarray::C_CONTIGUOUS)) {
      throw "input array b must be contiguous";
    }

    auto rows_a = a.shape(0);
    auto cols_a = a.shape(1);
    auto rows_b = b.shape(0);
    auto cols_b = b.shape(1);
    auto rows_parameters = parameters.shape(0);
    auto cols_parameters = parameters.shape(1);

    if (!((rows_parameters == 4) && (cols_parameters == 1))) {
      throw "parameter array must have shape (4,1)";
    }

    if (!((rows_a == rows_b) && (cols_a == cols_b))) {
      throw "input array a and b must have the same shape";
    }

    double* parameters_data = reinterpret_cast<double*>(parameters.get_data());
    double* a_data = reinterpret_cast<double*>(a.get_data());
    double* b_data = reinterpret_cast<double*>(b.get_data());

    for (int i = 0; i < rows_a; ++i) {
      ceres::CostFunction* cost_function =
          RotorEstimation::Create(&a_data[cols_a * i], &b_data[cols_b * i]);
      problem_.AddResidualBlock(cost_function, NULL, parameters_data);
    }

    ceres::LocalParameterization* local_parameterization =
        new ceres::AutoDiffLocalParameterization<RotorPlus, 4, 3>;
    problem_.SetParameterization(parameters_data, local_parameterization);

    Solve(options_, &problem_, &summary_);


    return parameters;
  }

  auto Summary() -> bp::dict { return game::SummaryToDict(summary_); }

  Problem problem_;
  Solver::Options options_;
  Solver::Summary summary_;
};

}  // namespace game

#endif  // GAME_GAME_ROTOR_ESTIMATION_H_
