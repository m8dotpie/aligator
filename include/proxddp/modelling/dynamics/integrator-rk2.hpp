#pragma once

#include "proxddp/modelling/dynamics/integrator-explicit.hpp"


namespace proxddp
{
  namespace dynamics
  {
    template<typename Scalar>
    struct RK2DataTpl;

    /** @brief  Second-order Runge-Kutta integrator.
     * 
     * \f{eqnarray*}{
     *    x_{k+1} = x_k \oplus h f(x^{(1)}, u_k),\\
     *    x^{(1)} = x_k \oplus \frac h2 f(x_k, u_k)
     * \f}
     * 
     */
    template<typename _Scalar>
    struct IntegratorRK2Tpl : ExplicitIntegratorAbstractTpl<_Scalar>
    {
      using Scalar = _Scalar;
      PROXNLP_DYNAMIC_TYPEDEFS(Scalar);
      using Base = ExplicitIntegratorAbstractTpl<Scalar>;
      using BaseData = ExplicitDynamicsDataTpl<Scalar>;
      using Data = RK2DataTpl<Scalar>;
      using ODEType = typename Base::ODEType;

      Scalar timestep_;
      Scalar dt_2_ = 0.5 * timestep_;

      IntegratorRK2Tpl(const shared_ptr<ODEType>& cont_dynamics, const Scalar timestep);
      void forward(const ConstVectorRef& x, const ConstVectorRef& u, BaseData& data) const;
      void dForward(const ConstVectorRef& x, const ConstVectorRef& u, BaseData& data) const;
    };
    
    template<typename Scalar>
    struct RK2DataTpl : ExplicitIntegratorDataTpl<Scalar>
    {
      PROXNLP_DYNAMIC_TYPEDEFS(Scalar);
      using Base = ExplicitIntegratorDataTpl<Scalar>;
      using ODEData = ODEDataTpl<Scalar>;
      shared_ptr<ODEData> continuous_data2;

      VectorXs x1_;
      VectorXs dx1_;

      explicit RK2DataTpl(const IntegratorRK2Tpl<Scalar>* integrator);

      using Base::dx_;
      using Base::xnext_;
      using Base::Jx_;
      using Base::Ju_;
      using Base::Jtmp_xnext;
    };

  } // namespace dynamics
} // namespace proxddp

#include "proxddp/modelling/dynamics/integrator-rk2.hxx"
