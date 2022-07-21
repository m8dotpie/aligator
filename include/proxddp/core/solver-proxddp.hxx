/// @file solver-proxddp.hxx
/// @brief  Implementations for the trajectory optimization algorithm.
/// @copyright Copyright (C) 2022 LAAS-CNRS, INRIA
#pragma once

#include "proxddp/core/solver-proxddp.hpp"
#include "proxddp/utils/exceptions.hpp"

#include <Eigen/Cholesky>

namespace proxddp {
template <typename Scalar>
void SolverProxDDP<Scalar>::computeDirection(const Problem &problem,
                                             Workspace &workspace,
                                             const Results &results) const {
  const std::size_t nsteps = problem.numSteps();

  // compute direction dx0
  {
    const value_store_t &vp = workspace.value_params[0];
    const StageModel &stage0 = *problem.stages_[0];
    const FunctionData &init_data = *workspace.problem_data.init_data;
    const int ndual0 = problem.init_state_error.nr;
    const int ndx0 = stage0.ndx1();
    const VectorXs &lamin0 = results.lams_[0];
    const VectorXs &prevlam0 = workspace.prev_lams_[0];
    const CostData &proxdata0 = workspace.prox_datas[0];
    BlockXs kkt_mat = workspace.getKktView(ndx0, ndual0);
    Eigen::Block<BlockXs, -1, 1, true> kkt_rhs_0 =
        workspace.getKktRhs(ndx0, ndual0, 1).col(0);
    kkt_mat.setZero();
    kkt_rhs_0.setZero();
    kkt_mat.topLeftCorner(ndx0, ndx0) = vp.Vxx_ + rho_ * proxdata0.Lxx_;
    kkt_mat.bottomLeftCorner(ndual0, ndx0) = init_data.Jx_;
    kkt_mat.bottomRightCorner(ndual0, ndual0).diagonal().array() = -mu_;
    workspace.lams_plus_[0] = prevlam0 + mu_inverse_ * init_data.value_;
    workspace.lams_pdal_[0] = 2 * workspace.lams_plus_[0] - lamin0;
    kkt_rhs_0.head(ndx0) =
        vp.Vx_ + init_data.Jx_ * lamin0 + rho_ * proxdata0.Lx_;
    kkt_rhs_0.tail(ndual0) = mu_ * (workspace.lams_plus_[0] - lamin0);

    auto kkt_sym = kkt_mat.template selfadjointView<Eigen::Lower>();
    auto ldlt = kkt_sym.ldlt();
    workspace.pd_step_[0] = ldlt.solve(-kkt_rhs_0);
    workspace.inner_criterion_by_stage(0) = math::infty_norm(kkt_rhs_0);
    workspace.dual_infeas_by_stage(0) = math::infty_norm(kkt_rhs_0.head(ndx0));
  }

  for (std::size_t i = 0; i < nsteps; i++) {
    const StageModel &stage = *problem.stages_[i];
    VectorXs &pd_step = workspace.pd_step_[i + 1];
    Eigen::Block<const MatrixXs, -1, 1, true> feedforward =
        results.gains_[i].col(0);
    Eigen::Block<const MatrixXs, -1, -1, true> feedback =
        results.gains_[i].rightCols(stage.ndx1());

    pd_step = feedforward + feedback * workspace.dxs_[i];
  }
  if (problem.term_constraint_) {
    const MatrixXs &Gterm = results.gains_[nsteps];
    const int ndx = (*problem.term_constraint_).func_->ndx1;
    workspace.dlams_[nsteps + 1] =
        Gterm.col(0) + Gterm.rightCols(ndx) * workspace.dxs_[nsteps];
  }
}

template <typename Scalar>
void SolverProxDDP<Scalar>::tryStep(const Problem &problem,
                                    Workspace &workspace,
                                    const Results &results,
                                    const Scalar alpha) const {

  const std::size_t nsteps = problem.numSteps();

  for (std::size_t i = 0; i <= nsteps; i++)
    workspace.trial_lams_[i] = results.lams_[i] + alpha * workspace.dlams_[i];

  for (std::size_t i = 0; i < nsteps; i++) {
    const StageModel &stage = *problem.stages_[i];
    stage.xspace_->integrate(results.xs_[i], alpha * workspace.dxs_[i],
                             workspace.trial_xs_[i]);
    stage.uspace_->integrate(results.us_[i], alpha * workspace.dus_[i],
                             workspace.trial_us_[i]);
  }
  const StageModel &stage = *problem.stages_[nsteps - 1];
  stage.xspace_next_->integrate(results.xs_[nsteps],
                                alpha * workspace.dxs_[nsteps],
                                workspace.trial_xs_[nsteps]);
}

template <typename Scalar>
void SolverProxDDP<Scalar>::backwardPass(const Problem &problem,
                                         Workspace &workspace,
                                         Results &results) const {
  /* Terminal node */
  computeTerminalValue(problem, workspace, results);

  const std::size_t nsteps = problem.numSteps();
  for (std::size_t i = 0; i < nsteps; i++) {
    computeGains(problem, workspace, results, nsteps - i - 1);
  }
  workspace.inner_criterion =
      math::infty_norm(workspace.inner_criterion_by_stage);
  results.dual_infeasibility = math::infty_norm(workspace.dual_infeas_by_stage);
}

template <typename Scalar>
void SolverProxDDP<Scalar>::computeTerminalValue(const Problem &problem,
                                                 Workspace &workspace,
                                                 Results &results) const {
  const std::size_t nsteps = problem.numSteps();

  const TrajOptDataTpl<Scalar> &prob_data = workspace.problem_data;
  const CostData &term_cost_data = *prob_data.term_cost_data;
  value_store_t &term_value = workspace.value_params[nsteps];
  const CostData &proxdata = workspace.prox_datas[nsteps];

  term_value.v_2() = 2 * (term_cost_data.value_ + rho_ * proxdata.value_);
  term_value.Vx_ = term_cost_data.Lx_ + rho_ * proxdata.Lx_;
  term_value.Vxx_ = term_cost_data.Lxx_ + rho_ * proxdata.Lxx_;

  if (problem.term_constraint_) {
    /* check number of multipliers */
    assert(results.lams_.size() == (nsteps + 2));
    assert(results.gains_.size() == (nsteps + 1));
    const Constraint &term_cstr = *problem.term_constraint_;
    const ConstraintSetBase<Scalar> &cstr_set = *term_cstr.set_;
    const FunctionData &cstr_data = *prob_data.term_cstr_data;

    const int ndx = term_cstr.func_->ndx1;
    MatrixXs &G = results.gains_[nsteps];
    VectorXs &lamplus = workspace.lams_plus_[nsteps + 1];
    VectorXs &lamprev = workspace.prev_lams_[nsteps + 1];
    VectorXs &lamin = results.lams_[nsteps + 1];

    const VectorXs &cv = cstr_data.value_;
    const auto &cJx = cstr_data.Jx_;

    auto l_expr = lamprev + mu_inverse_ * cv;
    cstr_set.applyNormalConeProjectionJacobian(l_expr, cJx);
    cstr_set.normalConeProjection(l_expr, lamplus);

    auto ff = G.col(0);
    auto fb = G.rightCols(ndx);
    /* feedforward */
    ff = lamplus - lamin;
    /* feedback */
    fb = mu_inverse_ * cJx;

    auto Hx = term_value.Vx_ + cJx.transpose() * lamin;
    auto Hxx = term_value.Vxx_ + cstr_data.Hxx_;

    term_value.Vx_ = Hx + cJx.transpose() * ff;
    term_value.Vxx_ = Hxx + cJx.transpose() * fb;
  }

  term_value.storage =
      term_value.storage.template selfadjointView<Eigen::Lower>();
}

template <typename Scalar>
void SolverProxDDP<Scalar>::computeGains(const Problem &problem,
                                         Workspace &workspace, Results &results,
                                         const std::size_t step) const {
  const StageModel &stage = *problem.stages_[step];

  const value_store_t &vnext = workspace.value_params[step + 1];
  q_store_t &qparam = workspace.q_params[step];

  StageData &stage_data = workspace.problem_data.getData(step);
  const CostData &cdata = *stage_data.cost_data;
  const CostData &proxdata = workspace.prox_datas[step];

  const int nprim = stage.numPrimal();
  const int ndual = stage.numDual();
  const int ndx1 = stage.ndx1();
  const int nu = stage.nu();
  const int ndx2 = stage.ndx2();

  assert(vnext.storage.rows() == ndx2 + 1);
  assert(vnext.storage.cols() == ndx2 + 1);

  // Use the contiguous full gradient/jacobian/hessian buffers
  // to fill in the Q-function derivatives
  qparam.storage.setZero();

  qparam.q_2() = 2 * cdata.value_;
  qparam.grad_.head(ndx1 + nu) = cdata.grad_ + rho_ * proxdata.grad_;
  qparam.grad_.tail(ndx2) = vnext.Vx_;
  qparam.hess_.topLeftCorner(ndx1 + nu, ndx1 + nu) =
      cdata.hess_ + rho_ * proxdata.hess_;
  qparam.hess_.bottomRightCorner(ndx2, ndx2) = vnext.Vxx_;

  // self-adjoint view to (nprim + ndual) sized block of kkt buffer
  BlockXs kkt_mat = workspace.getKktView(nprim, ndual);
  BlockXs kkt_rhs = workspace.getKktRhs(nprim, ndual, ndx1);
  Eigen::Block<BlockXs, -1, -1> kkt_jac = kkt_mat.block(nprim, 0, ndual, nprim);

  auto kkt_rhs_0 = kkt_rhs.col(0);
  auto kkt_rhs_D = kkt_rhs.rightCols(ndx1);

  const VectorXs &lam_inn = results.lams_[step + 1];
  const VectorXs &lamprev = workspace.prev_lams_[step + 1];
  VectorXs &lamplus = workspace.lams_plus_[step + 1];
  VectorXs &lampdal = workspace.lams_pdal_[step + 1];

  const ConstraintContainer<Scalar> &cstr_mgr = stage.constraints_;

  // Loop over constraints
  for (std::size_t j = 0; j < stage.numConstraints(); j++) {
    FunctionData &cstr_data = *stage_data.constraint_data[j];

    // Grab Lagrange multiplier segments

    const auto lam_inn_j = cstr_mgr.getConstSegmentByConstraint(lam_inn, j);
    const auto lamprev_j = cstr_mgr.getConstSegmentByConstraint(lamprev, j);
    auto lamplus_j = cstr_mgr.getSegmentByConstraint(lamplus, j);
    auto lampdal_j = cstr_mgr.getSegmentByConstraint(lampdal, j);

    // compose Jacobian by projector and project multiplier
    const ConstraintSetBase<Scalar> &cstr_set = cstr_mgr.getConstraintSet(j);
    auto lam_expr = lamprev_j + mu_inverse_ * cstr_data.value_;
    cstr_set.applyNormalConeProjectionJacobian(lam_expr, cstr_data.jac_buffer_);
    cstr_set.normalConeProjection(lam_expr, lamplus_j);
    lampdal_j = 2 * lamplus_j - lam_inn_j;

    qparam.grad_ += cstr_data.jac_buffer_.transpose() * lam_inn_j;
    qparam.hess_ += cstr_data.vhp_buffer_;

    // update the KKT jacobian columns
    cstr_mgr.getBlockByConstraint(kkt_jac, j) =
        cstr_data.jac_buffer_.rightCols(nprim);
    cstr_mgr.getBlockByConstraint(kkt_rhs_D.bottomRows(ndual), j) =
        cstr_data.jac_buffer_.leftCols(ndx1);
  }

  qparam.storage = qparam.storage.template selfadjointView<Eigen::Lower>();

  // blocks: u, y, and dual
  kkt_rhs_0.head(nprim) = qparam.grad_.tail(nprim);
  kkt_rhs_0.tail(ndual) = mu_ * (lamplus - lam_inn);

  kkt_rhs_D.topRows(nu) = qparam.Qxu_.transpose();
  kkt_rhs_D.middleRows(nu, ndx2) = qparam.Qxy_.transpose();

  // KKT matrix: (u, y)-block = bottom right of q hessian
  kkt_mat.topLeftCorner(nprim, nprim) =
      qparam.hess_.bottomRightCorner(nprim, nprim);
  kkt_mat.topLeftCorner(nprim, nprim).diagonal().array() += xreg_;
  kkt_mat.bottomRightCorner(ndual, ndual).diagonal().array() = -mu_;

  {
    const CostData &proxnext = workspace.prox_datas[step + 1];
    auto grad_u = kkt_rhs_0.head(nu);
    auto grad_y = kkt_rhs_0.segment(nu, ndx2);
    Scalar dual_res_u = math::infty_norm(grad_u - rho_ * proxdata.Lu_);
    Scalar dual_res_y = math::infty_norm(grad_y - rho_ * proxnext.Lx_);
    workspace.inner_criterion_by_stage(long(step + 1)) =
        math::infty_norm(kkt_rhs_0);
    workspace.dual_infeas_by_stage(long(step + 1)) =
        std::max(dual_res_u, dual_res_y);
  }

  /* Compute gains with LDLT */
  auto kkt_mat_view = kkt_mat.template selfadjointView<Eigen::Lower>();
  auto ldlt_ = kkt_mat_view.ldlt();
  MatrixXs &G = results.gains_[step];
  G = -kkt_rhs;
  ldlt_.solveInPlace(G);

  /* Value function */
  value_store_t &vcurr = workspace.value_params[step];
  vcurr.storage = qparam.storage.topLeftCorner(ndx1 + 1, ndx1 + 1) +
                  kkt_rhs.transpose() * G;
}

template <typename Scalar>
bool SolverProxDDP<Scalar>::run(const Problem &problem,
                                const std::vector<VectorXs> &xs_init,
                                const std::vector<VectorXs> &us_init) {
  if (workspace_ == 0 || results_ == 0) {
    proxddp_runtime_error("workspace and results were not allocated yet!");
  }
  Workspace &workspace = *workspace_;
  Results &results = *results_;

  const std::size_t nsteps = problem.numSteps();
  if (xs_init.size() == 0) {
    for (std::size_t i = 0; i < nsteps + 1; i++) {
      const StageModel &sm = *problem.stages_[i];
      results.xs_[i] = sm.xspace().neutral();
    }
  } else {
    if (xs_init.size() != (nsteps + 1)) {
      proxddp_runtime_error("warm-start for xs has wrong size!")
    }
    results.xs_ = xs_init;
  }

  if (us_init.size() == 0) {
    for (std::size_t i = 0; i < nsteps; i++) {
      const StageModel &sm = *problem.stages_[i];
      results.us_[i] = sm.uspace().neutral();
    }
  } else {
    if (us_init.size() != nsteps) {
      proxddp_runtime_error("warm-start for us has wrong size!")
    }
    results.us_ = us_init;
  }

  workspace.prev_xs_ = results.xs_;
  workspace.prev_us_ = results.us_;
  workspace.prev_lams_ = results.lams_;

  inner_tol_ = inner_tol0;
  prim_tol_ = prim_tol0;
  updateTolerancesOnFailure();

  inner_tol_ = std::max(inner_tol_, target_tolerance);
  prim_tol_ = std::max(prim_tol_, target_tolerance);

  bool &conv = results.conv;

  std::size_t al_iter = 0;
  while ((al_iter < MAX_AL_ITERS) && (results.num_iters < MAX_ITERS)) {
    if (verbose_ >= 1) {
      auto colout = fmt::fg(fmt::color::medium_orchid);
      fmt::print(fmt::emphasis::bold | colout, "[AL iter {:>2d}]", al_iter + 1);
      fmt::print(" ("
                 " inner_tol {:.2g} |"
                 " prim_tol  {:.2g} |"
                 " mu  {:.2g} |"
                 " rho {:.2g} )\n",
                 inner_tol_, prim_tol_, mu_, rho_);
    }
    innerLoop(problem, workspace, results);
    computeInfeasibilities(problem, workspace, results);

    // accept primal updates
    workspace.prev_xs_ = results.xs_;
    workspace.prev_us_ = results.us_;

    if (results.primal_infeasibility <= prim_tol_) {
      updateTolerancesOnSuccess();

      switch (mul_update_mode) {
      case MultiplierUpdateMode::NEWTON:
        workspace.prev_lams_ = results.lams_;
        break;
      case MultiplierUpdateMode::PRIMAL:
        workspace.prev_lams_ = workspace.lams_plus_;
        break;
      case MultiplierUpdateMode::PRIMAL_DUAL:
        workspace.prev_lams_ = workspace.lams_pdal_;
        break;
      default:
        break;
      }

      if (std::max(results.primal_infeasibility, results.dual_infeasibility) <=
          target_tolerance) {
        conv = true;
        break;
      }
    } else {
      updateALPenalty();
      updateTolerancesOnFailure();
    }
    rho_ *= bcl_params.rho_update_factor;

    inner_tol_ = std::max(inner_tol_, target_tolerance);
    prim_tol_ = std::max(prim_tol_, target_tolerance);

    al_iter++;
  }

  if (verbose_ >= 1) {
    if (conv)
      fmt::print(fmt::fg(fmt::color::dodger_blue), "Successfully converged.");
    else {
      fmt::print(fmt::fg(fmt::color::red), "Convergence failure.");
    }
    fmt::print("\n");
  }
  invokeCallbacks(workspace, results);
  return conv;
}

template <typename Scalar>
void SolverProxDDP<Scalar>::innerLoop(const Problem &problem,
                                      Workspace &workspace, Results &results) {
  // instantiate the subproblem merit function
  PDALFunction<Scalar> merit_fun{mu_, rho_, ls_params.mode};

  auto merit_eval_fun = [&](Scalar a0) {
    tryStep(problem, workspace, results, a0);
    problem.evaluate(workspace.trial_xs_, workspace.trial_us_,
                     workspace.trial_prob_data);
    evaluateProx(workspace.trial_xs_, workspace.trial_us_, workspace);
    return merit_fun.evaluate(problem, workspace.trial_lams_, workspace,
                              workspace.trial_prob_data);
  };
  Scalar phi0 = 0.;
  Scalar eps = 1e-10;
  Scalar phieps = 0., dphi0 = 0.;

  std::size_t &k = results.num_iters;
  while (k < MAX_ITERS) {
    problem.evaluate(results.xs_, results.us_, workspace.problem_data);
    problem.computeDerivatives(results.xs_, results.us_,
                               workspace.problem_data);
    evaluateProx(results.xs_, results.us_, workspace);
    evaluateProxDerivatives(results.xs_, results.us_, workspace);

    backwardPass(problem, workspace, results);
    phi0 = merit_fun.evaluate(problem, results.lams_, workspace,
                              workspace.problem_data);
    computeInfeasibilities(problem, workspace, results);

    if (verbose_ >= 1) {
      fmt::print(" | inner_crit {:.3e}"
                 " | prim_err   {:.3e}"
                 " | dual_err   {:.3e}",
                 workspace.inner_criterion, results.primal_infeasibility,
                 results.dual_infeasibility);
      fmt::print(" |\n");
    }

    bool inner_conv = workspace.inner_criterion < inner_tol_;
    if (inner_conv) {
      break;
    } else {
      bool inner_acceptable = workspace.inner_criterion < target_tolerance;
      if (inner_acceptable &&
          (results.primal_infeasibility < target_tolerance)) {
        break;
      }
    }

    if (verbose_ >= 1) {
      fmt::print(fmt::fg(fmt::color::yellow_green), "[iter {:>3d}]", k + 1);
      fmt::print("\n");
    }

    computeDirection(problem, workspace, results);

    phieps = merit_eval_fun(eps);
    dphi0 = (phieps - phi0) / eps;

    Scalar alpha_opt = 1;

    switch (ls_params.strategy) {
    case LinesearchStrategy::ARMIJO:
      proxnlp::ArmijoLinesearch<Scalar>::run(
          merit_eval_fun, phi0, dphi0, verbose_, ls_params.ls_beta,
          ls_params.armijo_c1, ls_params.alpha_min, alpha_opt);
      break;
    case LinesearchStrategy::CUBIC_INTERP:
      proxnlp::CubicInterpLinesearch<Scalar>::run(
          merit_eval_fun, phi0, dphi0, verbose_, ls_params.armijo_c1,
          ls_params.alpha_min, alpha_opt);
      break;
    default:
      break;
    }

    results.traj_cost_ = merit_fun.traj_cost;
    results.merit_value_ = merit_fun.value_;
    if (verbose_ >= 1) {
      fmt::print(" | alpha  {:.3e}"
                 " | dphi0  {:.3e}"
                 " | merit  {:.3e}\n",
                 alpha_opt, dphi0, results.merit_value_);
    }

    // accept the step
    results.xs_ = workspace.trial_xs_;
    results.us_ = workspace.trial_us_;
    results.lams_ = workspace.trial_lams_;

    invokeCallbacks(workspace, results);

    k++;
  }
}

template <typename Scalar>
void SolverProxDDP<Scalar>::computeInfeasibilities(const Problem &problem,
                                                   Workspace &workspace,
                                                   Results &results) const {
  const TrajOptDataTpl<Scalar> &prob_data = workspace.problem_data;
  const std::size_t nsteps = problem.numSteps();
  results.primal_infeasibility = 0.;
  Scalar infeas_over_j = 0.;
  for (std::size_t step = 0; step < nsteps; step++) {
    const StageModel &stage = *problem.stages_[step];
    const StageData &stage_data = prob_data.getData(step);
    infeas_over_j = 0.;
    for (std::size_t j = 0; j < stage.numConstraints(); j++) {
      const ConstraintSetBase<Scalar> &cstr_set =
          stage.constraints_.getConstraintSet(j);
      auto &v = stage_data.constraint_data[j]->value_;
      cstr_set.normalConeProjection(v, v);
      infeas_over_j = std::max(infeas_over_j, math::infty_norm(v));
    }
    workspace.primal_infeas_by_stage(long(step)) = infeas_over_j;
  }
  results.primal_infeasibility =
      math::infty_norm(workspace.primal_infeas_by_stage);
  return;
}

} // namespace proxddp
