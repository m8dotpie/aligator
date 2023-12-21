#include "proxddp/fwd.hpp"

#ifdef ALIGATOR_PINOCCHIO_V3
#include "proxddp/modelling/dynamics/multibody-constraint-fwd.hpp"

namespace aligator {
namespace dynamics {

template struct MultibodyConstraintFwdDynamicsTpl<context::Scalar>;
template struct MultibodyConstraintFwdDataTpl<context::Scalar>;

} // namespace dynamics
} // namespace aligator
#endif
