/*
 optimize.cpp

 Copyright (c) 2014-2018 Terumasa Tadano

 This file is distributed under the terms of the MIT license.
 Please see the file 'LICENCE.txt' in the root directory
 or http://opensource.org/licenses/mit-license.php for information.
*/

#include "optimize.h"
#include "files.h"
#include "constants.h"
#include "constraint.h"
#include "error.h"
#include "fcs.h"
#include "input_parser.h"
#include "mathfunctions.h"
#include "memory.h"
#include "symmetry.h"
#include "timer.h"
#include <iostream>
#include <cmath>
#include <string>
#include <vector>
#include <boost/lexical_cast.hpp>

#ifdef WITH_SPARSE_SOLVER
#include <Eigen/Dense>
#include <Eigen/SparseCore>
#include <Eigen/SparseQR>
#include <Eigen/SparseCholesky>
//#include <Eigen/IterativeLinearSolvers>
//#include <unsupported/Eigen/SparseExtra>
//#include <bench/BenchTimer.h>
#endif

using namespace ALM_NS;

Optimize::Optimize()
{
    set_default_variables();
}

Optimize::~Optimize()
{
    deallocate_variables();
}

void Optimize::set_default_variables()
{
    params = nullptr;
    u_in = nullptr;
    f_in = nullptr;
    ndata = 0;
    nstart = 1;
    nend = 0;
    skip_s = 0;
    skip_e = 0;
    ndata_used = 0;
    ndata_test = 0;
    nstart_test = 0;
    nend_test = 0;
}

void Optimize::deallocate_variables()
{
    if (params) {
        deallocate(params);
    }
    if (u_in) {
        deallocate(u_in);
    }
    if (f_in) {
        deallocate(f_in);
    }
}

int Optimize::optimize_main(const Symmetry *symmetry,
                            Constraint *constraint,
                            const Fcs *fcs,
                            const int maxorder,
                            const std::string file_prefix,
                            const std::vector<std::string> &str_order,
                            const unsigned int nat,
                            const int verbosity,
                            const std::string file_disp,
                            const std::string file_force,
                            Timer *timer)
{
    timer->start_clock("optimize");

    const int natmin = symmetry->get_nat_prim();
    const auto ndata_used = nend - nstart + 1 - skip_e + skip_s;
    const auto ndata_used_test = nend_test - nstart_test + 1;
    const int ntran = symmetry->get_ntran();
    auto info_fitting = 0;
    const int M = 3 * natmin * static_cast<long>(ndata_used) * ntran;
    const auto M_test = 3 * natmin * ndata_used_test * ntran;
    auto N = 0;
    auto N_new = 0;
    for (auto i = 0; i < maxorder; ++i) {
        N += fcs->get_nequiv()[i].size();
    }

    if (constraint->get_constraint_algebraic()) {
        for (auto i = 0; i < maxorder; ++i) {
            N_new += constraint->get_index_bimap(i).size();
        }
    }


    if (verbosity > 0) {
        std::cout << " OPTIMIZATION" << std::endl;
        std::cout << " ============" << std::endl << std::endl;

        std::cout << "  Reference files" << std::endl;
        std::cout << "   Displacement: " << file_disp << std::endl;
        std::cout << "   Force       : " << file_force << std::endl;
        std::cout << std::endl;

        std::cout << "  NSTART = " << nstart << "; NEND = " << nend;
        if (skip_s < skip_e) std::cout << ": SKIP = " << skip_s + 1 << "-" << skip_e;
        std::cout << std::endl;
        std::cout << "  " << ndata_used
            << " entries will be used for optimization." << std::endl << std::endl;

        if (optcontrol.cross_validation_mode == 2) {
            std::cout << "  Validation test files" << std::endl;
            std::cout << "   Displacement: " << dfile_test << std::endl;
            std::cout << "   Force       : " << ffile_test << std::endl;
            std::cout << std::endl;

            std::cout << "  NSTART = " << nstart_test << "; NEND = " << nend_test << std::endl;
            std::cout << "  " << ndata_used_test
                << " entries will be used for validation." << std::endl << std::endl;
        }

        std::cout << "  Total Number of Parameters : " << N << '\n';
        if (constraint->get_constraint_algebraic()) {
            std::cout << "  Total Number of Free Parameters : " << N_new << '\n';
        }
        std::cout << '\n';
    }

    // Parse displacement and force data sets from files

    double **u = nullptr;
    double **f = nullptr;
    double **u_test = nullptr;
    double **f_test = nullptr;

    const auto input_parser = new InputParser();
    
    allocate(u, ndata_used, 3 * nat);
    allocate(f, ndata_used, 3 * nat);

    if (optcontrol.optimizer == 1 && !u_in || optcontrol.optimizer == 2) {
     // This if condition is necessary because DFILE and FFILE are not
     // defined when the method is called via API.
    input_parser->parse_displacement_and_force_files(u,
                                                     f,
                                                     nat,
                                                     ndata,
                                                     nstart,
                                                     nend,
                                                     skip_s,
                                                     skip_e,
                                                     file_disp,
                                                     file_force);
    }
    if (optcontrol.optimizer == 2 &&
        optcontrol.cross_validation_mode == 1) {

        allocate(u_test, ndata_used_test, 3 * nat);
        allocate(f_test, ndata_used_test, 3 * nat);

        input_parser->parse_displacement_and_force_files(u_test,
                                                         f_test,
                                                         nat,
                                                         ndata_test,
                                                         nstart_test,
                                                         nend_test,
                                                         0,
                                                         0,
                                                         dfile_test,
                                                         ffile_test);
    }

    delete input_parser;

    // Run optimization and obtain force constants

    std::vector<double> fcs_tmp(N, 0.0);

    if (optcontrol.optimizer == 1) {

        // Use ordinary least-squares

        info_fitting = least_squares(maxorder,
                                     natmin,
                                     ntran,
                                     N,
                                     N_new,
                                     M,
                                     verbosity,
                                     symmetry,
                                     fcs,
                                     constraint,
                                     fcs_tmp);

    } else if (optcontrol.optimizer == 2) {

        // Use elastic net 

        info_fitting = elastic_net(file_prefix,
                                   maxorder,
                                   natmin,
                                   ntran,
                                   N,
                                   N_new,
                                   M,
                                   M_test,
                                   u,
                                   f,
                                   u_test,
                                   f_test,
                                   symmetry,
                                   str_order,
                                   fcs,
                                   constraint,
                                   nat,
                                   verbosity,
                                   fcs_tmp);
    }

    if (u) {
        deallocate(u);
    }
    if (f) {
        deallocate(f);
    }
    if (u_test) {
        deallocate(u_test);
    }
    if (f_test) {
        deallocate(f_test);
    }

    if (info_fitting == 0) {
        // I should copy fcs_tmp to parameters in the Fcs class?

        // Copy force constants to public variable "params"
        if (params) {
            deallocate(params);
        }
        allocate(params, N);
        for (auto i = 0; i < N; ++i) params[i] = fcs_tmp[i];
    }

    fcs_tmp.clear();
    fcs_tmp.shrink_to_fit();

    if (verbosity > 0) {
        std::cout << std::endl;
        timer->print_elapsed();
        std::cout << " -------------------------------------------------------------------" << std::endl;
        std::cout << std::endl;
    }

    timer->stop_clock("optimize");

    return info_fitting;
}

int Optimize::least_squares(const int maxorder,
                            const int natmin,
                            const int ntran,
                            const int N,
                            const int N_new,
                            const int M,
                            const int verbosity,
                            const Symmetry *symmetry,
                            const Fcs *fcs,
                            const Constraint *constraint,
                            std::vector<double> &param_out)
{
    auto info_fitting = 0;

    std::vector<double> amat;
    std::vector<double> bvec;

    if (constraint->get_constraint_algebraic()) {

        // Apply constraints algebraically. (ICONST = 2, 3 is not supported.)
        // SPARSE = 1 is used only when the constraints are considered algebraically.

        // Calculate matrix elements for fitting

        double fnorm;
        const unsigned long nrows = 3 * static_cast<long>(natmin)
            * static_cast<long>(ndata_used)
            * static_cast<long>(ntran);

        const unsigned long ncols = static_cast<long>(N_new);

        if (optcontrol.use_sparse_solver) {

            // Use a solver for sparse matrix 
            // (Requires less memory for sparse inputs.)

#ifdef WITH_SPARSE_SOLVER
            SpMat sp_amat(nrows, ncols);
            Eigen::VectorXd sp_bvec(nrows);

            get_matrix_elements_in_sparse_form(maxorder,
                                               ndata_used,
                                               sp_amat,
                                               sp_bvec,
                                               fnorm,
                                               symmetry,
                                               fcs,
                                               constraint);
            if (verbosity > 0) {
                std::cout << "Now, start fitting ..." << std::endl;
            }

            info_fitting = run_eigen_sparseQR(sp_amat,
                                              sp_bvec,
                                              param_out,
                                              fnorm,
                                              maxorder,
                                              fcs,
                                              constraint,
                                              verbosity);
#else
            std::cout << " Please recompile the code with -DWITH_SPARSE_SOLVER" << std::endl;
            exit("optimize_main", "Sparse solver not supported.");
#endif

        } else {

            // Use a direct solver for a dense matrix

            amat.resize(nrows * ncols, 0.0);
            bvec.resize(nrows, 0.0);

            get_matrix_elements_algebraic_constraint(maxorder,
                                                     ndata_used,
                                                     &amat[0],
                                                     &bvec[0],
                                                     fnorm,
                                                     symmetry,
                                                     fcs,
                                                     constraint);

            // Perform fitting with SVD

            info_fitting
                = fit_algebraic_constraints(N_new,
                                            M,
                                            &amat[0],
                                            &bvec[0],
                                            param_out,
                                            fnorm,
                                            maxorder,
                                            fcs,
                                            constraint,
                                            verbosity);
        }

    } else {

        // Apply constraints numerically (ICONST=2 is supported)

        if (optcontrol.use_sparse_solver && verbosity > 0) {
            std::cout << "  WARNING: SPARSE = 1 works only with ICONST = 10 or ICONST = 11." << std::endl;
            std::cout << "  Use a solver for dense matrix." << std::endl;
        }

        // Calculate matrix elements for fitting

        const unsigned long nrows = 3 * static_cast<long>(natmin)
            * static_cast<long>(ndata_used)
            * static_cast<long>(ntran);

        const unsigned long ncols = static_cast<long>(N);

        amat.resize(nrows * ncols, 0.0);
        bvec.resize(nrows, 0.0);

        get_matrix_elements(maxorder,
                            ndata_used,
                            &amat[0],
                            &bvec[0],
                            symmetry,
                            fcs);

        // Perform fitting with SVD or QRD

        assert(!amat.empty());
        assert(!bvec.empty());

        if (constraint->get_exist_constraint()) {
            info_fitting
                = fit_with_constraints(N,
                                       M,
                                       constraint->get_number_of_constraints(),
                                       &amat[0],
                                       &bvec[0],
                                       &param_out[0],
                                       constraint->get_const_mat(),
                                       constraint->get_const_rhs(),
                                       verbosity);
        } else {
            info_fitting
                = fit_without_constraints(N,
                                          M,
                                          &amat[0],
                                          &bvec[0],
                                          &param_out[0],
                                          verbosity);
        }
    }


    return info_fitting;
}


int Optimize::elastic_net(const std::string job_prefix,
                          const int maxorder,
                          const int natmin,
                          const int ntran,
                          const int N,
                          const int N_new,
                          const int M,
                          const int M_test,
                          double **&u,
                          double **&f,
                          double **&u_test,
                          double **&f_test,
                          const Symmetry *symmetry,
                          const std::vector<std::string> &str_order,
                          const Fcs *fcs,
                          Constraint *constraint,
                          const unsigned int nat,
                          const int verbosity,
                          std::vector<double> &param_out)
{
    auto info_fitting = 0;
    int i, j;
    auto fnorm = 0.0;
    auto fnorm_test = 0.0;
    const auto ndata_used_test = nend_test - nstart_test + 1;

    std::vector<double> param_tmp(N_new);

    unsigned long nrows = 3 * static_cast<long>(natmin)
        * static_cast<long>(ndata_used)
        * static_cast<long>(ntran);

    const unsigned long ncols = static_cast<long>(N_new);

    const int scale_displacement
        = std::abs(optcontrol.displacement_scaling_factor - 1.0) > eps
        && optcontrol.standardize == 0;

    // Scale displacements if DNORM is not 1 and the data is not standardized.
    if (scale_displacement) {
        const auto inv_dnorm = 1.0 / optcontrol.displacement_scaling_factor;
        for (i = 0; i < ndata_used; ++i) {
            for (j = 0; j < 3 * nat; ++j) {
                u[i][j] *= inv_dnorm;
            }
        }
        // Scale force constants
        for (i = 0; i < maxorder; ++i) {
            const auto scale_factor = std::pow(optcontrol.displacement_scaling_factor, i + 1);
            for (j = 0; j < constraint->get_const_fix(i).size(); ++j) {
                const auto scaled_val = constraint->get_const_fix(i)[j].val_to_fix * scale_factor;
                constraint->set_const_fix_val_to_fix(i, j, scaled_val);
            }
        }
    }

    std::vector<double> amat_1D, amat_1D_test;
    std::vector<double> bvec, bvec_test;

    amat_1D.resize(nrows * ncols, 0.0);
    bvec.resize(nrows, 0.0);

    set_displacement_and_force(u,
                               f,
                               nat,
                               ndata_used);

    get_matrix_elements_algebraic_constraint(maxorder,
                                             ndata_used,
                                             &amat_1D[0],
                                             &bvec[0],
                                             fnorm,
                                             symmetry,
                                             fcs,
                                             constraint);

    if (u) {
        deallocate(u);
        u = nullptr;
    }

    if (f) {
        deallocate(f);
        f = nullptr;
    }

    if (optcontrol.cross_validation_mode == 1) {
        nrows = 3 * static_cast<long>(natmin)
            * static_cast<long>(ndata_used_test)
            * static_cast<long>(ntran);

        amat_1D_test.resize(nrows * ncols, 0.0);
        bvec_test.resize(nrows, 0.0);

        if (scale_displacement) {
            const auto inv_dnorm = 1.0 / optcontrol.displacement_scaling_factor;
            for (i = 0; i < ndata_used_test; ++i) {
                for (j = 0; j < 3 * nat; ++j) {
                    u_test[i][j] *= inv_dnorm;
                }
            }
        }

        set_displacement_and_force(u_test,
                                   f_test,
                                   nat,
                                   ndata_used_test);

        get_matrix_elements_algebraic_constraint(maxorder,
                                                 ndata_used_test,
                                                 &amat_1D_test[0],
                                                 &bvec_test[0],
                                                 fnorm_test,
                                                 symmetry,
                                                 fcs,
                                                 constraint);
        deallocate(u_test);
        u_test = nullptr;
        deallocate(f_test);
        f_test = nullptr;
    }

    // Scale back force constants

    if (scale_displacement) {
        for (i = 0; i < maxorder; ++i) {
            const auto scale_factor = 1.0 / std::pow(optcontrol.displacement_scaling_factor, i + 1);
            for (j = 0; j < constraint->get_const_fix(i).size(); ++j) {
                const auto scaled_val = constraint->get_const_fix(i)[j].val_to_fix * scale_factor;
                constraint->set_const_fix_val_to_fix(i, j, scaled_val);
            }
        }
    }

    if (optcontrol.cross_validation_mode > 0) {

        info_fitting = run_elastic_net_crossvalidation(job_prefix,
                                                       maxorder,
                                                       M,
                                                       M_test,
                                                       N_new,
                                                       amat_1D,
                                                       bvec,
                                                       fnorm,
                                                       amat_1D_test,
                                                       bvec_test,
                                                       fnorm_test,
                                                       constraint,
                                                       verbosity,
                                                       param_tmp);

    } else if (optcontrol.cross_validation_mode == 0) {

        // Optimize with a given L1 coefficient (l1_alpha)
        info_fitting = run_elastic_net_optimization(maxorder,
                                                    M,
                                                    N_new,
                                                    amat_1D,
                                                    bvec,
                                                    fnorm,
                                                    str_order,
                                                    verbosity,
                                                    param_tmp);

    }

    if (verbosity > 0 && info_fitting == 0) {
        auto iparam = 0;
        std::vector<int> nzero_lasso(maxorder);

        for (i = 0; i < maxorder; ++i) {
            nzero_lasso[i] = 0;
            for (const auto &it : constraint->get_index_bimap(i)) {
                const auto inew = it.left + iparam;
                if (std::abs(param_tmp[inew]) < eps) ++nzero_lasso[i];
            }
            iparam += constraint->get_index_bimap(i).size();
        }

        for (auto order = 0; order < maxorder; ++order) {
            std::cout << "  Number of non-zero " << std::setw(9) << str_order[order] << " FCs : "
                << constraint->get_index_bimap(order).size() - nzero_lasso[order] << std::endl;
        }
        std::cout << std::endl;
    }

    if (scale_displacement) {
        auto k = 0;
        for (i = 0; i < maxorder; ++i) {
            const auto scale_factor = 1.0 / std::pow(optcontrol.displacement_scaling_factor, i + 1);

            for (j = 0; j < constraint->get_index_bimap(i).size(); ++j) {
                param_tmp[k] *= scale_factor;
                ++k;
            }
        }
    }

    recover_original_forceconstants(maxorder,
                                    param_tmp,
                                    param_out,
                                    fcs->get_nequiv(),
                                    constraint);
    return info_fitting;
}

int Optimize::run_elastic_net_crossvalidation(const std::string job_prefix,
                                              const int maxorder,
                                              const int M,
                                              const int M_test,
                                              const int N_new,
                                              std::vector<double> &amat_1D,
                                              std::vector<double> &bvec,
                                              const double fnorm,
                                              std::vector<double> &amat_1D_test,
                                              std::vector<double> &bvec_test,
                                              const double fnorm_test,
                                              const Constraint *constraint,
                                              const int verbosity,
                                              std::vector<double> &param_out)
{
    // Cross-validation mode

    int initialize_mode;
    std::vector<int> nzero_lasso(maxorder);

    bool *has_prod;

    Eigen::MatrixXd Prod;
    Eigen::VectorXd grad0, grad, x;
    Eigen::VectorXd scale_beta, scale_beta_enet;
    Eigen::VectorXd factor_std;
    Eigen::VectorXd fdiff, fdiff_test;
    std::vector<double> params_tmp;

    // Coordinate descent

    Eigen::MatrixXd A = Eigen::Map<Eigen::MatrixXd>(&amat_1D[0], M, N_new);
    Eigen::VectorXd b = Eigen::Map<Eigen::VectorXd>(&bvec[0], M);
    Eigen::MatrixXd A_test = Eigen::Map<Eigen::MatrixXd>(&amat_1D_test[0], M_test, N_new);
    Eigen::VectorXd b_test = Eigen::Map<Eigen::VectorXd>(&bvec_test[0], M_test);

    Prod.setZero(N_new, N_new);
    grad0.resize(N_new);
    grad.resize(N_new);
    x.setZero(N_new);
    scale_beta.resize(N_new);
    scale_beta_enet.resize(N_new);
    factor_std.resize(N_new);
    fdiff.resize(M);
    fdiff_test.resize(M);

    allocate(has_prod, N_new);

    for (auto i = 0; i < N_new; ++i) {
        has_prod[i] = false;
    }

    if (verbosity > 0) {
        std::cout << "  Lasso validation with the following parameters:" << std::endl;
        std::cout << "   LASSO_MINALPHA = " << std::setw(15) << optcontrol.l1_alpha_min;
        std::cout << " LASSO_MAXALPHA = " << std::setw(15) << optcontrol.l1_alpha_max << std::endl;
        std::cout << "   LASSO_NALPHA = " << std::setw(5) << optcontrol.num_l1_alpha << std::endl;
        std::cout << "   LASSO_TOL = " << std::setw(15) << optcontrol.tolerance_iteration << std::endl;
        std::cout << "   LASSO_MAXITER = " << std::setw(5) << optcontrol.maxnum_iteration << std::endl;
        std::cout << "   LASSO_DBASIS = " << std::setw(15) << optcontrol.displacement_scaling_factor << std::endl;
        std::cout << std::endl;

        if (optcontrol.standardize) {
            std::cout << " STANDARDIZE = 1 : Standardization will be performed for matrix A and vector b." << std::endl;
            std::cout << "                   The LASSO_DNORM-tag will be neglected." << std::endl;
        } else {
            std::cout << " STANDARDIZE = 0 : No standardization of matrix A and vector b." << std::endl;
            std::cout << "                   Columns of matrix A will be scaled by the LASSO_DNORM value." << std::endl;
        }
    }

    std::ofstream ofs_cv, ofs_coef;

    auto file_cv = job_prefix + ".lasso_cv";
    auto file_coef = job_prefix + ".lasso_coef";
    ofs_cv.open(file_cv.c_str(), std::ios::out);

    ofs_cv << "# Algorithm : Coordinate descent" << std::endl;
    ofs_cv << "# LASSO_DBASIS = " << std::setw(15) << optcontrol.displacement_scaling_factor << std::endl;
    ofs_cv << "# LASSO_TOL = " << std::setw(15) << optcontrol.tolerance_iteration << std::endl;
    ofs_cv << "# L1 ALPHA, Fitting error, Validation error, Num. zero IFCs (2nd, 3rd, ...) " << std::endl;

    if (optcontrol.save_solution_path) {
        ofs_coef.open(file_coef.c_str(), std::ios::out);
        ofs_coef << "# L1 ALPHA, coefficients" << std::endl;
        params_tmp.resize(N_new);
    }

    if (optcontrol.standardize) {
        Eigen::VectorXd mean, dev;
        get_standardizer(A, mean, dev, factor_std, scale_beta);
        apply_standardizer(A, mean, dev);
        apply_standardizer(A_test, mean, dev);

    } else {
        Eigen::VectorXd mean, dev;
        get_standardizer(A, mean, dev, factor_std, scale_beta);
    }

    if (verbosity > 0) {
        std::cout << " Recommended LASSO_MAXALPHA = "
            << get_esimated_max_alpha(A, b) << std::endl << std::endl;
    }

    // Start iteration

    grad0 = A.transpose() * b;
    grad = grad0;

    for (auto ialpha = 0; ialpha <= optcontrol.num_l1_alpha; ++ialpha) {

        const auto l1_alpha = optcontrol.l1_alpha_min
            * std::pow(optcontrol.l1_alpha_max / optcontrol.l1_alpha_min,
                       static_cast<double>(optcontrol.num_l1_alpha - ialpha) /
                       static_cast<double>(optcontrol.num_l1_alpha));


        ofs_cv << std::setw(15) << l1_alpha;

        if (ialpha == 0) {
            initialize_mode = 0;
        } else {
            initialize_mode = 1;
        }

        for (auto i = 0; i < N_new; ++i) {
            scale_beta_enet(i) = 1.0 / (1.0 / scale_beta(i) + (1.0 - optcontrol.l1_ratio) * l1_alpha);
        }

        coordinate_descent(M, N_new, l1_alpha,
                           initialize_mode,
                           x, A, b, grad0, has_prod, Prod, grad, fnorm,
                           scale_beta_enet,
                           verbosity);

        fdiff = A * x - b;
        fdiff_test = A_test * x - b_test;
        const auto res1 = fdiff.dot(fdiff) / (fnorm * fnorm);
        const auto res2 = fdiff_test.dot(fdiff_test) / (fnorm_test * fnorm_test);

        // Count the number of zero parameters
        auto iparam = 0;

        for (auto i = 0; i < maxorder; ++i) {
            nzero_lasso[i] = 0;
            for (const auto &it : constraint->get_index_bimap(i)) {
                const auto inew = it.left + iparam;
                if (std::abs(x[inew]) < eps) ++nzero_lasso[i];

            }
            iparam += constraint->get_index_bimap(i).size();
        }

        ofs_cv << std::setw(15) << std::sqrt(res1);
        ofs_cv << std::setw(15) << std::sqrt(res2);
        for (auto i = 0; i < maxorder; ++i) {
            ofs_cv << std::setw(10) << nzero_lasso[i];
        }
        ofs_cv << std::endl;

        if (optcontrol.save_solution_path) {
            ofs_coef << std::setw(15) << l1_alpha;

            for (auto i = 0; i < N_new; ++i) params_tmp[i] = x[i];
            auto k = 0;
            for (auto i = 0; i < maxorder; ++i) {
                const auto scale_factor = 1.0 / std::pow(optcontrol.displacement_scaling_factor, i + 1);

                for (auto j = 0; j < constraint->get_index_bimap(i).size(); ++j) {
                    params_tmp[k] *= scale_factor * factor_std(k);
                    ++k;
                }
            }
            for (auto i = 0; i < N_new; ++i) {
                ofs_coef << std::setw(15) << params_tmp[i];
            }
            ofs_coef << std::endl;
        }
    }
    if (optcontrol.save_solution_path) {
        ofs_coef.close();
        params_tmp.clear();
        params_tmp.shrink_to_fit();
    }

    ofs_cv.close();

    deallocate(has_prod);

    return 1;
}


int Optimize::run_elastic_net_optimization(const int maxorder,
                                           const int M,
                                           const int N_new,
                                           std::vector<double> &amat_1D,
                                           std::vector<double> &bvec,
                                           const double fnorm,
                                           const std::vector<std::string> &str_order,
                                           const int verbosity,
                                           std::vector<double> &param_out)
{
    // Start Lasso optimization
    int i;
    bool *has_prod;

    Eigen::MatrixXd A, Prod;
    Eigen::VectorXd b, grad0, grad, x;
    Eigen::VectorXd scale_beta, factor_std;
    Eigen::VectorXd fdiff;

    // Coordinate descent

    A = Eigen::Map<Eigen::MatrixXd>(&amat_1D[0], M, N_new);
    b = Eigen::Map<Eigen::VectorXd>(&bvec[0], M);

    Prod.setZero(N_new, N_new);
    grad0.resize(N_new);
    grad.resize(N_new);
    x.setZero(N_new);
    scale_beta.resize(N_new);
    factor_std.resize(N_new);
    fdiff.resize(M);

    allocate(has_prod, N_new);

    for (i = 0; i < N_new; ++i) {
        has_prod[i] = false;
    }

    if (verbosity > 0) {
        std::cout << "  Lasso minimization with the following parameters:" << std::endl;
        std::cout << "   LASSO_ALPHA  (L1) = " << std::setw(15) << optcontrol.l1_alpha << std::endl;
        std::cout << "   LASSO_TOL = " << std::setw(15) << optcontrol.tolerance_iteration << std::endl;
        std::cout << "   LASSO_MAXITER = " << std::setw(5) << optcontrol.maxnum_iteration << std::endl;
        std::cout << "   LASSO_DBASIS = " << std::setw(15) << optcontrol.displacement_scaling_factor << std::endl;

        std::cout << std::endl;
        if (optcontrol.standardize) {
            std::cout << " STANDARDIZE = 1 : Standardization will be performed for matrix A and vector b." << std::endl;
            std::cout << "                   The LASSO_DNORM-tag will be neglected." << std::endl;
        } else {
            std::cout << " STANDARDIZE = 0 : No standardization of matrix A and vector b." << std::endl;
            std::cout << "                   Columns of matrix A will be scaled by the LASSO_DNORM value." << std::endl;
        }
    }

    // Standardize if necessary

    if (optcontrol.standardize) {
        Eigen::VectorXd mean, dev;
        get_standardizer(A, mean, dev, factor_std, scale_beta);
        apply_standardizer(A, mean, dev);
    } else {
        Eigen::VectorXd mean, dev;
        get_standardizer(A, mean, dev, factor_std, scale_beta);
    }

    grad0 = A.transpose() * b;
    grad = grad0;

    for (i = 0; i < N_new; ++i) {
        scale_beta(i) = 1.0 / (1.0 / scale_beta(i) + (1.0 - optcontrol.l1_ratio) * optcontrol.l1_alpha);
    }

    // Coordinate Descent Method
    coordinate_descent(M, N_new, optcontrol.l1_alpha,
                       0,
                       x, A, b, grad0, has_prod, Prod, grad, fnorm,
                       scale_beta,
                       verbosity);

    for (i = 0; i < N_new; ++i) {
        param_out[i] = x[i] * factor_std[i];
    }

    if (verbosity > 0) {
        fdiff = A * x - b;
        const auto res1 = fdiff.dot(fdiff) / (fnorm * fnorm);
        std::cout << "  RESIDUAL (%): " << std::sqrt(res1) * 100.0 << std::endl;
    }

    deallocate(has_prod);

    if (optcontrol.debiase_after_l1opt) {
        run_least_squares_with_nonzero_coefs(A, b,
                                             factor_std,
                                             param_out,
                                             verbosity);
    }

    return 0;
}

int Optimize::run_least_squares_with_nonzero_coefs(const Eigen::MatrixXd &A_in,
                                                   const Eigen::VectorXd &b_in,
                                                   const Eigen::VectorXd &factor_std,
                                                   std::vector<double> &params,
                                                   const int verbosity)
{
    // Perform OLS fitting to the features selected by LASSO for reducing the bias.

    if (verbosity > 0) {
        std::cout << " DEBIAS_OLS = 1: Attempt to reduce the bias of LASSO by performing OLS fitting" << std::endl;
        std::cout << "                 with features selected by LASSO." << std::endl;
    }

    const auto N_new = A_in.cols();
    const auto M = A_in.rows();

    std::vector<int> nonzero_index, zero_index;

    for (auto i = 0; i < N_new; ++i) {
        if (std::abs(params[i]) >= eps) {
            nonzero_index.push_back(i);
        } else {
            zero_index.push_back(i);
        }
    }

    const int N_nonzero = nonzero_index.size();
    Eigen::MatrixXd A_nonzero(M, N_nonzero);

    for (auto i = 0; i < N_nonzero; ++i) {
        A_nonzero.col(i) = A_in.col(nonzero_index[i]);
    }
    Eigen::VectorXd x_nonzero = A_nonzero.colPivHouseholderQr().solve(b_in);

    for (auto i = 0; i < N_new; ++i) params[i] = 0.0;
    for (auto i = 0; i < N_nonzero; ++i) {
        params[nonzero_index[i]] = x_nonzero[i] * factor_std[nonzero_index[i]];
    }

    return 0;
}


void Optimize::get_standardizer(const Eigen::MatrixXd &Amat,
                                Eigen::VectorXd &mean,
                                Eigen::VectorXd &dev,
                                Eigen::VectorXd &factor_std,
                                Eigen::VectorXd &scale_beta)
{
    const auto nrows = Amat.rows();
    const auto ncols = Amat.cols();

    if (mean.size() != ncols) mean.resize(ncols);
    if (dev.size() != ncols) dev.resize(ncols);
    if (factor_std.size() != ncols) factor_std.resize(ncols);
    if (scale_beta.size() != ncols) scale_beta.resize(ncols);

    const auto inv_nrows = 1.0 / static_cast<double>(nrows);
    double sum1, sum2;

    if (optcontrol.standardize) {
        for (auto j = 0; j < ncols; ++j) {
            sum1 = Amat.col(j).sum() * inv_nrows;
            sum2 = Amat.col(j).dot(Amat.col(j)) * inv_nrows;
            mean(j) = sum1;
            dev(j) = std::sqrt(sum2 - sum1 * sum1);
            factor_std(j) = 1.0 / dev(j);
            scale_beta(j) = 1.0;
        }
    } else {
        for (auto j = 0; j < ncols; ++j) {
            sum2 = Amat.col(j).dot(Amat.col(j)) * inv_nrows;
            mean(j) = 0.0;
            dev(j) = 1.0;
            factor_std(j) = 1.0;
            scale_beta(j) = 1.0 / sum2;
        }
    }
}

void Optimize::apply_standardizer(Eigen::MatrixXd &Amat,
                                  const Eigen::VectorXd &mean,
                                  const Eigen::VectorXd &dev)
{
    const auto ncols = Amat.cols();
    const auto nrows = Amat.rows();
    if (mean.size() != ncols || dev.size() != ncols) {
        exit("apply_standardizer", "The number of colums is inconsistent.");
    }

    for (auto i = 0; i < nrows; ++i) {
        for (auto j = 0; j < ncols; ++j) {
            Amat(i, j) = (Amat(i, j) - mean(j)) / dev(j);
        }
    }
}

double Optimize::get_esimated_max_alpha(const Eigen::MatrixXd &Amat,
                                        const Eigen::VectorXd &bvec) const
{
    const auto ncols = Amat.cols();
    Eigen::MatrixXd C = Amat.transpose() * bvec;
    auto lambda_max = 0.0;

    for (auto i = 0; i < ncols; ++i) {
        lambda_max = std::max<double>(lambda_max, std::abs(C(i)));
    }
    lambda_max /= static_cast<double>(Amat.rows());

    return lambda_max;
}


void Optimize::set_displacement_and_force(const double * const *disp_in,
                                          const double * const *force_in,
                                          const int nat,
                                          const int ndata_used_in)
{
    ndata_used = ndata_used_in;

    if (u_in) {
        deallocate(u_in);
    }
    allocate(u_in, ndata_used, 3 * nat);

    if (f_in) {
        deallocate(f_in);
    }
    allocate(f_in, ndata_used, 3 * nat);

    for (auto i = 0; i < ndata_used; i++) {
        for (auto j = 0; j < 3 * nat; j++) {
            u_in[i][j] = disp_in[i][j];
            f_in[i][j] = force_in[i][j];
        }
    }
}

void Optimize::set_fcs_values(const int maxorder,
                              double *fc_in,
                              std::vector<int> *nequiv,
                              const Constraint *constraint)
{
    // fc_in: irreducible set of force constants
    // fc_length: dimension of params (can differ from that of fc_in)

    int i;

    auto N = 0;
    auto Nirred = 0;
    for (i = 0; i < maxorder; ++i) {
        N += nequiv[i].size();
        Nirred += constraint->get_index_bimap(i).size();
    }

    std::vector<double> param_in(Nirred, 0.0);
    std::vector<double> param_out(N, 0.0);

    for (i = 0; i < Nirred; ++i) {
        param_in[i] = fc_in[i];
    }
    recover_original_forceconstants(maxorder,
                                    param_in,
                                    param_out,
                                    nequiv,
                                    constraint);
    if (params) {
        deallocate(params);
    }
    allocate(params, N);
    for (i = 0; i < N; ++i) {
        params[i] = param_out[i];
    }
}

int Optimize::get_ndata_used() const
{
    return ndata_used;
}


int Optimize::fit_without_constraints(const int N,
                                      const int M,
                                      double *amat,
                                      const double *bvec,
                                      double *param_out,
                                      const int verbosity) const
{
    int i;
    int nrhs = 1, nrank, INFO, M_tmp, N_tmp;
    auto rcond = -1.0;
    auto f_square = 0.0;
    double *WORK, *S, *fsum2;


    const auto LMIN = std::min<int>(M, N);
    auto LMAX = std::max<int>(M, N);

    auto LWORK = 3 * LMIN + std::max<int>(2 * LMIN, LMAX);
    LWORK = 2 * LWORK;

    if (verbosity > 0) {
        std::cout << "  Entering fitting routine: SVD without constraints" << std::endl;
    }


    allocate(WORK, LWORK);
    allocate(S, LMIN);
    allocate(fsum2, LMAX);

    for (i = 0; i < M; ++i) {
        fsum2[i] = bvec[i];
        f_square += std::pow(bvec[i], 2);
    }
    for (i = M; i < LMAX; ++i) fsum2[i] = 0.0;

    if (verbosity > 0) std::cout << "  SVD has started ... ";

    // Fitting with singular value decomposition
    // M_tmp and N_tmp are prepared to cast N and M to (non-const) int.
    M_tmp = M;
    N_tmp = N;
    dgelss_(&M_tmp, &N_tmp, &nrhs, amat, &M_tmp, fsum2, &LMAX,
            S, &rcond, &nrank, WORK, &LWORK, &INFO);

    if (verbosity > 0) {
        std::cout << "finished !" << std::endl << std::endl;
        std::cout << "  RANK of the matrix = " << nrank << std::endl;
    }

    if (nrank < N)
        warn("fit_without_constraints",
             "Matrix is rank-deficient. Force constants could not be determined uniquely :(");

    if (nrank == N && verbosity > 0) {
        auto f_residual = 0.0;
        for (i = N; i < M; ++i) {
            f_residual += std::pow(fsum2[i], 2);
        }
        std::cout << std::endl << "  Residual sum of squares for the solution: "
            << sqrt(f_residual) << std::endl;
        std::cout << "  Fitting error (%) : "
            << sqrt(f_residual / f_square) * 100.0 << std::endl;
    }

    for (i = 0; i < N; ++i) {
        param_out[i] = fsum2[i];
    }

    deallocate(WORK);
    deallocate(S);
    deallocate(fsum2);

    return INFO;
}

int Optimize::fit_with_constraints(const int N,
                                   const int M,
                                   const int P,
                                   double *amat,
                                   const double *bvec,
                                   double *param_out,
                                   const double * const *cmat,
                                   double *dvec,
                                   const int verbosity) const
{
    int i, j;
    int N_tmp, M_tmp, P_tmp;
    double *fsum2;
    double *mat_tmp;

    if (verbosity > 0) {
        std::cout << "  Entering fitting routine: QRD with constraints" << std::endl;
    }

    allocate(fsum2, M);
    allocate(mat_tmp, (M + P) * N);

    unsigned long k = 0;
    unsigned long l = 0;

    // Concatenate two matrices as 1D array
    for (j = 0; j < N; ++j) {
        for (i = 0; i < M; ++i) {
            mat_tmp[k++] = amat[l++];
        }
        for (i = 0; i < P; ++i) {
            mat_tmp[k++] = cmat[i][j];
        }
    }

    const auto nrank = rankQRD((M + P), N, mat_tmp, eps12);
    deallocate(mat_tmp);

    if (nrank != N) {
        std::cout << std::endl;
        std::cout << " **************************************************************************" << std::endl;
        std::cout << "  WARNING : rank deficient.                                                " << std::endl;
        std::cout << "  rank ( (A) ) ! = N            A: Fitting matrix     B: Constraint matrix " << std::endl;
        std::cout << "       ( (B) )                  N: The number of parameters                " << std::endl;
        std::cout << "  rank = " << nrank << " N = " << N << std::endl << std::endl;
        std::cout << "  This can cause a difficulty in solving the fitting problem properly      " << std::endl;
        std::cout << "  with DGGLSE, especially when the difference is large. Please check if    " << std::endl;
        std::cout << "  you obtain reliable force constants in the .fcs file.                    " << std::endl << std::
            endl;
        std::cout << "  You may need to reduce the cutoff radii and/or increase NDATA            " << std::endl;
        std::cout << "  by giving linearly-independent displacement patterns.                    " << std::endl;
        std::cout << " **************************************************************************" << std::endl;
        std::cout << std::endl;
    }

    auto f_square = 0.0;
    for (i = 0; i < M; ++i) {
        fsum2[i] = bvec[i];
        f_square += std::pow(bvec[i], 2);
    }
    if (verbosity > 0) std::cout << "  QR-Decomposition has started ...";

    double *cmat_mod;
    allocate(cmat_mod, P * N);

    k = 0;
    for (j = 0; j < N; ++j) {
        for (i = 0; i < P; ++i) {
            cmat_mod[k++] = cmat[i][j];
        }
    }

    // Fitting

    auto LWORK = P + std::min<int>(M, N) + 10 * std::max<int>(M, N);
    int INFO;
    double *WORK, *x;
    allocate(WORK, LWORK);
    allocate(x, N);

    // M_tmp, N_tmp, P_tmp are prepared to cast N, M, P to (non-const)
    // int.
    M_tmp = M;
    N_tmp = N;
    P_tmp = P;
    dgglse_(&M_tmp, &N_tmp, &P_tmp, amat, &M_tmp, cmat_mod, &P_tmp,
            fsum2, dvec, x, WORK, &LWORK, &INFO);

    if (verbosity > 0) std::cout << " finished. " << std::endl;

    auto f_residual = 0.0;
    for (i = N - P; i < M; ++i) {
        f_residual += std::pow(fsum2[i], 2);
    }

    if (verbosity > 0) {
        std::cout << std::endl << "  Residual sum of squares for the solution: "
            << sqrt(f_residual) << std::endl;
        std::cout << "  Fitting error (%) : "
            << std::sqrt(f_residual / f_square) * 100.0 << std::endl;
    }

    // copy fcs to bvec
    for (i = 0; i < N; ++i) {
        param_out[i] = x[i];
    }

    deallocate(cmat_mod);
    deallocate(WORK);
    deallocate(x);
    deallocate(fsum2);

    return INFO;
}

int Optimize::fit_algebraic_constraints(const int N,
                                        const int M,
                                        double *amat,
                                        const double *bvec,
                                        std::vector<double> &param_out,
                                        const double fnorm,
                                        const int maxorder,
                                        const Fcs *fcs,
                                        const Constraint *constraint,
                                        const int verbosity) const
{
    int i;
    int nrhs = 1, nrank, INFO, M_tmp, N_tmp;
    auto rcond = -1.0;
    double *WORK, *S, *fsum2;

    if (verbosity > 0) {
        std::cout << "  Entering fitting routine: SVD with constraints considered algebraically." << std::endl;
    }

    auto LMIN = std::min<int>(M, N);
    auto LMAX = std::max<int>(M, N);

    auto LWORK = 3 * LMIN + std::max<int>(2 * LMIN, LMAX);
    LWORK = 2 * LWORK;

    allocate(WORK, LWORK);
    allocate(S, LMIN);
    allocate(fsum2, LMAX);

    for (i = 0; i < M; ++i) {
        fsum2[i] = bvec[i];
    }
    for (i = M; i < LMAX; ++i) fsum2[i] = 0.0;

    if (verbosity > 0) std::cout << "  SVD has started ... ";

    // Fitting with singular value decomposition
    // M_tmp and N_tmp are prepared to cast N and M to (non-const) int.
    M_tmp = M;
    N_tmp = N;
    dgelss_(&M_tmp, &N_tmp, &nrhs, amat, &M_tmp, fsum2, &LMAX,
            S, &rcond, &nrank, WORK, &LWORK, &INFO);

    deallocate(WORK);
    deallocate(S);

    if (verbosity > 0) {
        std::cout << "finished !" << std::endl << std::endl;
        std::cout << "  RANK of the matrix = " << nrank << std::endl;
    }

    if (nrank < N) {
        warn("fit_without_constraints",
             "Matrix is rank-deficient. Force constants could not be determined uniquely :(");
    }

    if (nrank == N && verbosity > 0) {
        auto f_residual = 0.0;
        for (i = N; i < M; ++i) {
            f_residual += std::pow(fsum2[i], 2);
        }
        std::cout << std::endl;
        std::cout << "  Residual sum of squares for the solution: "
            << sqrt(f_residual) << std::endl;
        std::cout << "  Fitting error (%) : "
            << sqrt(f_residual / (fnorm * fnorm)) * 100.0 << std::endl;
    }

    if (INFO == 0) {
        std::vector<double> param_irred(N, 0.0);
        for (i = 0; i < LMIN; ++i) param_irred[i] = fsum2[i];
        deallocate(fsum2);

        // Recover reducible set of force constants

        recover_original_forceconstants(maxorder,
                                        param_irred,
                                        param_out,
                                        fcs->get_nequiv(),
                                        constraint);
    }

    return INFO;
}


void Optimize::get_matrix_elements(const int maxorder,
                                   const int ndata_fit,
                                   double *amat,
                                   double *bvec,
                                   const Symmetry *symmetry,
                                   const Fcs *fcs) const
{
    int i, j;
    long irow;

    std::vector<std::vector<double>> u_multi, f_multi;

    data_multiplier(u_in, u_multi, ndata_fit, symmetry);
    data_multiplier(f_in, f_multi, ndata_fit, symmetry);

    const int natmin = symmetry->get_nat_prim();
    const int natmin3 = 3 * natmin;
    auto ncols = 0;

    for (i = 0; i < maxorder; ++i) ncols += fcs->get_nequiv()[i].size();

    const long ncycle = static_cast<long>(ndata_fit) * symmetry->get_ntran();

#ifdef _OPENMP
#pragma omp parallel private(irow, i, j)
#endif
    {
        int *ind;
        int mm, order, iat, k;
        int im, iparam;
        long idata;
        double amat_tmp;
        double **amat_orig_tmp;

        allocate(ind, maxorder + 1);
        allocate(amat_orig_tmp, natmin3, ncols);

#ifdef _OPENMP
#pragma omp for schedule(guided)
#endif
        for (irow = 0; irow < ncycle; ++irow) {

            // generate r.h.s vector B
            for (i = 0; i < natmin; ++i) {
                iat = symmetry->get_map_p2s()[i][0];
                for (j = 0; j < 3; ++j) {
                    im = 3 * i + j + natmin3 * irow;
                    bvec[im] = f_multi[irow][3 * iat + j];
                }
            }

            for (i = 0; i < natmin3; ++i) {
                for (j = 0; j < ncols; ++j) {
                    amat_orig_tmp[i][j] = 0.0;
                }
            }

            // generate l.h.s. matrix A

            idata = natmin3 * irow;
            iparam = 0;

            for (order = 0; order < maxorder; ++order) {

                mm = 0;

                for (const auto &iter : fcs->get_nequiv()[order]) {
                    for (i = 0; i < iter; ++i) {
                        ind[0] = fcs->get_fc_table()[order][mm].elems[0];
                        k = inprim_index(ind[0], symmetry);
                        amat_tmp = 1.0;
                        for (j = 1; j < order + 2; ++j) {
                            ind[j] = fcs->get_fc_table()[order][mm].elems[j];
                            amat_tmp *= u_multi[irow][fcs->get_fc_table()[order][mm].elems[j]];
                        }
                        amat_orig_tmp[k][iparam] -= gamma(order + 2, ind) * fcs->get_fc_table()[order][mm].sign *
                            amat_tmp;
                        ++mm;
                    }
                    ++iparam;
                }
            }

            for (i = 0; i < natmin3; ++i) {
                for (j = 0; j < ncols; ++j) {
                    // Transpose here for later use of lapack without transpose
                    amat[natmin3 * ncycle * j + i + idata] = amat_orig_tmp[i][j];
                }
            }
        }

        deallocate(ind);
        deallocate(amat_orig_tmp);
    }

    u_multi.clear();
    f_multi.clear();
}


void Optimize::get_matrix_elements_algebraic_constraint(const int maxorder,
                                                        const int ndata_fit,
                                                        double *amat,
                                                        double *bvec,
                                                        double &fnorm,
                                                        const Symmetry *symmetry,
                                                        const Fcs *fcs,
                                                        const Constraint *constraint) const
{
    int i, j;
    long irow;

    std::vector<std::vector<double>> u_multi, f_multi;

    data_multiplier(u_in, u_multi, ndata_fit, symmetry);
    data_multiplier(f_in, f_multi, ndata_fit, symmetry);

    const int natmin = symmetry->get_nat_prim();
    const auto natmin3 = 3 * natmin;
    const int nrows = natmin3 * ndata_fit * symmetry->get_ntran();
    auto ncols = 0;
    auto ncols_new = 0;

    for (i = 0; i < maxorder; ++i) {
        ncols += fcs->get_nequiv()[i].size();
        ncols_new += constraint->get_index_bimap(i).size();
    }

    const auto ncycle = static_cast<long>(ndata_fit) * symmetry->get_ntran();

    std::vector<double> bvec_orig(nrows, 0.0);


#ifdef _OPENMP
#pragma omp parallel private(irow, i, j)
#endif
    {
        int *ind;
        int mm, order, iat, k;
        int im, iparam;
        long idata;
        int ishift;
        int iold, inew;
        double amat_tmp;
        double **amat_orig_tmp;
        double **amat_mod_tmp;

        allocate(ind, maxorder + 1);
        allocate(amat_orig_tmp, natmin3, ncols);
        allocate(amat_mod_tmp, natmin3, ncols_new);

#ifdef _OPENMP
#pragma omp for schedule(guided)
#endif
        for (irow = 0; irow < ncycle; ++irow) {

            // generate r.h.s vector B
            for (i = 0; i < natmin; ++i) {
                iat = symmetry->get_map_p2s()[i][0];
                for (j = 0; j < 3; ++j) {
                    im = 3 * i + j + natmin3 * irow;
                    bvec[im] = f_multi[irow][3 * iat + j];
                    bvec_orig[im] = f_multi[irow][3 * iat + j];
                }
            }

            for (i = 0; i < natmin3; ++i) {
                for (j = 0; j < ncols; ++j) {
                    amat_orig_tmp[i][j] = 0.0;
                }
                for (j = 0; j < ncols_new; ++j) {
                    amat_mod_tmp[i][j] = 0.0;
                }
            }

            // generate l.h.s. matrix A

            idata = natmin3 * irow;
            iparam = 0;

            for (order = 0; order < maxorder; ++order) {

                mm = 0;

                for (const auto &iter : fcs->get_nequiv()[order]) {
                    for (i = 0; i < iter; ++i) {
                        ind[0] = fcs->get_fc_table()[order][mm].elems[0];
                        k = inprim_index(ind[0], symmetry);

                        amat_tmp = 1.0;
                        for (j = 1; j < order + 2; ++j) {
                            ind[j] = fcs->get_fc_table()[order][mm].elems[j];
                            amat_tmp *= u_multi[irow][fcs->get_fc_table()[order][mm].elems[j]];
                        }
                        amat_orig_tmp[k][iparam] -= gamma(order + 2, ind)
                            * fcs->get_fc_table()[order][mm].sign * amat_tmp;
                        ++mm;
                    }
                    ++iparam;
                }
            }

            // Convert the full matrix and vector into a smaller irreducible form
            // by using constraint information.

            ishift = 0;
            iparam = 0;

            for (order = 0; order < maxorder; ++order) {

                for (i = 0; i < constraint->get_const_fix(order).size(); ++i) {

                    for (j = 0; j < natmin3; ++j) {
                        bvec[j + idata] -= constraint->get_const_fix(order)[i].val_to_fix
                            * amat_orig_tmp[j][ishift + constraint->get_const_fix(order)[i].p_index_target];
                    }
                }

                //                std::cout << "pass const_fix" << std::endl;

                for (const auto &it : constraint->get_index_bimap(order)) {
                    inew = it.left + iparam;
                    iold = it.right + ishift;

                    for (j = 0; j < natmin3; ++j) {
                        amat_mod_tmp[j][inew] = amat_orig_tmp[j][iold];
                    }
                }

                for (i = 0; i < constraint->get_const_relate(order).size(); ++i) {

                    iold = constraint->get_const_relate(order)[i].p_index_target + ishift;

                    for (j = 0; j < constraint->get_const_relate(order)[i].alpha.size(); ++j) {

                        inew = constraint->get_index_bimap(order).right.at(
                                constraint->get_const_relate(order)[i].p_index_orig[j]) +
                            iparam;

                        for (k = 0; k < natmin3; ++k) {
                            amat_mod_tmp[k][inew] -= amat_orig_tmp[k][iold]
                                * constraint->get_const_relate(order)[i].alpha[j];
                        }
                    }
                }

                ishift += fcs->get_nequiv()[order].size();
                iparam += constraint->get_index_bimap(order).size();
            }

            for (i = 0; i < natmin3; ++i) {
                for (j = 0; j < ncols_new; ++j) {
                    // Transpose here for later use of lapack without transpose
                    amat[natmin3 * ncycle * j + i + idata] = amat_mod_tmp[i][j];
                }
            }
        }

        deallocate(ind);
        deallocate(amat_orig_tmp);
        deallocate(amat_mod_tmp);
    }

    fnorm = 0.0;
    for (i = 0; i < bvec_orig.size(); ++i) {
        fnorm += bvec_orig[i] * bvec_orig[i];
    }
    fnorm = std::sqrt(fnorm);
}

#ifdef WITH_SPARSE_SOLVER
void Optimize::get_matrix_elements_in_sparse_form(const int maxorder,
                                                  const int ndata_fit,
                                                  SpMat &sp_amat,
                                                  Eigen::VectorXd &sp_bvec,
                                                  double &fnorm,
                                                  const Symmetry *symmetry,
                                                  const Fcs *fcs,
                                                  const Constraint *constraint)
{
    int i, j;
    long irow;
    typedef Eigen::Triplet<double> T;
    std::vector<T> nonzero_entries;
    std::vector<std::vector<double>> u_multi, f_multi;

    data_multiplier(u_in, u_multi, ndata_fit, symmetry);
    data_multiplier(f_in, f_multi, ndata_fit, symmetry);

    const int natmin = symmetry->get_nat_prim();
    const auto natmin3 = 3 * natmin;
    const int nrows = natmin3 * ndata_fit * symmetry->get_ntran();
    auto ncols = 0;
    auto ncols_new = 0;

    for (i = 0; i < maxorder; ++i) {
        ncols += fcs->get_nequiv()[i].size();
        ncols_new += constraint->get_index_bimap(i).size();
    }

    const auto ncycle = static_cast<long>(ndata_fit) * symmetry->get_ntran();

    std::vector<double> bvec_orig(nrows, 0.0);


#ifdef _OPENMP
#pragma omp parallel private(irow, i, j)
#endif
    {
        int *ind;
        int mm, order, iat, k;
        int im, iparam;
        long idata;
        int ishift;
        int iold, inew;
        double amat_tmp;
        double **amat_orig_tmp;
        double **amat_mod_tmp;

        std::vector<T> nonzero_omp;

        allocate(ind, maxorder + 1);
        allocate(amat_orig_tmp, natmin3, ncols);
        allocate(amat_mod_tmp, natmin3, ncols_new);

#ifdef _OPENMP
#pragma omp for schedule(guided)
#endif
        for (irow = 0; irow < ncycle; ++irow) {

            // generate r.h.s vector B
            for (i = 0; i < natmin; ++i) {
                iat = symmetry->get_map_p2s()[i][0];
                for (j = 0; j < 3; ++j) {
                    im = 3 * i + j + natmin3 * irow;
                    sp_bvec(im) = f_multi[irow][3 * iat + j];
                    bvec_orig[im] = f_multi[irow][3 * iat + j];
                }
            }

            for (i = 0; i < natmin3; ++i) {
                for (j = 0; j < ncols; ++j) {
                    amat_orig_tmp[i][j] = 0.0;
                }
                for (j = 0; j < ncols_new; ++j) {
                    amat_mod_tmp[i][j] = 0.0;
                }
            }

            // generate l.h.s. matrix A

            idata = natmin3 * irow;
            iparam = 0;

            for (order = 0; order < maxorder; ++order) {

                mm = 0;

                for (const auto &iter : fcs->get_nequiv()[order]) {
                    for (i = 0; i < iter; ++i) {
                        ind[0] = fcs->get_fc_table()[order][mm].elems[0];
                        k = inprim_index(ind[0], symmetry);

                        amat_tmp = 1.0;
                        for (j = 1; j < order + 2; ++j) {
                            ind[j] = fcs->get_fc_table()[order][mm].elems[j];
                            amat_tmp *= u_multi[irow][fcs->get_fc_table()[order][mm].elems[j]];
                        }
                        amat_orig_tmp[k][iparam] -= gamma(order + 2, ind)
                            * fcs->get_fc_table()[order][mm].sign * amat_tmp;
                        ++mm;
                    }
                    ++iparam;
                }
            }

            // Convert the full matrix and vector into a smaller irreducible form
            // by using constraint information.

            ishift = 0;
            iparam = 0;

            for (order = 0; order < maxorder; ++order) {

                for (i = 0; i < constraint->get_const_fix(order).size(); ++i) {

                    for (j = 0; j < natmin3; ++j) {
                        sp_bvec(j + idata) -= constraint->get_const_fix(order)[i].val_to_fix
                            * amat_orig_tmp[j][ishift + constraint->get_const_fix(order)[i].p_index_target];
                    }
                }

                for (const auto &it : constraint->get_index_bimap(order)) {
                    inew = it.left + iparam;
                    iold = it.right + ishift;

                    for (j = 0; j < natmin3; ++j) {
                        amat_mod_tmp[j][inew] = amat_orig_tmp[j][iold];
                    }
                }

                for (i = 0; i < constraint->get_const_relate(order).size(); ++i) {

                    iold = constraint->get_const_relate(order)[i].p_index_target + ishift;

                    for (j = 0; j < constraint->get_const_relate(order)[i].alpha.size(); ++j) {

                        inew = constraint->get_index_bimap(order).right.at(
                                constraint->get_const_relate(order)[i].p_index_orig[j]) +
                            iparam;

                        for (k = 0; k < natmin3; ++k) {
                            amat_mod_tmp[k][inew] -= amat_orig_tmp[k][iold]
                                * constraint->get_const_relate(order)[i].alpha[j];
                        }
                    }
                }

                ishift += fcs->get_nequiv()[order].size();
                iparam += constraint->get_index_bimap(order).size();
            }

            for (i = 0; i < natmin3; ++i) {
                for (j = 0; j < ncols_new; ++j) {
                    if (std::abs(amat_mod_tmp[i][j]) > eps) {
                        nonzero_omp.emplace_back(T(idata + i, j, amat_mod_tmp[i][j]));
                    }
                }
            }
        }

        deallocate(ind);
        deallocate(amat_orig_tmp);
        deallocate(amat_mod_tmp);

#pragma omp critical
        {
            for (const auto &it : nonzero_omp) {
                nonzero_entries.emplace_back(it);
            }
        }
    }

    fnorm = 0.0;
    for (i = 0; i < bvec_orig.size(); ++i) {
        fnorm += bvec_orig[i] * bvec_orig[i];
    }
    fnorm = std::sqrt(fnorm);
    sp_amat.setFromTriplets(nonzero_entries.begin(), nonzero_entries.end());
    sp_amat.makeCompressed();
}
#endif


void Optimize::recover_original_forceconstants(const int maxorder,
                                               const std::vector<double> &param_in,
                                               std::vector<double> &param_out,
                                               const std::vector<int> *nequiv,
                                               const Constraint *constraint) const
{
    // Expand the given force constants into the larger sets
    // by using the constraint matrix.

    int i, j, k;
    auto ishift = 0;
    auto iparam = 0;
    double tmp;
    int inew, iold;

    unsigned int nparams = 0;

    for (i = 0; i < maxorder; ++i) nparams += nequiv[i].size();

    param_out.resize(nparams, 0.0);

    for (i = 0; i < maxorder; ++i) {
        for (j = 0; j < constraint->get_const_fix(i).size(); ++j) {
            param_out[constraint->get_const_fix(i)[j].p_index_target + ishift]
                = constraint->get_const_fix(i)[j].val_to_fix;
        }

        for (const auto &it : constraint->get_index_bimap(i)) {
            inew = it.left + iparam;
            iold = it.right + ishift;

            param_out[iold] = param_in[inew];
        }

        for (j = 0; j < constraint->get_const_relate(i).size(); ++j) {
            tmp = 0.0;

            for (k = 0; k < constraint->get_const_relate(i)[j].alpha.size(); ++k) {
                tmp += constraint->get_const_relate(i)[j].alpha[k]
                    * param_out[constraint->get_const_relate(i)[j].p_index_orig[k] + ishift];
            }
            param_out[constraint->get_const_relate(i)[j].p_index_target + ishift] = -tmp;
        }

        ishift += nequiv[i].size();
        iparam += constraint->get_index_bimap(i).size();
    }
}


void Optimize::data_multiplier(const double * const *data_in,
                               std::vector<std::vector<double>> &data_out,
                               const int ndata_used,
                               const Symmetry *symmetry) const
{
    const int nat = symmetry->get_nat_prim() * symmetry->get_ntran();

    auto idata = 0;
    for (auto i = 0; i < ndata_used; ++i) {
        std::vector<double> data_tmp(3 * nat, 0.0);

        for (auto itran = 0; itran < symmetry->get_ntran(); ++itran) {
            for (auto j = 0; j < nat; ++j) {
                const auto n_mapped = symmetry->get_map_sym()[j][symmetry->get_symnum_tran()[itran]];
                for (auto k = 0; k < 3; ++k) {
                    data_tmp[3 * n_mapped + k] = data_in[i][3 * j + k];
                }
            }
            data_out.emplace_back(data_tmp);
            ++idata;
        }
    }
}

int Optimize::inprim_index(const int n,
                           const Symmetry *symmetry) const
{
    auto in = -1;
    const auto atmn = n / 3;
    const auto crdn = n % 3;

    for (auto i = 0; i < symmetry->get_nat_prim(); ++i) {
        if (symmetry->get_map_p2s()[i][0] == atmn) {
            in = 3 * i + crdn;
            break;
        }
    }
    return in;
}

double Optimize::gamma(const int n,
                       const int *arr) const
{
    int *arr_tmp, *nsame;
    int i;

    allocate(arr_tmp, n);
    allocate(nsame, n);

    for (i = 0; i < n; ++i) {
        arr_tmp[i] = arr[i];
        nsame[i] = 0;
    }

    const auto ind_front = arr[0];
    auto nsame_to_front = 1;

    insort(n, arr_tmp);

    auto nuniq = 1;
    auto iuniq = 0;

    nsame[0] = 1;

    for (i = 1; i < n; ++i) {
        if (arr_tmp[i] == arr_tmp[i - 1]) {
            ++nsame[iuniq];
        } else {
            ++nsame[++iuniq];
            ++nuniq;
        }

        if (arr[i] == ind_front) ++nsame_to_front;
    }

    auto denom = 1;

    for (i = 0; i < nuniq; ++i) {
        denom *= factorial(nsame[i]);
    }

    deallocate(arr_tmp);
    deallocate(nsame);

    return static_cast<double>(nsame_to_front) / static_cast<double>(denom);
}

int Optimize::get_ndata() const
{
    return ndata;
}

void Optimize::set_ndata(const int ndata_in)
{
    ndata = ndata_in;
}

int Optimize::get_nstart() const
{
    return nstart;
}

void Optimize::set_nstart(const int nstart_in)
{
    nstart = nstart_in;
}

int Optimize::get_nend() const
{
    return nend;
}

void Optimize::set_nend(const int nend_in)
{
    nend = nend_in;
}

int Optimize::get_skip_s() const
{
    return skip_s;
}

void Optimize::set_skip_s(const int skip_s_in)
{
    skip_s = skip_s_in;
}

int Optimize::get_skip_e() const
{
    return skip_e;
}

void Optimize::set_skip_e(const int skip_e_in)
{
    skip_e = skip_e_in;
}

double* Optimize::get_params() const
{
    return params;
}

int Optimize::factorial(const int n) const
{
    if (n == 1 || n == 0) {
        return 1;
    }
    return n * factorial(n - 1);
}


int Optimize::rankQRD(const int m,
                      const int n,
                      double *mat,
                      const double tolerance) const
{
    // Return the rank of matrix mat revealed by the column pivoting QR decomposition
    // The matrix mat is destroyed.

    auto m_ = m;
    auto n_ = n;

    auto LDA = m_;

    auto LWORK = 10 * n_;
    int INFO;
    int *JPVT;
    double *WORK, *TAU;

    const auto nmin = std::min<int>(m_, n_);

    allocate(JPVT, n_);
    allocate(WORK, LWORK);
    allocate(TAU, nmin);

    for (auto i = 0; i < n_; ++i) JPVT[i] = 0;

    dgeqp3_(&m_, &n_, mat, &LDA, JPVT, TAU, WORK, &LWORK, &INFO);

    deallocate(JPVT);
    deallocate(WORK);
    deallocate(TAU);

    if (std::abs(mat[0]) < eps) return 0;

    double **mat_tmp;
    allocate(mat_tmp, m_, n_);

    unsigned long k = 0;

    for (auto j = 0; j < n_; ++j) {
        for (auto i = 0; i < m_; ++i) {
            mat_tmp[i][j] = mat[k++];
        }
    }

    auto nrank = 0;
    for (auto i = 0; i < nmin; ++i) {
        if (std::abs(mat_tmp[i][i]) > tolerance * std::abs(mat[0])) ++nrank;
    }

    deallocate(mat_tmp);

    return nrank;
}


#ifdef WITH_SPARSE_SOLVER
int Optimize::run_eigen_sparseQR(const SpMat &sp_mat,
                                 const Eigen::VectorXd &sp_bvec,
                                 std::vector<double> &param_out,
                                 const double fnorm,
                                 const int maxorder,
                                 const Fcs *fcs,
                                 const Constraint *constraint,
                                 const int verbosity)
{
    //    Eigen::BenchTimer t;

    if (verbosity > 0) {
        std::cout << "  Solve least-squares problem by sparse LDLT." << std::endl;
    }

    SpMat AtA = sp_mat.transpose() * sp_mat;
    Eigen::VectorXd AtB, x;
    AtB = sp_mat.transpose() * sp_bvec;

    /*
        t.reset();
        t.start();
        Eigen::SparseQR<Eigen::SparseMatrix<double>, Eigen::COLAMDOrdering<int>> qr(sp_mat);
        x = qr.solve(sp_bvec);
    
        t.stop();
        std::cout << "sqr   : " << qr.info() << " ; " << t.value() 
        << "s ;  err: " << (AtA * x - AtB).norm() / AtB.norm() << "\n";
    
        t.reset();
        t.start();
    */
    Eigen::SimplicialLDLT<SpMat> ldlt(AtA);
    x = ldlt.solve(AtB);
    //       t.stop();
    //   std::cout << "ldlt  : " << ldlt.info() << " ; " << t.value() << "s ;  err: " << (AtA*x-AtB).norm() / AtB.norm() << "\n";

    /*
         t.reset(); t.start();
         Eigen::ConjugateGradient<SpMat> cg(AtA);
         cg.setTolerance(optcontrol.tolerance_iteration);
         cg.setMaxIterations(optcontrol.maxnum_iteration);
         x.setZero(); x = cg.solve(AtB);
         t.stop();
         std::cout << "cg    : " << cg.info() << " ; " << t.value() << "s ;  err: " << (AtA*x-AtB).norm() / AtB.norm() << "\n";
    */
    /*   t.reset(); t.start();
       Eigen::LeastSquaresConjugateGradient<SpMat> lscg(sp_mat);
       lscg.setTolerance(eps10);
       lscg.setMaxIterations(10000000);
       x.setZero(); x = lscg.solve(sp_bvec);
  
       t.stop();
       std::cout << "lscg  : " << lscg.info() << " ; " << t.value() << "s ;  err: " << (AtA*x-AtB).norm() / AtB.norm() << "\n";*/

    /*
         t.reset(); t.start();
         Eigen::BiCGSTAB<SpMat> bicg(AtA);
         bicg.setTolerance(optcontrol.tolerance_iteration);
         bicg.setMaxIterations(optcontrol.maxnum_iteration);
         x.setZero(); x = bicg.solve(AtB);
         t.stop();
         std::cout << "bicg    : " << bicg.info() << " ; " << t.value() << "s ;  err: " << (AtA*x-AtB).norm() / AtB.norm() << "\n";
    
    */
    auto res = sp_bvec - sp_mat * x;
    auto res2norm = res.squaredNorm();
    auto nparams = x.size();
    std::vector<double> param_irred(nparams);

    for (auto i = 0; i < nparams; ++i) {
        param_irred[i] = x(i);
    }

    if (ldlt.info() == Eigen::Success) {
        // Recover reducible set of force constants

        recover_original_forceconstants(maxorder,
                                        param_irred,
                                        param_out,
                                        fcs->get_nequiv(),
                                        constraint);

        if (verbosity > 0) {
            std::cout << "  Residual sum of squares for the solution: "
                << sqrt(res2norm) << std::endl;
            std::cout << "  Fitting error (%) : "
                << sqrt(res2norm / (fnorm * fnorm)) * 100.0 << std::endl;
        }

        return 0;

    } else {

        std::cerr << "  Fitting by LDLT failed." << std::endl;
        std::cerr << ldlt.info() << std::endl;

        return 1;
    }
}

#endif


void Optimize::set_optimizer_control(const OptimizerControl &optcontrol_in)
{
    // Check the validity of the options before copying it.

    if (optcontrol_in.cross_validation_mode < 0 || optcontrol_in.cross_validation_mode > 1) {
        exit("set_optimizer_control", "cross_validation_mode must be 0 or 1");
    }
    if (optcontrol_in.optimizer == 2) {
        if (optcontrol_in.l1_ratio <= eps || optcontrol_in.l1_ratio > 1.0) {
            exit("set_optimizer_control", "L1_RATIO must be 0 < L1_RATIO <= 1.");
        }

        if (optcontrol_in.cross_validation_mode == 1) {
            if (optcontrol_in.l1_alpha_min >= optcontrol_in.l1_alpha_max) {
                exit("set_optimizer_control", "L1_ALPHA_MIN must be smaller than L1_ALPHA_MAX.");
            }
        }
    }

    optcontrol = optcontrol_in;
}

OptimizerControl Optimize::get_optimizer_control() const
{
    return optcontrol;
}


void Optimize::coordinate_descent(const int M,
                                  const int N,
                                  const double alpha,
                                  const int warm_start,
                                  Eigen::VectorXd &x,
                                  const Eigen::MatrixXd &A,
                                  const Eigen::VectorXd &b,
                                  const Eigen::VectorXd &grad0,
                                  bool *has_prod,
                                  Eigen::MatrixXd &Prod,
                                  Eigen::VectorXd &grad,
                                  const double fnorm,
                                  const Eigen::VectorXd &scale_beta,
                                  const int verbosity) const
{
    int i, j;
    double diff;
    Eigen::VectorXd beta(N), delta(N);
    Eigen::VectorXd res(N);
    bool do_print_log;

    if (warm_start) {
        for (i = 0; i < N; ++i) beta(i) = x(i);
    } else {
        for (i = 0; i < N; ++i) beta(i) = 0.0;
        grad = grad0;
    }

    if (verbosity > 0) {
        std::cout << "-----------------------------------------------------------------" << std::endl;
        std::cout << "  L1_ALPHA = " << std::setw(15) << alpha << std::endl;
    }

    const auto Minv = 1.0 / static_cast<double>(M);
    const auto alphlambda = alpha * optcontrol.l1_ratio;

    auto iloop = 0;

    if (optcontrol.standardize) {
        while (iloop < optcontrol.maxnum_iteration) {
            do_print_log = !((iloop + 1) % optcontrol.output_frequency) && (verbosity > 0);

            if (do_print_log) {
                std::cout << "   Coordinate Descent : " << std::setw(5) << iloop + 1 << std::endl;
            }
            delta = beta;
            for (i = 0; i < N; ++i) {
                beta(i) = shrink(Minv * grad(i) + beta(i), alphlambda);
                delta(i) -= beta(i);
                if (std::abs(delta(i)) > 0.0) {
                    if (!has_prod[i]) {
                        for (j = 0; j < N; ++j) {
                            Prod(j, i) = A.col(j).dot(A.col(i));
                        }
                        has_prod[i] = true;
                    }
                    grad = grad + Prod.col(i) * delta(i);
                }
            }
            ++iloop;
            diff = std::sqrt(delta.dot(delta) / static_cast<double>(N));

            if (diff < optcontrol.tolerance_iteration) break;

            if (do_print_log) {
                std::cout << "    1: ||u_{k}-u_{k-1}||_2     = " << std::setw(15) << diff
                    << std::setw(15) << diff * std::sqrt(static_cast<double>(N) / beta.dot(beta)) << std::endl;
                auto tmp = 0.0;
#ifdef _OPENMP
#pragma omp parallel for reduction(+:tmp)
#endif
                for (i = 0; i < N; ++i) {
                    tmp += std::abs(beta(i));
                }
                std::cout << "    2: ||u_{k}||_1             = " << std::setw(15) << tmp << std::endl;
                res = A * beta - b;
                tmp = res.dot(res);
                std::cout << "    3: ||Au_{k}-f||_2          = " << std::setw(15) << std::sqrt(tmp)
                    << std::setw(15) << std::sqrt(tmp / (fnorm * fnorm)) << std::endl;
                std::cout << std::endl;
            }
        }
    } else {
        // Non-standardized version. Needs additional operations
        while (iloop < optcontrol.maxnum_iteration) {
            do_print_log = !((iloop + 1) % optcontrol.output_frequency) && (verbosity > 0);

            if (do_print_log) {
                std::cout << "   Coordinate Descent : " << std::setw(5) << iloop + 1 << std::endl;
            }
            delta = beta;
            for (i = 0; i < N; ++i) {
                beta(i) = shrink(Minv * grad(i) + beta(i) / scale_beta(i), alphlambda) * scale_beta(i);
                delta(i) -= beta(i);
                if (std::abs(delta(i)) > 0.0) {
                    if (!has_prod[i]) {
                        for (j = 0; j < N; ++j) {
                            Prod(j, i) = A.col(j).dot(A.col(i));
                        }
                        has_prod[i] = true;
                    }
                    grad = grad + Prod.col(i) * delta(i);
                }
            }
            ++iloop;
            diff = std::sqrt(delta.dot(delta) / static_cast<double>(N));

            if (diff < optcontrol.tolerance_iteration) break;

            if (do_print_log) {
                std::cout << "    1: ||u_{k}-u_{k-1}||_2     = " << std::setw(15) << diff
                    << std::setw(15) << diff * std::sqrt(static_cast<double>(N) / beta.dot(beta)) << std::endl;
                auto tmp = 0.0;
#ifdef _OPENMP
#pragma omp parallel for reduction(+:tmp)
#endif
                for (i = 0; i < N; ++i) {
                    tmp += std::abs(beta(i));
                }
                std::cout << "    2: ||u_{k}||_1             = " << std::setw(15) << tmp << std::endl;
                res = A * beta - b;
                tmp = res.dot(res);
                std::cout << "    3: ||Au_{k}-f||_2          = " << std::setw(15) << std::sqrt(tmp)
                    << std::setw(15) << std::sqrt(tmp / (fnorm * fnorm)) << std::endl;
                std::cout << std::endl;
            }
        }
    }

    if (verbosity > 0) {
        if (iloop >= optcontrol.maxnum_iteration) {
            std::cout << "WARNING: Convergence NOT achieved within " << optcontrol.maxnum_iteration
                << " coordinate descent iterations." << std::endl;
        } else {
            std::cout << "  Convergence achieved in " << iloop << " iterations." << std::endl;
        }
        const auto param2norm = beta.dot(beta);
        if (std::abs(param2norm) < eps) {
            std::cout << "    1': ||u_{k}-u_{k-1}||_2     = " << std::setw(15) << 0.0
                << std::setw(15) << 0.0 << std::endl;
        } else {
            std::cout << "    1': ||u_{k}-u_{k-1}||_2     = " << std::setw(15) << diff
                << std::setw(15) << diff * std::sqrt(static_cast<double>(N) / param2norm) << std::endl;
        }
        double tmp = 0.0;
#ifdef _OPENMP
#pragma omp parallel for reduction(+:tmp)
#endif
        for (i = 0; i < N; ++i) {
            tmp += std::abs(beta(i));
        }
        std::cout << "    2': ||u_{k}||_1             = " << std::setw(15) << tmp << std::endl;
        res = A * beta - b;
        tmp = res.dot(res);
        std::cout << "    3': ||Au_{k}-f||_2          = " << std::setw(15) << std::sqrt(tmp)
            << std::setw(15) << std::sqrt(tmp / (fnorm * fnorm)) << std::endl;
        std::cout << std::endl;
    }

    for (i = 0; i < N; ++i) x[i] = beta(i);
}