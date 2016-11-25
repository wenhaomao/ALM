/*
 constraint.h

 Copyright (c) 2014, 2015, 2016 Terumasa Tadano

 This file is distributed under the terms of the MIT license.
 Please see the file 'LICENCE.txt' in the root directory 
 or http://opensource.org/licenses/mit-license.php for information.
*/

#pragma once

#include <vector>
#include <set>
#include <string>
#include "pointers.h"
#include "constants.h"
#include <boost/bimap.hpp>

namespace ALM_NS
{
    class ConstraintClass
    {
    public:
        std::vector<double> w_const;

        ConstraintClass();

        ConstraintClass(const ConstraintClass &a) : w_const(a.w_const) {}

        ConstraintClass(const std::vector<double> &vec) : w_const(vec) {}

        ConstraintClass(const int n, const double *arr, const int nshift = 0)
        {
            for (int i = nshift; i < n; ++i) {
                w_const.push_back(arr[i]);
            }
        }

        bool operator<(const ConstraintClass &a) const
        {
            return std::lexicographical_compare(w_const.begin(), w_const.end(),
                                                a.w_const.begin(), a.w_const.end());
        }
    };

    class ConstraintTypeFix
    {
    public:
        unsigned int p_index_target;
        double val_to_fix;

        ConstraintTypeFix(const unsigned int index_in, const double val_in) :
            p_index_target(index_in), val_to_fix(val_in) {}
    };

    class ConstraintTypeRelate
    {
    public:
        unsigned int p_index_target;
        std::vector<double> alpha;
        std::vector<unsigned int> p_index_orig;

        ConstraintTypeRelate(const unsigned int index_in,
                             const std::vector<double> alpha_in,
                             const std::vector<unsigned int> p_index_in) :
            p_index_target(index_in), alpha(alpha_in), p_index_orig(p_index_in) {}
    };

    class Constraint: protected Pointers
    {
    public:
        Constraint(class ALMCore *);
        ~Constraint();

        void setup();

        int constraint_mode;
        int P;
        std::string fc2_file, fc3_file;
        bool fix_harmonic, fix_cubic;
        int constraint_algebraic;

        double **const_mat;
        double *const_rhs;

        bool exist_constraint;
        bool extra_constraint_from_symmetry;
        std::string rotation_axis;
        std::vector<ConstraintClass> *const_symmetry;

        std::vector<ConstraintTypeFix> *const_fix;
        std::vector<ConstraintTypeRelate> *const_relate;
        boost::bimap<int, int> *index_bimap;

        void constraint_from_symmetry(std::vector<ConstraintClass> *);

        void get_mapping_constraint(const int, std::vector<ConstraintClass> *,
                                    std::vector<ConstraintTypeFix> *,
                                    std::vector<ConstraintTypeRelate> *,
                                    boost::bimap<int, int> *, const bool);

    private:

        bool impose_inv_T, impose_inv_R, exclude_last_R;

        std::vector<ConstraintClass> *const_translation;
        std::vector<ConstraintClass> *const_rotation_self;
        std::vector<ConstraintClass> *const_rotation_cross;

        std::vector<ConstraintClass> *const_self;

        void set_default_variables();
        void deallocate_variables();

        int levi_civita(const int, const int, const int);

        void translational_invariance();
        void rotational_invariance();
        void calc_constraint_matrix(const int, int &);

        void setup_rotation_axis(bool [3][3]);
        bool is_allzero(const int, const double *, const int nshift = 0);
        bool is_allzero(const std::vector<double>);

        bool is_allzero(const std::vector<int>, int &);

        void remove_redundant_rows(const int, std::vector<ConstraintClass> &,
                                   const double tolerance = eps12);

        void rref(const int, const int, double **, int &, const double tolerance = eps12);
    };

    extern "C"
    {
        void dgetrf_(int *m, int *n, double *a, int *lda, int *ipiv, int *info);
    }
}
