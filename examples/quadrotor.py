"""
Simple quadrotor dynamics example.

Inspired by: https://github.com/loco-3d/crocoddyl/blob/master/examples/quadrotor.py
"""
import pinocchio as pin
import example_robot_data as erd

import numpy as np
import meshcat_utils

import proxddp
from proxddp import manifolds
from proxnlp import constraints

from utils.custom_functions import ControlBoxFunction as PyControlBoxFunction

import tap


class Args(tap.Tap):
    display: bool = False
    record: bool = False

    def process_args(self):
        if self.record:
            self.display = True


args = Args().parse_args()
print(args)

robot = erd.load("hector")
rmodel = robot.model
rdata = robot.data
nq = rmodel.nq
nv = rmodel.nv

vizer = pin.visualize.MeshcatVisualizer(rmodel, robot.collision_model, robot.visual_model, data=rdata)
vizer.initViewer(loadModel=True)
if args.display:
    vizer.viewer.open()
augvizer = meshcat_utils.ForceDraw(vizer)

space = manifolds.MultibodyPhaseSpace(rmodel)
print("Space:", space)
print("nq:", nq, "nv:", rmodel.nv)


# The matrix below maps rotor controls to torques

d_cog, cf, cm, u_lim, l_lim = 0.1525, 6.6e-5, 1e-6, 5., 0.1
QUAD_ACT_MATRIX = np.array(
    [[0., 0., 0., 0.],
     [0., 0., 0., 0.],
     [1., 1., 1., 1.],
     [0., d_cog, 0., -d_cog],
     [-d_cog, 0., d_cog, 0.],
     [-cm / cf, cm / cf, -cm / cf, cm / cf]]
)
nu = QUAD_ACT_MATRIX.shape[1]  # = no. of nrotors


class EulerIntegratorDynamics(proxddp.dynamics.ExplicitDynamicsModel):
    def __init__(self, dt: float, B: np.ndarray):
        self.dt = dt
        self.model = rmodel
        self.data = self.model.createData()
        self.B = B
        super().__init__(space, nu)

    def forward(self, x, u, out):
        assert out.size == space.nx
        q = x[:self.model.nq]
        v = x[self.model.nq:]
        tau = self.B @ u
        acc = pin.aba(self.model, self.data, q, v, tau)
        qout = out[:self.model.nq]
        vout = out[self.model.nq:]
        vout[:] = v + self.dt * acc
        qout[:] = pin.integrate(self.model, q, self.dt * vout)

    def dForward(self, x, u, Jx, Ju):
        Jx[:, :] = 0.
        Ju[:, :] = 0.
        q = x[:self.model.nq]
        v = x[self.model.nq:]
        tau = self.B @ u
        acc = pin.aba(self.model, self.data, q, v, tau)
        [dacc_dq, dacc_dv, dacc_dtau] = pin.computeABADerivatives(self.model, self.data, q, v, tau)
        dx = np.concatenate([self.dt * (v + self.dt * acc), self.dt * acc])

        dacc_dx = np.hstack([dacc_dq, dacc_dv])
        dacc_du = dacc_dtau @ self.B

        # Jx <- ddx_dx
        Jx[nv:, :] = dacc_dx * self.dt
        Jx[:nv, :] = Jx[nv:, :] * self.dt
        Jx[:nv, nv:] += np.eye(nv) * self.dt
        Ju[nv:, :] = dacc_du * self.dt
        Ju[:nv, :] = Ju[nv:, :] * self.dt
        space.JintegrateTransport(x, dx, Jx, 1)
        space.JintegrateTransport(x, dx, Ju, 1)

        Jtemp0 = np.zeros((space.ndx, space.ndx))
        space.Jintegrate(x, dx, Jtemp0, 0)
        Jx[:, :] = Jtemp0 + Jx


dt = 0.03
Tf = 2.
nsteps = int(Tf / dt)

dynmodel = EulerIntegratorDynamics(dt, QUAD_ACT_MATRIX)

x0 = np.concatenate([robot.q0, np.zeros(nv)])

u0 = np.zeros(nu)
vizer.display(x0[:nq])
out = space.neutral()

dynmodel.forward(x0, u0, out)
np.set_printoptions(precision=2, linewidth=250)
Jx = np.zeros((space.ndx, space.ndx))
Ju = np.zeros((space.ndx, nu))
Jx_nd = Jx.copy()

x1 = space.rand()
dynmodel.dForward(x1, u0, Jx, Ju)


EPS = 1e-7
ei = np.zeros(space.ndx)
x_n_plus = space.neutral()
x_n_ = space.neutral()
for i in range(space.ndx):
    ei[i] = EPS
    dynmodel.forward(x1, u0, x_n_)
    dynmodel.forward(space.integrate(x1, ei), u0, x_n_plus)
    Jx_nd[:, i] = space.difference(x_n_, x_n_plus) / EPS
    ei[i] = 0.


print(Jx, "Jx")
print(Jx_nd, "Jx_nd")
error_ = abs(Jx_nd - Jx)
print(error_)
print("Error nd:", np.max(error_))


us_init = [u0] * nsteps
xs_init = [x0] * (nsteps + 1)

# input("[enter]")
meshcat_utils.display_trajectory(vizer, augvizer, xs_init, wait=dt)

x_tar = space.neutral()
x_tar[:3] = (0.9, 0.1, 1.)

u_max = 4. * np.ones(nu)
u_min = -u_max


def setup():

    state_err = proxddp.StateErrorResidual(space, nu, x_tar)
    weights1 = np.zeros(space.ndx)
    weights1[:3] = 0.
    weights1[3:nv] = 1e-2
    weights1[nv:] = 1e-1
    w_x_term = weights1.copy()
    print(state_err.nr)
    assert state_err.nr == weights1.shape[0]

    rcost = proxddp.CostStack(space.ndx, nu)
    xreg_cost = proxddp.QuadraticResidualCost(state_err, 1e-3 * np.diag(weights1) * dt)
    rcost.addCost(xreg_cost)

    utar = np.zeros(nu)
    u_err = proxddp.ControlErrorResidual(space.ndx, nu, utar)
    w_u = np.eye(nu) * 1e-4
    ucost = proxddp.QuadraticResidualCost(u_err, w_u * dt)
    rcost.addCost(ucost)

    w_x_term[:6] = 1.
    term_cost = proxddp.QuadraticResidualCost(state_err, np.diag(w_x_term))
    prob = proxddp.ShootingProblem(x0, nu, space, term_cost=term_cost)
    for i in range(nsteps):
        stage = proxddp.StageModel(space, nu, rcost, dynmodel)
        stage.addConstraint(
            PyControlBoxFunction(space.ndx, nu, u_min, u_max),
            constraints.NegativeOrthant()
        )
        prob.addStage(stage)

    return prob


problem = setup()
tol = 1e-4
mu_init = 0.01
verbose = proxddp.VerboseLevel.VERBOSE
solver = proxddp.ProxDDP(tol, mu_init, verbose=verbose)
solver.run(problem, xs_init, us_init)

results = solver.getResults()
print(results)
xs_opt = results.xs.tolist()
us_opt = results.us.tolist()

import matplotlib.pyplot as plt

times = np.linspace(0, Tf, nsteps + 1)
fig: plt.Figure = plt.figure()
ax0 = fig.add_subplot(111)
ax0.plot(times[:-1], us_opt)
ax0.set_title("Controls")
ax0.set_xlabel("Time")


if args.display:
    import imageio
    frames_ = []
    input("[enter to play]")
    for _ in range(3):
        frames_ += meshcat_utils.display_trajectory(vizer, augvizer, xs_opt,
                                                    frame_ids=[rmodel.getFrameId("base_link")],
                                                    record=args.record, wait=dt, show_vel=True)

    imageio.mimwrite("examples/quadrotor_fly.mp4", frames_, fps=1. / dt)

plt.show()
