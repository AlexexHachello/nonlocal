#ifndef NONLOCAL_STRUCTURAL_SOLUTION_HPP
#define NONLOCAL_STRUCTURAL_SOLUTION_HPP

#include "mesh_proxy.hpp"

namespace nonlocal::structural {

enum class calc_type : bool { PLANE_STRESS, PLANE_STRAIN };

template<class T>
struct calculation_parameters final {
    calc_type type = calc_type::PLANE_STRESS;
    T nu    = 0, // Коэффициент Пуассона
      E     = 0, // Модуль Юнга
      alpha = 0; // Коэффициент линейного расширения
    bool thermoelasticity = false;
    std::vector<T> delta_temperature;
};

// Матрица Гука, которая имеет следующий портрет:
// arr[0] arr[1]   0
// arr[1] arr[0]   0
//   0      0    arr[2]
template<class T>
std::array<T, 3> hooke_matrix(const calculation_parameters<T>& parameters) noexcept {
    const bool plane_stress = parameters.type == calc_type::PLANE_STRESS;
    const T nu = plane_stress ? parameters.nu : parameters.nu / (1 - parameters.nu),
            E  = plane_stress ? parameters.E  : parameters.E  / (1 - parameters.nu * parameters.nu);
    return {       E / (1 - nu*nu),
              nu * E / (1 - nu*nu),
             0.5 * E / (1 + nu) };
}

template<class T, class I>
class solution final {
    std::shared_ptr<mesh::mesh_proxy<T, I>> _mesh_proxy;
    std::array<T, 3> _D;
    T _p1 = 1;
    std::function<T(const std::array<T, 2>& x, const std::array<T, 2>& y)> _influence_fun;

    std::array<std::vector<T>, 2> _u;
    std::array<std::vector<T>, 3> _strain, _stress;

    void strain_and_stress_loc();
    void stress_nonloc();

public:
    template<class Vector>
    explicit solution(const std::shared_ptr<mesh::mesh_proxy<T, I>>& mesh_proxy, const std::array<T, 3>& D, const Vector& u,
                      const T p1, const std::function<T(const std::array<T, 2>& x, const std::array<T, 2>& y)>& influence_fun)
            : _mesh_proxy{mesh_proxy}
            , _D{D}
            , _p1{p1}
            , _influence_fun{influence_fun}
            , _u{std::vector<T>(mesh_proxy->mesh().nodes_count()), std::vector<T>(mesh_proxy->mesh().nodes_count())} {
        for(size_t comp = 0; comp < _u.size(); ++comp)
            for(size_t i = 0; i < _mesh_proxy->mesh().nodes_count(); ++i)
                _u[comp][i] = u[2*i+comp];
    }

    const std::array<std::vector<T>, 2>& get_displacement() const { return _u; }
    const std::array<std::vector<T>, 3>& get_strains() const { return _strain; }
    const std::array<std::vector<T>, 3>& get_stress () const { return _stress; }

    void calc_strain_and_stress();
    T calc_energy() const;
    void save_as_vtk(const std::string& path) const;
};

template<class T, class I>
void solution<T, I>::strain_and_stress_loc() {
#pragma omp parallel for default(none)
    for(size_t node = 0; node < _mesh_proxy->mesh().nodes_count(); ++node) {
        for(const I e : _mesh_proxy->nodes_elements_map(node)) {
            const auto& el = _mesh_proxy->mesh().element_2d(e);
            const size_t i = _mesh_proxy->global_to_local_numbering(e).find(node)->second;
            const std::array<T, 4>& J = _mesh_proxy->jacobi_matrix_node(node);
            const T jac = _mesh_proxy->jacobian(J);
            for(size_t j = 0; j < el->nodes_count(); ++j) {
                const T Nxi = el->Nxi(j, el->node(i)), Neta = el->Neta(j, el->node(i));
                const std::array<T, 2> dx = {( J[3] * Nxi - J[2] * Neta) / jac,
                                             (-J[1] * Nxi + J[0] * Neta) / jac};
                _strain[0][node] += dx[0] * _u[0][_mesh_proxy->mesh().node_number(e, j)];
                _strain[1][node] += dx[1] * _u[1][_mesh_proxy->mesh().node_number(e, j)];
                _strain[2][node] += dx[0] * _u[1][_mesh_proxy->mesh().node_number(e, j)] +
                                    dx[1] * _u[0][_mesh_proxy->mesh().node_number(e, j)];
            }
        }
        _strain[0][node] /=     _mesh_proxy->nodes_elements_map(node).size();
        _strain[1][node] /=     _mesh_proxy->nodes_elements_map(node).size();
        _strain[2][node] /= 2 * _mesh_proxy->nodes_elements_map(node).size();
        _stress[0][node] += _D[0] * _strain[0][node] + _D[1] * _strain[1][node];
        _stress[1][node] += _D[1] * _strain[0][node] + _D[0] * _strain[1][node];
        _stress[2][node] += _D[2] * _strain[2][node];
    }
}

template<class T, class I>
void solution<T, I>::stress_nonloc() {
    const T p2 = 1 - _p1;
    const std::array<std::vector<T>, 3> strains_in_quads{
        _mesh_proxy->approx_in_quad(_strain[0]),
        _mesh_proxy->approx_in_quad(_strain[1]),
        _mesh_proxy->approx_in_quad(_strain[2])
    };

    std::vector<bool> neighbors(_mesh_proxy->mesh().elements_count(), false);
#pragma omp parallel for default(none) firstprivate(neighbors)
    for(size_t node = 0; node < _mesh_proxy->mesh().nodes_count(); ++node) {
        std::fill(neighbors.begin(), neighbors.end(), false);
        for(const I elL : _mesh_proxy->nodes_elements_map(node))
            for(const I elNL : _mesh_proxy->neighbors(elL))
                neighbors[elNL] = true;
        for(size_t e = 0; e < _mesh_proxy->mesh().elements_count(); ++e)
            if (neighbors[e]) {
                const auto& eNL = _mesh_proxy->mesh().element_2d(e);
                auto qshift     = _mesh_proxy->quad_shift(e);
                auto qcoord     = _mesh_proxy->quad_coord(e);
                auto J          = _mesh_proxy->jacobi_matrix(e);
                for(size_t q = 0; q < eNL->qnodes_count(); ++q, ++qshift, ++qcoord, ++J) {
                    const T influence_weight = p2 * eNL->weight(q) * _mesh_proxy->jacobian(*J) * _influence_fun(*qcoord, _mesh_proxy->mesh().node(node));
                    _stress[0][node] += influence_weight * (_D[0] * strains_in_quads[0][qshift] + _D[1] * strains_in_quads[1][qshift]);
                    _stress[1][node] += influence_weight * (_D[1] * strains_in_quads[0][qshift] + _D[0] * strains_in_quads[1][qshift]);
                    _stress[2][node] += influence_weight *  _D[2] * strains_in_quads[2][qshift];
                }
            }
    }
}

template<class T, class I>
void solution<T, I>::calc_strain_and_stress() {
    for(size_t comp = 0; comp < _strain.size(); ++comp) {
        _strain[comp].resize(_mesh_proxy->mesh().nodes_count());
        _stress[comp].resize(_mesh_proxy->mesh().nodes_count());
    }
    strain_and_stress_loc();
    if(_p1 < 0.999) { // Нелокальная задача
        for(size_t comp = 0; comp < _stress.size(); ++comp)
            for(size_t node = 0; node < _mesh_proxy->mesh().nodes_count(); ++node)
                _stress[comp][node] *= _p1;
        stress_nonloc();
    }
}

template<class T, class I>
T solution<T, I>::calc_energy() const {
    T integral = 0;
    if(!_strain[0].empty()) {
        for(size_t e = 0; e < _mesh_proxy->mesh().elements_count(); ++e) {
            const auto& el     = _mesh_proxy->mesh().element_2d(e);
            const auto J_start = _mesh_proxy->jacobi_matrix(e);
            for(size_t i = 0; i < el->nodes_count(); ++i) {
                auto J = J_start;
                for(size_t q = 0; q < el->qnodes_count(); ++q, ++J) {
                    const size_t node = _mesh_proxy->mesh().node_number(e, i);
                    integral += el->weight(q) * el->qN(i, q) * _mesh_proxy->jacobian(*J) *
                                (    _strain[0][node] * _stress[0][node] +
                                     _strain[1][node] * _stress[1][node] +
                                 2 * _strain[2][node] * _stress[2][node]);
                }
            }
        }
    }
    return 0.5 * integral;
}

template<class T, class I>
void solution<T, I>::save_as_vtk(const std::string& path) const {
    static constexpr std::string_view data_type = std::is_same_v<T, float> ? "float" : "double";
    static constexpr std::array<std::string_view, 4> strain_number = {"strain11", "strain22", "strain12", "strain33"},
                                                     stress_number = {"stress11", "stress22", "stress12", "stress33"};

    std::ofstream fout{path};
    fout.precision(std::numeric_limits<T>::max_digits10);

    _mesh_proxy->mesh().save_as_vtk(fout);

    fout << "POINT_DATA " << _mesh_proxy->mesh().nodes_count() << '\n';
    fout << "VECTORS Displacement " << data_type << '\n';
    for(size_t i = 0; i < _mesh_proxy->mesh().nodes_count(); ++i)
        fout << _u[0][i] << ' ' << _u[1][i] << " 0\n";

    if(!_strain[0].empty()) {
        for(size_t comp = 0; comp < 3; ++comp) {
            fout << "SCALARS " << strain_number[comp] << ' ' << data_type << " 1\n"
                 << "LOOKUP_TABLE default\n";
            for(size_t i = 0; i < _mesh_proxy->mesh().nodes_count(); ++i)
                fout << _strain[comp][i] << '\n';
        }

        //fout << "SCALARS " << strain_number[3] << ' ' << data_type << " 1\n"
        //     << "LOOKUP_TABLE default\n";
        //for(size_t i = 0; i < _mesh_proxy->mesh().nodes_count(); ++i)
        //    fout << (_stress[0][i] + _stress[1][i]) << '\n';

        for(size_t comp = 0; comp < 3; ++comp) {
            fout << "SCALARS " << stress_number[comp] << ' ' << data_type << " 1\n"
                 << "LOOKUP_TABLE default\n";
            for(size_t i = 0; i < _mesh_proxy->mesh().nodes_count(); ++i)
                fout << _stress[comp][i] << '\n';
        }

        fout << "SCALARS mises " << data_type << " 1\n"
             << "LOOKUP_TABLE default\n";
        for(size_t i = 0; i < _mesh_proxy->mesh().nodes_count(); ++i)
            fout << std::sqrt(_stress[0][i] * _stress[0][i] +
                              _stress[1][i] * _stress[1][i] -
                              _stress[0][i] * _stress[1][i] +
                          3 * _stress[2][i] * _stress[2][i]) << '\n';
    }
}

}

#endif