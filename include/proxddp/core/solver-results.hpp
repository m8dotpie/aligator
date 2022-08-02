#pragma once

#include "proxddp/core/traj-opt-problem.hpp"

#include <fmt/core.h>
#include <ostream>

namespace proxddp {

template <typename _Scalar> struct ResultsBaseTpl {
  using Scalar = _Scalar;
  PROXNLP_DYNAMIC_TYPEDEFS(Scalar);

  std::size_t num_iters = 0;
  bool conv = false;

  Scalar traj_cost_ = 0.;
  Scalar merit_value_ = 0.;
  /// Overall primal infeasibility/constraint violation for the
  /// TrajOptProblemTpl.
  Scalar primal_infeasibility = 0.;
  /// Overall dual infeasibility measure for the TrajOptProblemTpl.
  Scalar dual_infeasibility = 0.;

  /// Riccati gains
  std::vector<MatrixXs> gains_;
  /// States
  std::vector<VectorXs> xs_;
  /// Controls
  std::vector<VectorXs> us_;
  /// Problem Lagrange multipliers
  std::vector<VectorXs> lams_;
  /// Dynamics' co-states
  std::vector<VectorRef> co_state_;

  virtual ~ResultsBaseTpl() = default;
};

template <typename Scalar>
std::ostream &operator<<(std::ostream &oss, const ResultsBaseTpl<Scalar> &self);

/// @brief    Results holder struct.
template <typename _Scalar> struct ResultsTpl : ResultsBaseTpl<_Scalar> {
  using Scalar = _Scalar;
  PROXNLP_DYNAMIC_TYPEDEFS(Scalar);
  using Base = ResultsBaseTpl<Scalar>;
  using Base::co_state_;
  using Base::conv;
  using Base::gains_;
  using Base::lams_;
  using Base::num_iters;
  using Base::us_;
  using Base::xs_;

  /// @brief    Create the results struct from a problem (TrajOptProblemTpl)
  /// instance.
  explicit ResultsTpl(const TrajOptProblemTpl<Scalar> &problem);
};

template <typename Scalar>
std::ostream &operator<<(std::ostream &oss,
                         const ResultsBaseTpl<Scalar> &self) {
  oss << "Results {";
  oss << fmt::format("\n  numiters   :  {:d},", self.num_iters);
  oss << fmt::format("\n  converged  :  {},", self.conv);
  oss << fmt::format("\n  traj. cost :  {:.3e},", self.traj_cost_)
      << fmt::format("\n  merit.value:  {:.3e},", self.merit_value_)
      << fmt::format("\n  prim_infeas:  {:.3e},", self.primal_infeasibility)
      << fmt::format("\n  dual_infeas:  {:.3e},", self.dual_infeasibility);
  oss << "\n}";
  return oss;
}

} // namespace proxddp

#include "proxddp/core/solver-results.hxx"
