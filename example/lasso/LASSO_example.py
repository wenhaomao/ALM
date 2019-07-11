#!/usr/bin/env python

import math
import numpy as np
from alm import ALM
from sklearn.model_selection import GroupKFold
from sklearn.linear_model import lasso_path
from sklearn.preprocessing import StandardScaler
from sklearn.linear_model import Lasso
import matplotlib.pyplot as plt

def get_crystal_structure():
    lavec = [[20.406, 0, 0],
             [0, 20.406, 0],
             [0, 0, 20.406]]
    xcoord = [[0.0000000000000000, 0.0000000000000000, 0.0000000000000000],
              [0.0000000000000000, 0.0000000000000000, 0.5000000000000000],
              [0.0000000000000000, 0.2500000000000000, 0.2500000000000000],
              [0.0000000000000000, 0.2500000000000000, 0.7500000000000000],
              [0.0000000000000000, 0.5000000000000000, 0.0000000000000000],
              [0.0000000000000000, 0.5000000000000000, 0.5000000000000000],
              [0.0000000000000000, 0.7500000000000000, 0.2500000000000000],
              [0.0000000000000000, 0.7500000000000000, 0.7500000000000000],
              [0.1250000000000000, 0.1250000000000000, 0.1250000000000000],
              [0.1250000000000000, 0.1250000000000000, 0.6250000000000000],
              [0.1250000000000000, 0.3750000000000000, 0.3750000000000000],
              [0.1250000000000000, 0.3750000000000000, 0.8750000000000000],
              [0.1250000000000000, 0.6250000000000000, 0.1250000000000000],
              [0.1250000000000000, 0.6250000000000000, 0.6250000000000000],
              [0.1250000000000000, 0.8750000000000000, 0.3750000000000000],
              [0.1250000000000000, 0.8750000000000000, 0.8750000000000000],
              [0.2500000000000000, 0.0000000000000000, 0.2500000000000000],
              [0.2500000000000000, 0.0000000000000000, 0.7500000000000000],
              [0.2500000000000000, 0.2500000000000000, 0.0000000000000000],
              [0.2500000000000000, 0.2500000000000000, 0.5000000000000000],
              [0.2500000000000000, 0.5000000000000000, 0.2500000000000000],
              [0.2500000000000000, 0.5000000000000000, 0.7500000000000000],
              [0.2500000000000000, 0.7500000000000000, 0.0000000000000000],
              [0.2500000000000000, 0.7500000000000000, 0.5000000000000000],
              [0.3750000000000000, 0.1250000000000000, 0.3750000000000000],
              [0.3750000000000000, 0.1250000000000000, 0.8750000000000000],
              [0.3750000000000000, 0.3750000000000000, 0.1250000000000000],
              [0.3750000000000000, 0.3750000000000000, 0.6250000000000000],
              [0.3750000000000000, 0.6250000000000000, 0.3750000000000000],
              [0.3750000000000000, 0.6250000000000000, 0.8750000000000000],
              [0.3750000000000000, 0.8750000000000000, 0.1250000000000000],
              [0.3750000000000000, 0.8750000000000000, 0.6250000000000000],
              [0.5000000000000000, 0.0000000000000000, 0.0000000000000000],
              [0.5000000000000000, 0.0000000000000000, 0.5000000000000000],
              [0.5000000000000000, 0.2500000000000000, 0.2500000000000000],
              [0.5000000000000000, 0.2500000000000000, 0.7500000000000000],
              [0.5000000000000000, 0.5000000000000000, 0.0000000000000000],
              [0.5000000000000000, 0.5000000000000000, 0.5000000000000000],
              [0.5000000000000000, 0.7500000000000000, 0.2500000000000000],
              [0.5000000000000000, 0.7500000000000000, 0.7500000000000000],
              [0.6250000000000000, 0.1250000000000000, 0.1250000000000000],
              [0.6250000000000000, 0.1250000000000000, 0.6250000000000000],
              [0.6250000000000000, 0.3750000000000000, 0.3750000000000000],
              [0.6250000000000000, 0.3750000000000000, 0.8750000000000000],
              [0.6250000000000000, 0.6250000000000000, 0.1250000000000000],
              [0.6250000000000000, 0.6250000000000000, 0.6250000000000000],
              [0.6250000000000000, 0.8750000000000000, 0.3750000000000000],
              [0.6250000000000000, 0.8750000000000000, 0.8750000000000000],
              [0.7500000000000000, 0.0000000000000000, 0.2500000000000000],
              [0.7500000000000000, 0.0000000000000000, 0.7500000000000000],
              [0.7500000000000000, 0.2500000000000000, 0.0000000000000000],
              [0.7500000000000000, 0.2500000000000000, 0.5000000000000000],
              [0.7500000000000000, 0.5000000000000000, 0.2500000000000000],
              [0.7500000000000000, 0.5000000000000000, 0.7500000000000000],
              [0.7500000000000000, 0.7500000000000000, 0.0000000000000000],
              [0.7500000000000000, 0.7500000000000000, 0.5000000000000000],
              [0.8750000000000000, 0.1250000000000000, 0.3750000000000000],
              [0.8750000000000000, 0.1250000000000000, 0.8750000000000000],
              [0.8750000000000000, 0.3750000000000000, 0.1250000000000000],
              [0.8750000000000000, 0.3750000000000000, 0.6250000000000000],
              [0.8750000000000000, 0.6250000000000000, 0.3750000000000000],
              [0.8750000000000000, 0.6250000000000000, 0.8750000000000000],
              [0.8750000000000000, 0.8750000000000000, 0.1250000000000000],
              [0.8750000000000000, 0.8750000000000000, 0.6250000000000000]]
    kd = [14] * len(xcoord)

    return lavec, xcoord, kd


def get_data(ndata, nat):
    force = np.loadtxt("force_random.dat").reshape((-1, nat, 3))[:ndata]
    disp = np.loadtxt("disp_random.dat").reshape((-1, nat, 3))[:ndata]

    return force, disp


def run_alm(crystal, disp, force, maxorder=None, cutoff=None, nbody=None):
    with ALM(*crystal) as alm:
        alm.set_verbosity(1)
        alm.define(maxorder, cutoff, nbody)
        alm.set_constraint(translation=True)
        alm.set_training_data(disp, force)
        X, y = alm.get_matrix_elements()
    return X, y


def run_scikit_learn(X, y, nat,
                     force_ravel,
                     n_alphas=100,
                     eps=1.0e-7,
                     n_splits=5,
                     standardize=True):
    print("force shape: ", np.shape(force_ravel))
    print("Amat shape : ", np.shape(X))

    groups = []
    for i in range(30):
        groups.extend([i] * 3 * nat)

    gkf = GroupKFold(n_splits=n_splits)

    rms_train = np.zeros((n_alphas, n_splits))
    rms_test = np.zeros((n_alphas, n_splits))

    counter = 0
    alphas = np.logspace(-1, -6, num=n_alphas)

    sc = StandardScaler()

    for train, test in gkf.split(X, y, groups=groups):

        force_train = force_ravel[train]
        force_test = force_ravel[test]
        fnorm_train = np.dot(force_train, force_train)
        fnorm_test = np.dot(force_test, force_test)

        # Set displacement and force data
        X_train = X[train, :]
        X_test = X[test, :]
        y_train = y[train]
        y_test = y[test]

        if standardize:
            scaler = sc.fit(X_train)
            X_train = scaler.transform(X_train)
            X_test = scaler.transform(X_test)

        alphas_lasso, coefs_lasso, _ = lasso_path(X_train, y_train, eps=eps,
                                                  alphas=alphas,
                                                  verbose=True,
                                                  fit_intercept=False)

        y_model_train = np.dot(X_train, coefs_lasso)
        y_model_test = np.dot(X_test, coefs_lasso)

        for ialpha in range(n_alphas):
            y_diff_train = y_model_train[:, ialpha] - y_train
            y_diff_test = y_model_test[:, ialpha] - y_test

            residual_train = np.dot(y_diff_train, y_diff_train)
            residual_test = np.dot(y_diff_test, y_diff_test)

            rms_train[ialpha, counter] = math.sqrt(residual_train/fnorm_train)
            rms_test[ialpha, counter] = math.sqrt(residual_test/fnorm_test)

        counter += 1

    rmse_mean = np.mean(rms_train, axis=1)
    rmse_std = np.std(rms_train, axis=1, ddof=1)
    cv_mean = np.mean(rms_test, axis=1)
    cv_std = np.std(rms_test, axis=1, ddof=1)

    data = np.zeros((n_alphas, 5), dtype=np.float64)
    data[:, 0] = alphas
    data[:, 1] = rmse_mean
    data[:, 2] = rmse_std
    data[:, 3] = cv_mean
    data[:, 4] = cv_std

    np.savetxt('sklearn_cvscore.dat', data)

    alpha_opt = alphas[np.argmin(cv_mean)]
    print(alpha_opt)
    clf = Lasso(alpha=alpha_opt, fit_intercept=False, tol=eps)
    if standardize:
        scaler = sc.fit(X)
        X = scaler.transform(X)
        clf.fit(X, y)
        fc = np.true_divide(clf.coef_, scaler.scale_)
    else:
        clf.fit(X, y)
        fc = clf.coef_

    return fc, alphas_lasso, rmse_mean, cv_mean


def main():
    crystal = get_crystal_structure()
    nat = len(crystal[1])
    ndata = 30
    force, disp = get_data(ndata, nat)
    maxorder = 5
    cutoff = [-1, -1, 15, 8, 8]
    nbody = [2, 3, 3, 2, 2]
    X, y = run_alm(crystal, disp, force,
                   maxorder=maxorder, cutoff=cutoff, nbody=nbody)

    n_alphas = 300
    eps = 1.0e-7
    n_splits = 5
    standardize = True
    fc, alphas_lasso, rmse_mean, cv_mean = run_scikit_learn(
        X, y, nat,
        force.ravel(),
        n_alphas=n_alphas,
        eps=eps,
        n_splits=n_splits,
        standardize=standardize)

    print(rmse_mean)
    print(cv_mean)
    print(fc)
    print(np.linalg.norm(fc, 0))

    with ALM(*crystal) as alm:
        alm.define(maxorder, cutoff, nbody)
        alm.set_constraint(translation=True)
        alm.set_fc(fc)
        fc_values1, elem_indices1 = alm.get_fc(1, mode='origin')

    c = "xyz"
    for (val, elem) in zip(fc_values1, elem_indices1):
        v1 = elem[0] // 3
        c1 = elem[0] % 3
        v2 = elem[1] // 3
        c2 = elem[1] % 3
        print("%15.7f %d%s %d%s" % ((val, v1 + 1, c[c1], v2 + 1, c[c2])))

    ax = plt.subplot(111)
    ax.plot(alphas_lasso, rmse_mean, linestyle='--', marker='o', ms=5)
    ax.plot(alphas_lasso, cv_mean, linestyle='--', marker='o', ms=5)
    ax.set_xscale('log')

    plt.show()


if __name__ == '__main__':
    main()
