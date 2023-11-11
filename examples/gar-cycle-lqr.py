from proxddp import gar

import numpy as np

import matplotlib.pyplot as plt
import utils  # noqa


np.random.seed(42)
np.set_printoptions(precision=3, linewidth=250)

nx = 1
nu = 1


def create_knot(nx, nu):
    knot = gar.LQRKnot(nx, nu, 0)
    knot.Q[:] = np.eye(nx) * 0.01
    knot.R[:] = np.eye(nu) * 0.01
    knot.A[:] = 1.2
    knot.B[:] = np.eye(nx, nu)
    knot.E[:] = -np.eye(nx)
    return knot


T = 20
knots = [create_knot(nx, nu) for _ in range(T)]
knots.append(create_knot(nx, 0))
prob = gar.LQRProblem(knots, 0)

PARAM_DIM = nx
prob.addParameterization(PARAM_DIM)

xf = np.array([0.1])
kf = prob.stages[T]
kf.Q[:] = np.eye(nx) * 0.1
kf.q[:] = -kf.Q @ xf

t0 = T // 3
kt0 = prob.stages[t0]
kt0.q[:] = -kt0.Q @ np.array([0.1])

print(prob.stages[0])
print(prob.stages[T])
prob.stages[0].Gammax[:] = +np.eye(nx)
prob.stages[T].Gammax[:] = -np.eye(nx)

solver = gar.ProximalRiccatiSolver(prob)
mu = 1e-5
solver.backward(mu, mu)

sol_ = gar.getInitProblemTraj(prob)


th_grad = solver.thGrad
th_hess = solver.thHess
print("thGrad:", th_grad)
print("thHess:", th_hess)

th_opt = np.linalg.solve(th_hess, -th_grad)
solver.forward(**sol_, theta=th_opt)

xss = np.stack(sol_["xs"])
times = np.arange(0, T + 1)

print("xs[0] = ", xss[0])
plt.figure()
plt.plot(times, xss, marker="x")
plt.hlines(
    xss[0], times[0], times[-1], colors="k", linestyles="--", label="$x^\\star_0$"
)
ax = plt.gca()
ax.xaxis.set_major_locator(plt.MultipleLocator())

plt.title("$x_t$ (cyclic)")
plt.legend()
plt.tight_layout()
plt.savefig("examples/gar-cyclic-lqr-1d.png")

plt.show()
