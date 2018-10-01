/*
 constraint.h

 Copyright (c) 2014, 2015, 2016 Terumasa Tadano

 This file is distributed under the terms of the MIT license.
 Please see the file 'LICENCE.txt' in the root directory
 or http://opensource.org/licenses/mit-license.php for information.
*/

#pragma once

#include <boost/bimap.hpp>
#include <utility>
#include <vector>
#include <string>
#include <iomanip>
#include <map>
#include "constants.h"
#include "fcs.h"
#include "interaction.h"
#include "system.h"
#include "timer.h"

namespace ALM_NS
{
    class ConstraintClass
    {
    public:
        std::vector<double> w_const;

        ConstraintClass();

        ConstraintClass(const ConstraintClass &a) : w_const(a.w_const) { }

        ConstraintClass(std::vector<double> vec) : w_const(std::move(vec)) { }

        ConstraintClass(const int n,
                        const double *arr,
                        const int nshift = 0)
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

        ConstraintTypeFix(const unsigned int index_in,
                          const double val_in) :
            p_index_target(index_in), val_to_fix(val_in) { }
    };

    class ConstraintTypeRelate
    {
    public:
        unsigned int p_index_target;
        std::vector<double> alpha;
        std::vector<unsigned int> p_index_orig;

        ConstraintTypeRelate(const unsigned int index_in,
                             const std::vector<double> &alpha_in,
                             const std::vector<unsigned int> &p_index_in) :
            p_index_target(index_in), alpha(alpha_in), p_index_orig(p_index_in) { }
    };

    inline bool equal_within_eps12(const std::vector<double> &a,
                                   const std::vector<double> &b)
    {
        int n = a.size();
        int m = b.size();
        if (n != m) return false;
        double res = 0.0;
        for (int i = 0; i < n; ++i) {
            if (std::abs(a[i] - b[i]) > eps12) return false;
        }
        return true;
    }

    class ConstraintIntegerElement
    {
        // For sparse representation
    public:
        unsigned int col;
        int val;

        ConstraintIntegerElement(const unsigned int col_in,
                                 const int val_in) :
            col(col_in), val(val_in) {}
    };

    // Operator for sort
    inline bool operator<(const std::vector<ConstraintIntegerElement> &obj1,
                          const std::vector<ConstraintIntegerElement> &obj2)
    {
        const int len1 = obj1.size();
        const int len2 = obj2.size();
        const int min = std::min(len1, len2);

        for (int i = 0; i < min; ++i) {
            if (obj1[i].col < obj2[i].col) {
                return true;
            } else if (obj1[i].col > obj2[i].col) {
                return false;
            } else {
                if (obj1[i].val < obj2[i].val) {
                    return true;
                } else if (obj1[i].val > obj2[i].val) {
                    return false;
                }
            }
        }
        return false;
    }

    // Operator for unique
    inline bool operator==(const std::vector<ConstraintIntegerElement> &obj1,
                           const std::vector<ConstraintIntegerElement> &obj2)
    {
        const int len1 = obj1.size();
        const int len2 = obj2.size();
        if (len1 != len2) return false;

        for (int i = 0; i < len1; ++i) {
            if (obj1[i].col != obj2[i].col || obj1[i].val != obj2[i].val) {
                return false;
            }
        }
        return true;
    }

    class ConstraintDoubleElement
    {
        // For sparse representation
    public:
        unsigned int col;
        double val;

        ConstraintDoubleElement(const unsigned int col_in,
                                const double val_in) :
            col(col_in), val(val_in) {}
    };

    // Operator for sort
    inline bool operator<(const std::vector<ConstraintDoubleElement> &obj1,
                          const std::vector<ConstraintDoubleElement> &obj2)
    {
        const int len1 = obj1.size();
        const int len2 = obj2.size();
        const int min = std::min(len1, len2);

        for (int i = 0; i < min; ++i) {
            if (obj1[i].col < obj2[i].col) {
                return true;
            } else if (obj1[i].col > obj2[i].col) {
                return false;
            } else {
                if (obj1[i].val < obj2[i].val) {
                    return true;
                } else if (obj1[i].val > obj2[i].val) {
                    return false;
                }
            }
        }
        return false;
    }

    // Operator for unique
    inline bool operator==(const std::vector<ConstraintDoubleElement> &obj1,
                           const std::vector<ConstraintDoubleElement> &obj2)
    {
        const int len1 = obj1.size();
        const int len2 = obj2.size();
        if (len1 != len2) return false;

        for (int i = 0; i < len1; ++i) {
            if (obj1[i].col != obj2[i].col || (std::abs(obj1[i].val - obj2[i].val) > 1.0e-10)) {
                return false;
            }
        }
        return true;
    }

    inline bool operator<(const std::map<unsigned int, double> &obj1,
                          const std::map<unsigned int, double> &obj2)
    {
        return obj1.begin()->first < obj2.begin()->first;
    }

    class Constraint
    {
    public:
        Constraint();
        ~Constraint();

        void setup(const System *system,
                   const Fcs *fcs,
                   const Interaction *interaction,
                   const Symmetry *symmetry,
                   const std::string alm_mode,
                   const int verbosity,
                   Timer *timer);

        int constraint_mode;
        int number_of_constraints;
        std::string fc2_file, fc3_file;
        bool fix_harmonic, fix_cubic;
        int constraint_algebraic;

        double **const_mat;
        double *const_rhs;
        double tolerance_constraint;

        bool exist_constraint;
        bool extra_constraint_from_symmetry;
        std::string rotation_axis;

        ConstraintSparseForm *const_symmetry;
        std::vector<ConstraintTypeFix> *const_fix;
        std::vector<ConstraintTypeRelate> *const_relate;
        std::vector<ConstraintTypeRelate> *const_relate_rotation;
        boost::bimap<int, int> *index_bimap;

        void get_mapping_constraint(const int,
                                    std::vector<int> *,
                                    ConstraintSparseForm *,
                                    std::vector<ConstraintTypeFix> *,
                                    std::vector<ConstraintTypeRelate> *,
                                    boost::bimap<int, int> *) const;

    private:

        bool impose_inv_T, impose_inv_R, exclude_last_R;

        ConstraintSparseForm *const_translation;
        ConstraintSparseForm *const_rotation_self;
        ConstraintSparseForm *const_rotation_cross;
        ConstraintSparseForm *const_self;

        void set_default_variables();
        void deallocate_variables();

        int levi_civita(int,
                        int,
                        int) const;

        void generate_rotational_constraint(const System *,
                                            const Symmetry *,
                                            const Interaction *,
                                            const Fcs *,
                                            const int,
                                            const double,
                                            ConstraintSparseForm *,
                                            ConstraintSparseForm *);

        void calc_constraint_matrix(int,
                                    std::vector<int> *,
                                    int,
                                    int &) const;

        void print_constraint(const ConstraintSparseForm &) const;

        void setup_rotation_axis(bool [3][3]);
        bool is_allzero(int,
                        const double *,
                        int nshift = 0) const;
        bool is_allzero(const std::vector<int> &,
                        int &) const;
        bool is_allzero(const std::vector<double> &,
                        double,
                        int &,
                        const int nshift = 0) const;


        void remove_redundant_rows(int,
                                   std::vector<ConstraintClass> &,
                                   double tolerance = eps12) const;


        void generate_symmetry_constraint_in_cartesian(int,
                                                       const Symmetry *,
                                                       const Interaction *,
                                                       const Fcs *,
                                                       const int,
                                                       ConstraintSparseForm *) const;

        void get_constraint_translation(const Cell &,
                                        const Symmetry *,
                                        const Interaction *,
                                        const Fcs *,
                                        int,
                                        const std::vector<FcProperty> &,
                                        int,
                                        ConstraintSparseForm &,
                                        bool do_rref = false) const;

        void generate_translational_constraint(const Cell &,
                                               const Symmetry *,
                                               const Interaction *,
                                               const Fcs *,
                                               const int,
                                               ConstraintSparseForm *);

        void fix_forceconstants_to_file(int,
                                        const Symmetry *,
                                        const Fcs *,
                                        std::string,
                                        std::vector<ConstraintTypeFix> &) const;
    };

    extern "C" {
    void dgetrf_(int *m,
                 int *n,
                 double *a,
                 int *lda,
                 int *ipiv,
                 int *info);
    }
}
