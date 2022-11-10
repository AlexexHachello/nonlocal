#ifndef NONLOCAL_FINITE_ELEMENT_MATRIX_1D_HPP
#define NONLOCAL_FINITE_ELEMENT_MATRIX_1D_HPP

#include "../equation_parameters.hpp"
#include "../solvers_utils.hpp"

#include "mesh_1d.hpp"

#include <eigen3/Eigen/Sparse>

#include <iostream>

namespace nonlocal {

template<class T, class I>
class finite_element_matrix_1d {
    std::shared_ptr<mesh::mesh_1d<T>> _mesh;
    Eigen::SparseMatrix<T, Eigen::RowMajor, I> _matrix_inner;
    std::array<std::unordered_map<size_t, T>, 2> _matrix_bound;

protected:
    explicit finite_element_matrix_1d(const std::shared_ptr<mesh::mesh_1d<T>>& mesh);

    void calc_shifts(const std::array<bool, 2> is_first_kind);
    void init_indices();
    void create_matrix_portrait(const std::array<bool, 2> is_first_kind);

    template<theory_t Theory, class Callback>
    void mesh_run(const size_t segment, const Callback& callback) const;
    template<class Integrate_Loc, class Integrate_Nonloc>
    void calc_matrix(const std::array<bool, 2> is_first_kind, const std::vector<theory_t>& theories,
                     const Integrate_Loc& integrate_rule_loc, const Integrate_Nonloc& integrate_rule_nonloc);

public:
    virtual ~finite_element_matrix_1d() = default;

    const mesh::mesh_1d<T>& mesh() const noexcept;
    const std::shared_ptr<mesh::mesh_1d<T>>& mesh_ptr() const noexcept;
    Eigen::SparseMatrix<T, Eigen::RowMajor, I>& matrix_inner() noexcept;
    const Eigen::SparseMatrix<T, Eigen::RowMajor, I>& matrix_inner() const noexcept;
    std::array<std::unordered_map<size_t, T>, 2>& matrix_bound() noexcept;
    const std::array<std::unordered_map<size_t, T>, 2>& matrix_bound() const noexcept;

    void clear();
};

template<class T, class I>
finite_element_matrix_1d<T, I>::finite_element_matrix_1d(const std::shared_ptr<mesh::mesh_1d<T>>& mesh)
    : _mesh{mesh} {}

template<class T, class I>
const mesh::mesh_1d<T>& finite_element_matrix_1d<T, I>::mesh() const noexcept {
    return *_mesh;
}

template<class T, class I>
const std::shared_ptr<mesh::mesh_1d<T>>& finite_element_matrix_1d<T, I>::mesh_ptr() const noexcept {
    return _mesh;
}

template<class T, class I>
Eigen::SparseMatrix<T, Eigen::RowMajor, I>& finite_element_matrix_1d<T, I>::matrix_inner() noexcept {
    return _matrix_inner;
}

template<class T, class I>
const Eigen::SparseMatrix<T, Eigen::RowMajor, I>& finite_element_matrix_1d<T, I>::matrix_inner() const noexcept {
    return _matrix_inner;
}

template<class T, class I>
std::array<std::unordered_map<size_t, T>, 2>& finite_element_matrix_1d<T, I>::matrix_bound() noexcept {
    return _matrix_bound;
}

template<class T, class I>
const std::array<std::unordered_map<size_t, T>, 2>& finite_element_matrix_1d<T, I>::matrix_bound() const noexcept {
    return _matrix_bound;
}

template<class T, class I>
void finite_element_matrix_1d<T, I>::clear() {
    _matrix_inner = {};
    _matrix_bound = {};
}

template<class T, class I>
void finite_element_matrix_1d<T, I>::calc_shifts(const std::array<bool, 2> is_first_kind) {
    const size_t last_node = mesh().nodes_count() - 1;
    const size_t nodes_in_element = mesh().element().nodes_count() - 1;
    for(size_t node = 0; node < mesh().nodes_count(); ++node) {
        if (is_first_kind.front() && node == 0 || is_first_kind.back() && node == last_node)
            matrix_inner().outerIndexPtr()[node + 1] = 1;
        else {
            const auto [left, right] = mesh().node_elements(node);
            const auto [e, i] = right ? right : left;
            size_t neighbour = mesh().neighbours(e).back();
            if (e == neighbour)
                ++neighbour;
            const bool is_last_first_kind = is_first_kind.back() && neighbour * nodes_in_element == last_node;
            matrix_inner().outerIndexPtr()[node + 1] = (neighbour - e) * nodes_in_element - i + 1 - is_last_first_kind;
        }
    }
}

template<class T, class I>
void finite_element_matrix_1d<T, I>::init_indices() {
    for(const size_t i : std::ranges::iota_view{size_t{0}, size_t(matrix_inner().rows())})
        for(size_t j = matrix_inner().outerIndexPtr()[i], k = i; j < matrix_inner().outerIndexPtr()[i+1]; ++j, ++k)
            matrix_inner().innerIndexPtr()[j] = k;
}

template<class T, class I>
void finite_element_matrix_1d<T, I>::create_matrix_portrait(const std::array<bool, 2> is_first_kind) {
    calc_shifts(is_first_kind);
    utils::accumulate_shifts(matrix_inner());
    std::cout << "Non-zero elements count: " << matrix_inner().outerIndexPtr()[matrix_inner().rows()] << std::endl;
    utils::allocate_matrix(matrix_inner());
    init_indices();
}

template<class T, class I>
template<theory_t Theory, class Callback>
void finite_element_matrix_1d<T, I>::mesh_run(const size_t segment, const Callback& callback) const {
    const auto correct_node_data =
        [this](const size_t node, const std::ranges::iota_view<size_t, size_t> segment_nodes) constexpr noexcept {
            auto node_elements = mesh().node_elements(node).to_array();
            if (node == segment_nodes.front() && node != 0)
                node_elements.front() = std::nullopt;
            else if (node == segment_nodes.back() && node != mesh().nodes_count() - 1)
                node_elements.back() = std::nullopt;
            return node_elements;
        };

    const auto segment_nodes = mesh().segment_nodes(segment);
    for(const size_t node : segment_nodes) {
        for(const auto node_data : correct_node_data(node, segment_nodes))
            if (node_data) {
                const auto& [eL, iL] = node_data;
                if constexpr (Theory == theory_t::LOCAL)
                    for(const size_t jL : std::ranges::iota_view{size_t{0}, mesh().element().nodes_count()})
                        callback(eL, iL, jL);
                if constexpr (Theory == theory_t::NONLOCAL)
                    for(const size_t eNL : mesh().neighbours(eL))
                        for(const size_t jNL : std::ranges::iota_view{size_t{0}, mesh().element().nodes_count()})
                            callback(eL, eNL, iL, jNL);
            }            
    }
}

template<class T, class I>
template<class Integrate_Loc, class Integrate_Nonloc>
void finite_element_matrix_1d<T, I>::calc_matrix(const std::array<bool, 2> is_first_kind, const std::vector<theory_t>& theories,
                                                 const Integrate_Loc& integrate_rule_loc, const Integrate_Nonloc& integrate_rule_nonloc) {
    const auto assemble_bound = [this](std::unordered_map<size_t, T>& matrix_bound, const size_t row, const size_t col, const T integral) {
        if (col == row)
            matrix_inner().coeffRef(row, col) = T{1};
        else if (const auto [it, flag] = matrix_bound.template try_emplace(col, integral); !flag)
            it->second += integral;
    };

    const auto assemble = [this, is_first_kind, &assemble_bound, last_node = mesh().nodes_count() - 1]
                          <class Integrate, class... Model_Args>
                          (const size_t row, const size_t col, const Integrate& integrate_rule, const Model_Args&... args) {
        if (is_first_kind.front() && (row == 0 || col == 0)) {
            if (row == 0)
                assemble_bound(matrix_bound().front(), row, col, integrate_rule(args...));
        } else if (is_first_kind.back() && (row == last_node || col == last_node)) {
            if (row == last_node)
                assemble_bound(matrix_bound().back(), row, col, integrate_rule(args...));
        } else if (row <= col)
            matrix_inner().coeffRef(row, col) += integrate_rule(args...);
    };

    for(const size_t segment : std::ranges::iota_view{size_t{0}, mesh().segments_count()})
        switch (theories[segment]) {
            case theory_t::NONLOCAL:
                mesh_run<theory_t::NONLOCAL>(segment,
                    [this, &assemble, &integrate_rule_nonloc, segment](const size_t eL, const size_t eNL, const size_t iL, const size_t jNL) {
                        assemble(mesh().node_number(eL, iL), mesh().node_number(eNL, jNL), integrate_rule_nonloc, segment, eL, eNL, iL, jNL);
                    }
                );
            case theory_t::LOCAL:
                mesh_run<theory_t::LOCAL>(segment,
                    [this, &assemble, integrate_rule_loc, segment](const size_t e, const size_t i, const size_t j) {
                        assemble(mesh().node_number(e, i), mesh().node_number(e, j), integrate_rule_loc, segment, e, i, j);
                    }
                );
            break;
            
            default:
                throw std::runtime_error{"Unknown theory type."};
        }
}

}

#endif