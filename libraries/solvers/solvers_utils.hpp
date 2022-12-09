#ifndef NONLOCAL_SOLVERS_UTILS_HPP
#define NONLOCAL_SOLVERS_UTILS_HPP

#include "solvers_constants.hpp"
#include <eigen3/Eigen/Sparse>
#include <algorithm>
#include <array>
#include <ranges>
#include <vector>

namespace nonlocal::utils {

template<class B>
constexpr boundary_condition_t to_general_condition(const B condition) noexcept {
    return boundary_condition_t(condition);
}

template<class B, size_t N>
constexpr std::array<boundary_condition_t, N> to_general_condition(const std::array<B, N> conditions) noexcept {
    std::array<boundary_condition_t, N> conditions_types;
    std::transform(conditions.cbegin(), conditions.cend(), conditions_types.begin(),
        [](const B condition_type) constexpr noexcept { return to_general_condition(condition_type); });
    return conditions_types;
}

template<class B>
std::vector<boundary_condition_t> to_general_condition(const std::vector<B>& conditions) {
    std::vector<B> conditions_types(conditions.size());
    std::transform(conditions.cbegin(), conditions.cend(), conditions_types.begin(),
        [](const B condition_type) constexpr noexcept { return to_general_condition(condition_type); });
    return conditions_types;
}

template<class T, class I>
void accumulate_shifts(Eigen::SparseMatrix<T, Eigen::RowMajor, I>& K) {
    for(const size_t i : std::ranges::iota_view{0u, size_t(K.rows())})
        K.outerIndexPtr()[i+1] += K.outerIndexPtr()[i];
}

template<class T, class I>
void allocate_matrix(Eigen::SparseMatrix<T, Eigen::RowMajor, I>& K) {
    K.data().resize(K.outerIndexPtr()[K.rows()]);
    for(const size_t i : std::ranges::iota_view{0u, size_t(K.nonZeros())}) {
        K.innerIndexPtr()[i] = 0;
        K.valuePtr()[i] = T{0};
    }
}

template<class T, class I>
void sort_indices(Eigen::SparseMatrix<T, Eigen::RowMajor, I>& K) {
#pragma omp parallel for default(none) shared(K) schedule(dynamic)
    for(size_t i = 0; i < K.rows(); ++i)
        std::sort(&K.innerIndexPtr()[K.outerIndexPtr()[i]], &K.innerIndexPtr()[K.outerIndexPtr()[i+1]]);
}

}

#endif