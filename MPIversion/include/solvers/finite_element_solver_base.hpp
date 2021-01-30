#ifndef FINITE_ELEMENT_ROUTINE_HPP
#define FINITE_ELEMENT_ROUTINE_HPP

#include <numeric>
#include <petsc.h>
#include <petscsystypes.h>
#include "../../Eigen/Eigen/Sparse"
#include "mesh.hpp"

namespace nonlocal {

enum class boundary_type : uint8_t {
    FIRST_KIND,
    SECOND_KIND
};

template<class T, class B, size_t DoF>
struct boundary_condition {
    static_assert(std::is_floating_point_v<T>, "The T must be floating point.");

    struct boundary_pair {
        B type = B(boundary_type::SECOND_KIND);
        std::function<T(const std::array<T, 2>&)> func = [](const std::array<T, 2>&) constexpr noexcept { return 0; };
    };

    std::array<boundary_pair, DoF> data;

    static constexpr size_t degrees_of_freedom() { return DoF; }

    B type(const size_t b) const { return data[b].type; }
    const std::function<T(const std::array<T, 2>&)>& func(const size_t b) const { return data[b].func; }

    bool contains_condition_second_kind() const {
        return std::any_of(data.cbegin(), data.cend(), [](const boundary_pair& pair) { return pair.type == B(boundary_type::SECOND_KIND); });
    }
};

template<class T, class I>
class finite_element_solver_base {
    std::shared_ptr<mesh::mesh_info<T, I>> _mesh;

protected:
    using Finite_Element_1D_Ptr = typename mesh::mesh_2d<T, I>::Finite_Element_1D_Ptr;
    using Finite_Element_2D_Ptr = typename mesh::mesh_2d<T, I>::Finite_Element_2D_Ptr;

    enum component : bool {X, Y};
    static constexpr T MAX_LOCAL_WEIGHT = 0.999;

    explicit finite_element_solver_base(const std::shared_ptr<mesh::mesh_info<T, I>>& mesh) {
        set_mesh(mesh);
    }

    virtual ~finite_element_solver_base() noexcept = default;

    const mesh::mesh_2d<T, I>&            mesh                     ()                     const { return _mesh->mesh(); }
    I                                     quad_shift               (const size_t element) const { return _mesh->quad_shift(element); }
    const std::array<T, 2>&               quad_coord               (const size_t quad)    const { return _mesh->quad_coord(quad); }
    const std::array<T, 4>&               jacobi_matrix            (const size_t quad)    const { return _mesh->jacobi_matrix(quad); }
    const std::vector<I>&                 nodes_elements_map       (const size_t node)    const { return _mesh->nodes_elements_map(node); }
    const std::unordered_map<I, uint8_t>& global_to_local_numbering(const size_t element) const { return _mesh->global_to_local_numbering(element); }
    const std::vector<I>&                 neighbors                (const size_t element) const { return _mesh->neighbors(element); }
    int                                   rank                     ()                     const { return _mesh->rank(); }
    int                                   size                     ()                     const { return _mesh->size(); }
    size_t                                first_node               ()                     const { return _mesh->first_node(); }
    size_t                                last_node                ()                     const { return _mesh->last_node(); }

    // Функция обхода сетки в локальных постановках.
    // Нужна для предварительного подсчёта количества элементов  и интегрирования системы.
    // Callback - функтор с сигнатурой void(size_t, size_t, size_t)
    template<class Callback>
    void mesh_run_loc(const Callback& callback) const {
#pragma omp parallel for default(none) firstprivate(callback)
        for(size_t node = first_node(); node < last_node(); ++node) {
            for(const I el : nodes_elements_map(node)) {
                const auto& e = mesh().element_2d(el);
                const size_t i = global_to_local_numbering(el).find(node)->second; // Проекционные функции
                for(size_t j = 0; j < e->nodes_count(); ++j) // Аппроксимационные функции
                    callback(el, i, j);
            }
        }
    }

    // Функция обхода сетки в нелокальных постановках.
    // Нужна для предварительного подсчёта количества элементов и интегрирования системы.
    // Callback - функтор с сигнатурой void(size_t, size_t, size_t, size_t)
    template<class Callback>
    void mesh_run_nonloc(const Callback& callback) const {
#pragma omp parallel for default(none) firstprivate(callback)
        for(size_t node = first_node(); node < last_node(); ++node) {
            for(const I elL : nodes_elements_map(node)) {
                const size_t iL =  global_to_local_numbering(elL).find(node)->second; // Проекционные функции
                for(const I elNL : neighbors(elL)) {
                    const auto& eNL = mesh().element_2d(elNL);
                    for(size_t jNL = 0; jNL < eNL->nodes_count(); ++jNL) // Аппроксимационные функции
                        callback(elL, iL, elNL, jNL);
                }
            }
        }
    }

    template<class Callback>
    void boundary_nodes_run(const Callback& callback) const {
        for(size_t b = 0; b < mesh().boundary_groups_count(); ++b)
            for(size_t el = 0; el < mesh().elements_count(b); ++el) {
                const auto& be = mesh().element_1d(b, el);
                for(size_t i = 0; i < be->nodes_count(); ++i)
                    callback(b, el, i);
            }
    }

    void convert_portrait(Eigen::SparseMatrix<T, Eigen::RowMajor, I>& K, const std::vector<std::unordered_set<I>>& portrait) const {
        static constexpr auto accumulator = [](const size_t sum, const std::unordered_set<I>& row) { return sum + row.size(); };
        K.data().resize(std::accumulate(portrait.cbegin(), portrait.cend(), size_t{0}, accumulator));
        K.outerIndexPtr()[0] = 0;
        for(size_t row = 0; row < portrait.size(); ++row)
            K.outerIndexPtr()[row+1] = K.outerIndexPtr()[row] + portrait[row].size();
#pragma omp parallel for default(none) shared(K, portrait)
        for(size_t row = 0; row < portrait.size(); ++row) {
            I inner_index = K.outerIndexPtr()[row];
            for(const I col : portrait[row]) {
                K.valuePtr()[inner_index] = 0;
                K.innerIndexPtr()[inner_index++] = col;
            }
            std::sort(&K.innerIndexPtr()[K.outerIndexPtr()[row]], &K.innerIndexPtr()[K.outerIndexPtr()[row+1]]);
        }
    }

    void approx_quad_nodes_on_bound(std::vector<std::array<T, 2>>& quad_nodes, const size_t b, const size_t el) const {
        const auto& be = mesh().element_1d(b, el);
        quad_nodes.clear();
        quad_nodes.resize(be->qnodes_count(), {});
        for(size_t q = 0; q < be->qnodes_count(); ++q)
            for(size_t i = 0; i < be->nodes_count(); ++i)
                for(size_t comp = 0; comp < 2; ++comp)
                    quad_nodes[q][comp] += mesh().node(mesh().node_number(b, el, i))[comp] * be->qN(i, q);
    }

    void approx_jacobi_matrices_on_bound(std::vector<std::array<T, 2>>& jacobi_matrices, const size_t b, const size_t el) const {
        const auto& be = mesh().element_1d(b, el);
        jacobi_matrices.clear();
        jacobi_matrices.resize(be->qnodes_count(), {});
        for(size_t q = 0; q < be->qnodes_count(); ++q)
            for(size_t i = 0; i < be->nodes_count(); ++i)
                for(size_t comp = 0; comp < 2; ++comp)
                    jacobi_matrices[q][comp] += mesh().node(mesh().node_number(b, el, i))[comp] * be->qNxi(i, q);
    }

    // Function - функтор с сигнатурой T(std::array<T, 2>&)
    template<class Finite_Element_2D_Ptr, class Function>
    T integrate_function(const Finite_Element_2D_Ptr& e, const size_t i, size_t quad_shift, const Function& func) const {
        T integral = 0;
        for(size_t q = 0; q < e->qnodes_count(); ++q, ++quad_shift)
            integral += e->weight(q) * e->qN(i, q) * func(quad_coord(quad_shift)) * jacobian(quad_shift);
        return integral;
    }

    // Boundary_Gradient - функтор с сигнатурой T(std::array<T, 2>&)
    template<class Boundary_Gradient>
    T integrate_boundary_gradient(const Finite_Element_1D_Ptr& be, const size_t i,
                                  const std::vector<std::array<T, 2>>& quad_nodes, 
                                  const std::vector<std::array<T, 2>>& jacobi_matrices, 
                                  const Boundary_Gradient& boundary_gradient) const {
        T integral = 0;
        for(size_t q = 0; q < be->qnodes_count(); ++q)
            integral += be->weight(q) * be->qN(i, q) * boundary_gradient(quad_nodes[q]) * jacobian(jacobi_matrices[q]);
        return integral;
    }

    template<class B, size_t DoF>
    void integrate_boundary_condition_second_kind(Eigen::Matrix<T, Eigen::Dynamic, 1>& f,
                                                  const std::vector<boundary_condition<T, B, DoF>>& bounds_cond) const {
        std::vector<std::array<T, 2>> quad_nodes, jacobi_matrices;
        for(size_t b = 0; b < bounds_cond.size(); ++b)
            if (bounds_cond[b].contains_condition_second_kind())
                for(size_t el = 0; el < mesh().elements_count(b); ++el) {
                    approx_quad_nodes_on_bound(quad_nodes, b, el);
                    approx_jacobi_matrices_on_bound(jacobi_matrices, b, el);
                    const auto& be = mesh().element_1d(b, el);
                    for(size_t i = 0; i < be->nodes_count(); ++i)
                        for(size_t comp = 0; comp < DoF; ++comp)
                            if(bounds_cond[b].type(comp) == B(boundary_type::SECOND_KIND)) {
                                const I node = DoF * mesh().node_number(b, el, i) + comp;
                                if (node >= DoF * first_node() && node < DoF * last_node())
                                    f[node - DoF * first_node()] += integrate_boundary_gradient(be, i, quad_nodes, jacobi_matrices, bounds_cond[b].func(comp));
                            }

                }
    }

    template<class B, size_t DoF>
    void boundary_condition_first_kind(Eigen::Matrix<T, Eigen::Dynamic, 1>& f,
                                       const std::vector<boundary_condition<T, B, DoF>>& bounds_cond,
                                       const Eigen::SparseMatrix<T, Eigen::RowMajor, I>& K_bound) const {
        Eigen::Matrix<T, Eigen::Dynamic, 1> x = Eigen::Matrix<T, Eigen::Dynamic, 1>::Zero(K_bound.cols());
        boundary_nodes_run(
            [this, &bounds_cond, &x](const size_t b, const size_t el, const size_t i) {
                for(size_t comp = 0; comp < DoF; ++comp)
                    if (bounds_cond[b].type(comp) == B(boundary_type::FIRST_KIND)) {
                        const I node = DoF * mesh().node_number(b, el, i) + comp;
                        if (x[node] == 0)
                            x[node] = bounds_cond[b].func(comp)(mesh().node(node));
                    }
            });

        f -= K_bound * x;

        boundary_nodes_run(
        [this, &bounds_cond, &x, &f](const size_t b, const size_t el, const size_t i) {
            for(size_t comp = 0; comp < DoF; ++comp)
                if (bounds_cond[b].type(comp) == B(boundary_type::FIRST_KIND)) {
                    const I node = DoF * mesh().node_number(b, el, i) + comp;
                    if (node >= DoF * first_node() && node < DoF * last_node())
                        f[node - DoF * first_node()] = x[node];
                }
        });
    }

    static T jacobian(const std::array<T, 4>& J) noexcept {
        return std::abs(J[0] * J[3] - J[1] * J[2]);
    }

    T jacobian(const size_t quad_shift) const noexcept {
        return jacobian(jacobi_matrix(quad_shift));
    }

    static T jacobian(const std::array<T, 2>& jacobi_matrix) noexcept {
        return sqrt(jacobi_matrix[0] * jacobi_matrix[0] + jacobi_matrix[1] * jacobi_matrix[1]);
    }

    template<bool Component>
    T dNd(const Finite_Element_2D_Ptr& e, const size_t i, const size_t q, const size_t quad_shift) const {
        const std::array<T, 4>& J = jacobi_matrix(quad_shift);
        if constexpr (Component == component::X)
            return  e->qNxi(i, q) * J[3] - e->qNeta(i, q) * J[2];
        if constexpr (Component == component::Y)
            return -e->qNxi(i, q) * J[1] + e->qNeta(i, q) * J[0];
    }

public:
    void set_mesh(const std::shared_ptr<mesh::mesh_info<T, I>>& mesh) {
        _mesh = mesh;
    }
};

}

#endif