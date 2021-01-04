#ifndef HEAT_EQUATION_SOLVER_HPP
#define HEAT_EQUATION_SOLVER_HPP

#include <iostream>
#include <algorithm>
#include <omp.h>
#include "finite_element_solver_base.hpp"
#include "../../Eigen/Eigen/Dense"
#include "../../Eigen/Eigen/Sparse"
//#include "Eigen/PardisoSupport"

namespace nonlocal::heat {

enum class boundary_t : uint8_t {
    TEMPERATURE = uint8_t(boundary_type::FIRST_KIND),
    FLOW        = uint8_t(boundary_type::SECOND_KIND)
};

template<class T>
struct boundary_condition {
    static_assert(std::is_floating_point_v<T>, "The T must be floating point.");
    std::function<T(const std::array<T, 2>&)> func = [](const std::array<T, 2>&) noexcept { return 0; };
    boundary_t type = boundary_t::FLOW;
};

template<class T, class I>
class heat_equation_solver : protected finite_element_solver_base<T, I> {
    using _base = finite_element_solver_base<T, I>;
    using typename _base::Finite_Element_2D_Ptr;
    using _base::X;
    using _base::Y;
    using _base::mesh;
    using _base::quad_shift;
    using _base::quad_coord;
    using _base::jacobian;

    // Интегрирование базисной функции i по элементу e.
    // Для использования, предварительно должны быть проинициализированы jacobi_matrices текущего элемента.
    // Квадратурный сдвиг quad_shift нужен в случае если матрицы Якоби текуще элемента хранятся со сдвигом.
    T integrate_basic(const Finite_Element_2D_Ptr& e, const size_t i, size_t quad_shift) const {
        T integral = 0;
        for(size_t q = 0; q < e->nodes_count(); ++q, ++quad_shift)
            integral += e->weight(q) * e->qN(i, q) * jacobian(quad_shift);
        return integral;
    }

    // Интегрирование произведения базисных функций i и j элемента e.
    T integrate_basic_pair(const Finite_Element_2D_Ptr& e, const size_t i, const size_t j, size_t quad_shift) const {
        T integral = 0;
        for(size_t q = 0; q < e->nodes_count(); ++q, ++quad_shift)
            integral += e->weight(q) * e->qN(i, q) * e->qN(j, q) * jacobian(quad_shift);
        return integral;
    }

    // Интегрирование произведения пар градиентов функций i и j элемента e.
    T integrate_loc(const Finite_Element_2D_Ptr& e, const size_t i, const size_t j, size_t quad_shift) const {
        T integral = 0;
        for(size_t q = 0; q < e->qnodes_count(); ++q, ++quad_shift)
            integral += e->weight(q) / jacobian(quad_shift) *
                        (_base::template dNd<X>(e, i, q, quad_shift) * _base::template dNd<X>(e, j, q, quad_shift) +
                         _base::template dNd<Y>(e, i, q, quad_shift) * _base::template dNd<Y>(e, j, q, quad_shift));
        return integral;
    }

    // Интегрирование произведения пары градиентов функции iL элемента eL и функции jNL элемента eNL.
    // Influence_Function - функтор с сигнатурой T(std::array<T, 2>&, std::array<T, 2>&)
    template<class Influence_Function>
    T integrate_nonloc(const Finite_Element_2D_Ptr& eL,  const size_t iL,  size_t shiftL,
                       const Finite_Element_2D_Ptr& eNL, const size_t jNL, size_t shiftNL,
                       const Influence_Function& influence_function) const {
        T integral = 0;
        const size_t sub_shift = shiftNL;
        for(size_t qL = 0; qL < eL->qnodes_count(); ++qL, ++shiftL) {
            T inner_int_x = 0, inner_int_y = 0;
            for(size_t qNL = 0, shiftNL = sub_shift; qNL < eNL->qnodes_count(); ++qNL, ++shiftNL) {
                const T influence_weight = eNL->weight(qNL) * influence_function(quad_coord(shiftL), quad_coord(shiftNL));
                inner_int_x += influence_weight * _base::template dNd<X>(eNL, jNL, qNL, shiftNL);
                inner_int_y += influence_weight * _base::template dNd<Y>(eNL, jNL, qNL, shiftNL);
            }
            integral += eL->weight(qL) * (inner_int_x * _base::template dNd<X>(eL, iL, qL, shiftL) + 
                                          inner_int_y * _base::template dNd<Y>(eL, iL, qL, shiftL));
        }
        return integral;
    }

    template<class Right_Part>
    void integrate_right_part(Eigen::Matrix<T, Eigen::Dynamic, 1>& f, const Right_Part& right_part) const {
        for(size_t el = 0; el < mesh().elements_count(); ++el) {
            const auto& e = mesh().element_2d(el);
            for(size_t i = 0; i < e->nodes_count(); ++i)
                f[mesh().node_number(el, i)] += _base::template integrate_function(e, i, quad_shift(el), right_part);
        }
    }

    void create_matrix_portrait(Eigen::SparseMatrix<T, Eigen::RowMajor, I>& K, Eigen::SparseMatrix<T, Eigen::RowMajor, I>& K_bound,
                                const bool neumann_task, const T p1, const std::vector<bool>& inner_nodes) const {
        std::vector<std::set<I>> inner_portrait(mesh().nodes_count()),
                                 bound_portrait(mesh().nodes_count());

        if (neumann_task) {
#pragma omp parallel for default(none) shared(K, inner_portrait)
            for(size_t node = 0; node < mesh().nodes_count(); ++node)
                inner_portrait[node].insert(mesh().nodes_count());
        }

        const auto indexator = [&inner_nodes, &inner_portrait, &bound_portrait](const I row, const I col) {
            if (inner_nodes[row] && inner_nodes[col]) {
                if (row <= col)
                    inner_portrait[row].insert(col);
            } else if (row != col) {
                if (!inner_nodes[col])
                    bound_portrait[row].insert(col);
            } else
                inner_portrait[row].insert(col);
        };

        if (p1 > _base::MAX_LOCAL_WEIGHT) {
            _base::template mesh_run_loc(
                [this, &indexator] (const size_t el, const size_t i, const size_t j) {
                    indexator(mesh().node_number(el, i), mesh().node_number(el, j));
                });
        } else {
            _base::template mesh_run_nonloc(
                [this, &indexator](const size_t elL, const size_t iL, const size_t elNL, const size_t jNL) {
                    indexator(mesh().node_number(elL, iL), mesh().node_number(elNL, jNL));
                });
        }

        _base::convert_portrait(K, inner_portrait);
        inner_portrait.reserve(0);
        _base::convert_portrait(K_bound, bound_portrait);
        bound_portrait.reserve(0);
    }

    template<class Integrate_Rule, class Influence_Function>
    void calc_matrix(Eigen::SparseMatrix<T, Eigen::RowMajor, I>& K, Eigen::SparseMatrix<T, Eigen::RowMajor, I>& K_bound,
                     const Integrate_Rule& integrate_rule, const bool neumann_task,
                     const T p1, const Influence_Function& influence_fun, const std::vector<bool>& inner_nodes) const {
        if (neumann_task) {
#pragma omp parallel for default(none) shared(K)
            for(size_t node = 0; node < mesh().nodes_count(); ++node) {
                T& val = K.coeffRef(node, mesh().nodes_count());
                for(const I el : _base::nodes_elements_map(node)) {
                    const auto& e = mesh().element_2d(el);
                    const size_t i = _base::global_to_local_numbering(el).find(node)->second;
                    val += integrate_basic(e, i, quad_shift(el));
                }
            }
        }

        _base::template mesh_run_loc(
            [this, &K, &K_bound, &inner_nodes, &integrate_rule, p1](const size_t el, const size_t i, const size_t j) {
                const I row = mesh().node_number(el, i),
                        col = mesh().node_number(el, j);
                if (inner_nodes[row] && inner_nodes[col]) {
                    if (row <= col)
                        K.coeffRef(row, col) += p1 * integrate_rule(mesh().element_2d(el), i, j, quad_shift(el));
                } else if (row != col) {
                    if (!inner_nodes[col])
                        K_bound.coeffRef(row, col) += p1 * integrate_rule(mesh().element_2d(el), i, j, quad_shift(el));
                } else
                    K.coeffRef(row, col) = 1;
            }
        );

        if (p1 < _base::MAX_LOCAL_WEIGHT) {
            _base::template mesh_run_nonloc(
                [this, &K, &K_bound, &inner_nodes, &integrate_rule, &influence_fun, p2 = 1 - p1]
                (const size_t elL, const size_t iL, const size_t elNL, const size_t jNL) {
                    const I row = mesh().node_number(elL,  iL ),
                            col = mesh().node_number(elNL, jNL);
                    if (inner_nodes[row] && inner_nodes[col]) {
                        if (row <= col)
                            K.coeffRef(row, col) += p2 * integrate_nonloc(mesh().element_2d(elL ), iL,  quad_shift(elL ),
                                                                          mesh().element_2d(elNL), jNL, quad_shift(elNL), influence_fun);
                    } else if (row != col)
                        if (!inner_nodes[col])
                            K_bound.coeffRef(row, col) += p2 * integrate_nonloc(mesh().element_2d(elL ), iL,  quad_shift(elL ),
                                                                                mesh().element_2d(elNL), jNL, quad_shift(elNL), influence_fun);
                }
            );
        }
    }

    // Вычисление марицы теплопроводности (теплоёмкости в случае когда integrate_rule == integrate_basic_pair).
    // На выходе получаем расщеплённую матрицу, где K будет участвовать в решение СЛАУ, а K_bound уйдёт в правую часть.
    // Integrate_Rule - функтор с сигнатурой T(const Finite_Element_2D_Ptr&, const size_t, const size_t, const std::vector<std::array<T, 4>>&, size_t)
    // Influence_Function - функтор с сигнатурой T(std::array<T, 2>&, std::array<T, 2>&)
    template<class Integrate_Rule, class Influence_Function>
    void create_matrix(Eigen::SparseMatrix<T, Eigen::RowMajor, I>& K, Eigen::SparseMatrix<T, Eigen::RowMajor, I>& K_bound,
                       const std::vector<boundary_condition<T>>& bounds_cond, const bool neumann_task,
                       const Integrate_Rule& integrate_rule, const T p1, const Influence_Function& influence_fun) const {
        std::vector<bool> inner_nodes(mesh().nodes_count(), true);
        _base::template boundary_nodes_run([this, &bounds_cond, &inner_nodes](const size_t b, const size_t el, const size_t i) {
            if(bounds_cond[b].type == boundary_t::TEMPERATURE)
                inner_nodes[mesh().node_number(b, el, i)] = false;
        });

        double time = omp_get_wtime();
        create_matrix_portrait(K, K_bound, neumann_task, p1, inner_nodes);
        std::cout << "create_matrix_portrait: " << omp_get_wtime() - time << std::endl;

        time = omp_get_wtime();
        calc_matrix(K, K_bound, integrate_rule, neumann_task, p1, influence_fun, inner_nodes);
        std::cout << "calc coeffs: " << omp_get_wtime() - time << std::endl;

//        std::cout << Eigen::MatrixXd{K} << std::endl << std::endl;
//        std::cout << Eigen::MatrixXd{K_bound} << std::endl << std::endl;
    }

    void integrate_boundary_flow(Eigen::Matrix<T, Eigen::Dynamic, 1>& f, const std::vector<boundary_condition<T>>& bounds_cond) const {
        std::vector<std::array<T, 2>> quad_nodes, jacobi_matrices;
        for(size_t b = 0; b < bounds_cond.size(); ++b)
            if(bounds_cond[b].type == boundary_t::FLOW)
                for(size_t el = 0; el < mesh().elements_count(b); ++el) {
                    _base::approx_quad_nodes_on_bound(quad_nodes, b, el);
                    _base::approx_jacobi_matrices_on_bound(jacobi_matrices, b, el);
                    const auto& be = mesh().element_1d(b, el);
                    for(size_t i = 0; i < be->nodes_count(); ++i)
                        f[mesh().node_number(b, el, i)] += _base::template integrate_boundary_gradient(be, i, quad_nodes, jacobi_matrices, bounds_cond[b].func);
                }
    }

    void temperature_on_boundary(Eigen::Matrix<T, Eigen::Dynamic, 1>& f, const std::vector<boundary_condition<T>>& bounds_cond,
                                 const Eigen::SparseMatrix<T, Eigen::RowMajor, I>& K_bound) const {
        Eigen::Matrix<T, Eigen::Dynamic, 1> temperature = Eigen::Matrix<T, Eigen::Dynamic, 1>::Zero(f.size());

        _base::template boundary_nodes_run(
            [this, &bounds_cond, &temperature](const size_t b, const size_t el, const size_t i) {
                if (bounds_cond[b].type == boundary_t::TEMPERATURE) {
                    const I node = mesh().node_number(b, el, i);
                    if (temperature[node] == 0)
                        temperature[node] = bounds_cond[b].func(mesh().node(node));
                }
            });

        f -= K_bound * temperature;

        _base::template boundary_nodes_run(
            [this, &bounds_cond, &temperature, &f](const size_t b, const size_t el, const size_t i) {
                if (bounds_cond[b].type == boundary_t::TEMPERATURE) {
                    const I node = mesh().node_number(b, el, i);
                    f[node] = temperature[node];
                }
            });
    }

public:
    explicit heat_equation_solver(const mesh::mesh_2d<T, I>& mesh) :
        _base{mesh} {}

    explicit heat_equation_solver(mesh::mesh_2d<T, I>&& mesh) :
        _base{std::move(mesh)} {}

    ~heat_equation_solver() override = default;

    template<class Vector>
    void save_as_vtk(const std::string& path, const Vector& temperature) const;

    T integrate_solution(const Eigen::Matrix<T, Eigen::Dynamic, 1>& temperature) const;

    // Функция, решающая стационарное уравнение теплопроводности в нелокальной постановке.
    // Right_Part - функтор с сигнатурой T(std::array<T, 2>&),
    // Influence_Function - функтор с сигнатурой T(std::array<T, 2>&, std::array<T, 2>&)
    // volume - значение интеграла по области, в случае если поставлена задача Неймана, по умолчанию 0.
    template<class Right_Part, class Influence_Function>
    Eigen::Matrix<T, Eigen::Dynamic, 1>
    stationary(const std::vector<boundary_condition<T>>& bounds_cond, const Right_Part& right_part, 
               const T r, const T p1, const Influence_Function& influence_fun, const T volume = 0);

//    template<class Init_Distribution, class Right_Part, class Influence_Function>
//    void nonstationary(const std::string& path, const T tau, const uintmax_t time_steps,
//                       const std::vector<boundary_condition<T>>& bounds_cond,
//                       const Init_Distribution& init_dist, const Right_Part& right_part,
//                       const T r, const T p1, const Influence_Function& influence_fun,
//                       const uintmax_t print_frequency = std::numeric_limits<uintmax_t>::max());
};

template<class T, class I>
template<class Vector>
void heat_equation_solver<T, I>::save_as_vtk(const std::string& path, const Vector& temperature) const {
    static constexpr std::string_view data_type = std::is_same_v<T, float> ? "float" : "double";

    if(mesh().nodes_count() != size_t(temperature.size()))
        throw std::domain_error{"mesh().nodes_count() != T.size()."};

    std::ofstream fout{path};
    fout.precision(20);
    mesh().save_as_vtk(fout);
    fout << "POINT_DATA " << mesh().nodes_count() << '\n';
    fout << "SCALARS Temperature " << data_type << " 1" << '\n'
         << "LOOKUP_TABLE default" << '\n';
    for(size_t i = 0; i < mesh().nodes_count(); ++i)
        fout << temperature[i] << '\n';
}

template<class T, class I>
T heat_equation_solver<T, I>::integrate_solution(const Eigen::Matrix<T, Eigen::Dynamic, 1>& temperature) const {
    if(mesh().nodes_count() != size_t(temperature.size()))
        throw std::logic_error{"mesh.nodes_count() != T.size()"};

    T integral = 0;
    for(size_t el = 0; el < mesh().elements_count(); ++el) {
        const auto& e = mesh().element_2d(el);
        for(size_t i = 0; i < e->nodes_count(); ++i)
            for(size_t q = 0, shift = quad_shift(el); q < e->qnodes_count(); ++q, ++shift)
                integral += e->weight(q) * e->qN(i, q) * temperature[mesh().node_number(el, i)] * jacobian(shift);
    }
    return integral;
}

template<class T, class I>
template<class Right_Part, class Influence_Function>
Eigen::Matrix<T, Eigen::Dynamic, 1> 
heat_equation_solver<T, I>::stationary(const std::vector<boundary_condition<T>>& bounds_cond, const Right_Part& right_part, 
                                       const T r, const T p1, const Influence_Function& influence_fun, const T volume) {
    const bool neumann_task = std::all_of(bounds_cond.cbegin(), bounds_cond.cend(), 
        [](const boundary_condition<T>& bound) { return bound.type == boundary_t::FLOW; });
    const size_t matrix_size = neumann_task ? mesh().nodes_count()+1 : mesh().nodes_count();

    Eigen::Matrix<T, Eigen::Dynamic, 1> f = Eigen::Matrix<T, Eigen::Dynamic, 1>::Zero(matrix_size);
    integrate_boundary_flow(f, bounds_cond);
    if(neumann_task) {
        if(std::abs(std::accumulate(f.data(), f.data() + f.size(), T{0}, [](const T sum, const T val) { return sum + val; })) > 1e-5)
            throw std::domain_error{"The problem is unsolvable. Contour integral != 0."};
        f[mesh().nodes_count()] = volume;
    }

    if (p1 < _base::MAX_LOCAL_WEIGHT)
        _base::find_neighbors(r);

    Eigen::SparseMatrix<T, Eigen::RowMajor, I> K      (matrix_size, matrix_size),
                                               K_bound(matrix_size, matrix_size);
    create_matrix(
        K, K_bound, bounds_cond, neumann_task,
        [this](const Finite_Element_2D_Ptr& e, const size_t i, const size_t j, size_t quad_shift) {
            return integrate_loc(e, i, j, quad_shift); },
        p1, influence_fun
    );

    integrate_right_part(f, right_part);
    temperature_on_boundary(f, bounds_cond, K_bound);

    //Eigen::PardisoLDLT<Eigen::SparseMatrix<T, Eigen::ColMajor, I>, Eigen::Lower> solver;
    Eigen::ConjugateGradient<Eigen::SparseMatrix<T, Eigen::RowMajor, I>, Eigen::Upper> solver;
    solver.compute(K);
    Eigen::Matrix<T, Eigen::Dynamic, 1> temperature = solver.solve(f);
    //std::cout << "System solving: " << omp_get_wtime() - time << std::endl;
    temperature.conservativeResize(mesh().nodes_count());

    return std::move(temperature);
}

//template<class T, class I>
//template<class Init_Distribution, class Right_Part, class Influence_Function>
//void heat_equation_solver<T, I>::nonstationary(const std::string& path, const T tau, const uintmax_t time_steps,
//                                               const std::vector<boundary_condition<T>>& bounds_cond,
//                                               const Init_Distribution& init_dist, const Right_Part& right_part,
//                                               const T r, const T p1, const Influence_Function& influence_fun, const uintmax_t print_frequency) {
//    _base::find_neighbors(r);
//
//    static constexpr bool NOT_NEUMANN_TASK = false;
//    Eigen::SparseMatrix<T, Eigen::ColMajor, I> K      (mesh().nodes_count(), mesh().nodes_count()),
//                                               K_bound(mesh().nodes_count(), mesh().nodes_count());
//    create_matrix(
//       K, K_bound, bounds_cond, NOT_NEUMANN_TASK,
//       [this](const Finite_Element_2D_Ptr& e, const size_t i, const size_t j, size_t quad_shift) {
//            return integrate_loc(e, i, j, quad_shift); },
//       p1, influence_fun
//    );
//
//    static constexpr T LOCAL = 1;
//    Eigen::SparseMatrix<T, Eigen::ColMajor, I> C      (mesh().nodes_count(), mesh().nodes_count()),
//                                               C_bound(mesh().nodes_count(), mesh().nodes_count());
//    create_matrix(
//       C, C_bound, bounds_cond, NOT_NEUMANN_TASK,
//       [this](const Finite_Element_2D_Ptr& e, const size_t i, const size_t j, size_t quad_shift) {
//            return integrate_basic_pair(e, i, j, quad_shift); },
//       LOCAL, influence_fun
//    );
//
//    C_bound.setZero();
//    K_bound *= tau;
//    K *= tau;
//    K += C;
//    _base::template boundary_nodes_run([this, &bounds_cond, &K](const size_t b, const size_t el, const size_t i) {
//        if(bounds_cond[b].type == boundary_t::TEMPERATURE)
//            K.coeffRef(mesh().node_number(b, el, i), mesh().node_number(b, el, i)) = 1;
//    });
//
//    Eigen::Matrix<T, Eigen::Dynamic, 1> f = Eigen::Matrix<T, Eigen::Dynamic, 1>::Zero(mesh().nodes_count()),
//                                        temperature_prev(mesh().nodes_count()),
//                                        temperature     (mesh().nodes_count());
//    for(size_t i = 0; i < mesh().nodes_count(); ++i)
//        temperature_prev[i] = init_dist(mesh().node(i));
//
////    Eigen::PardisoLDLT<Eigen::SparseMatrix<T, Eigen::ColMajor, I>, Eigen::Lower> solver;
////    solver.compute(K);
////    if(print_frequency != std::numeric_limits<uintmax_t>::max()) {
////        save_as_vtk(path + "0.vtk", temperature_prev);
////        std::cout << "step = " << 0 << " Volume = " << integrate_solution(temperature_prev) << std::endl;
////    }
////    for(size_t i = 1; i < time_steps; ++i) {
////        f.setZero();
////        integrate_boundary_flow(f, bounds_cond);
////        integrate_right_part(f, right_part);
////        f *= tau;
////        f += C.template selfadjointView<Eigen::Lower>() * temperature_prev;
////        temperature_on_boundary(f, bounds_cond, K_bound);
////        temperature = solver.solve(f);
////        temperature_prev.swap(temperature);
////        if(i % print_frequency == 0) {
////            save_as_vtk(path + std::to_string(i) + ".vtk", temperature_prev);
////            std::cout << "step = " << i << " Volume = " << integrate_solution(temperature_prev) << std::endl;
////        }
////    }
//}

}

#endif