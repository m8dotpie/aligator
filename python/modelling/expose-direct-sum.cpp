/// @file
/// @copyright Copyright (C) 2022-2023 LAAS-CNRS, INRIA

#include "proxddp/python/fwd.hpp"
#include "proxddp/modelling/explicit-dynamics-direct-sum.hpp"

namespace proxddp {
namespace python {

using context::Scalar;
using DirectSumExplicitDynamics = DirectSumExplicitDynamicsTpl<Scalar>;
using context::ExplicitDynamics;

void exposeExplicitDynDirectSum() {

  bp::register_ptr_to_python<shared_ptr<DirectSumExplicitDynamics>>();
  bp::class_<DirectSumExplicitDynamics, bp::bases<ExplicitDynamics>>(
      "DirectSumExplicitDynamics",
      "Direct sum :math:`f \\oplus g` of two explicit dynamical models.",
      bp::no_init)
      .def(bp::init<shared_ptr<ExplicitDynamics>, shared_ptr<ExplicitDynamics>>(
          bp::args("self", "f", "g")));

  bp::class_<DirectSumExplicitDynamics::Data,
             bp::bases<context::ExplicitDynamicsData>>(
      "DirectSumExplicitDynamicsData", bp::no_init)
      .def_readwrite("data1", &DirectSumExplicitDynamics::Data::data1_)
      .def_readwrite("data2", &DirectSumExplicitDynamics::Data::data2_);

  bp::def("directSum", directSum<Scalar>, bp::args("f", "g"),
          "Produce the direct sum.");
}

} // namespace python
} // namespace proxddp
