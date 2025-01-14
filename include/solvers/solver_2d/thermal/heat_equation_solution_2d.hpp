#ifndef NONLOCAL_HEAT_EQUATION_SOLUTION_HPP
#define NONLOCAL_HEAT_EQUATION_SOLUTION_HPP

#include "solution_2d.hpp"
#include "heat_equation_parameters_2d.hpp"

namespace nonlocal::thermal {

template<class T, class I>
class heat_equation_solution_2d : public solution_2d<T, I> {
    using _base = solution_2d<T, I>;
    using typename solution_2d<T, I>::influence_function_t;

    std::vector<T> _temperature;
    std::array<std::vector<T>, 2> _flux;
    std::array<T, 2> _thermal_conductivity = {1, 1};

    void calc_local_flux(const std::array<std::vector<T>, 2>& gradient_in_quads);
    void calc_nonlocal_flux(const std::array<std::vector<T>, 2>& gradient_in_quads);

public:
    explicit heat_equation_solution_2d(const std::shared_ptr<mesh::mesh_proxy<T, I>>& mesh_proxy);
    template<material_t Material, class Vector>
    explicit heat_equation_solution_2d(const std::shared_ptr<mesh::mesh_proxy<T, I>>& mesh_proxy,
                                       const T local_weight, const influence_function_t& influence_function,
                                       const equation_parameters_2d<T, Material>& parameters, const Vector& temperature);
    ~heat_equation_solution_2d() noexcept override = default;

    const std::vector<T>& temperature() const noexcept;
    const std::array<std::vector<T>, 2>& flux() const noexcept;

    T calc_energy() const;
    bool is_flux_calculated() const noexcept;
    const std::array<std::vector<T>, 2>& calc_flux();

    void save_as_vtk(std::ofstream& output) const override;
    void save_as_vtk(const std::string& path) const;
};

template<class T, class I>
heat_equation_solution_2d<T, I>::heat_equation_solution_2d(const std::shared_ptr<mesh::mesh_proxy<T, I>>& mesh_proxy)
    : _base{mesh_proxy} {}

template<class T, class I>
template<material_t Material, class Vector>
heat_equation_solution_2d<T, I>::heat_equation_solution_2d(const std::shared_ptr<mesh::mesh_proxy<T, I>>& mesh_proxy,
                                                           const T local_weight, const influence_function_t& influence_function,
                                                           const equation_parameters_2d<T, Material>& parameters, const Vector& temperature)
    : _base{mesh_proxy, local_weight, influence_function}
    , _temperature{temperature.cbegin(), std::next(temperature.cbegin(), mesh_proxy->mesh().nodes_count())} {
    if constexpr (Material == material_t::ISOTROPIC)
        _thermal_conductivity = {parameters.thermal_conductivity, parameters.thermal_conductivity};
    else
        _thermal_conductivity = parameters.thermal_conductivity;
    }

template<class T, class I>
const std::vector<T>& heat_equation_solution_2d<T, I>::temperature() const noexcept {
    return _temperature;
}

template<class T, class I>
const std::array<std::vector<T>, 2>& heat_equation_solution_2d<T, I>::flux() const noexcept {
    return _flux;
}

template<class T, class I>
T heat_equation_solution_2d<T, I>::calc_energy() const {
    return mesh::integrate(*_base::mesh_proxy(), temperature());
}

template<class T, class I>
bool heat_equation_solution_2d<T, I>::is_flux_calculated() const noexcept {
    return !flux()[X].empty() && !flux()[Y].empty();
}

template<class T, class I>
void heat_equation_solution_2d<T, I>::calc_local_flux(const std::array<std::vector<T>, 2>& gradient_in_quads) {
    using namespace metamath::functions;
    const std::array<T, 2> local_factor = _base::local_weight() * _thermal_conductivity;
    _flux[X] = mesh::from_qnodes_to_nodes(*_base::mesh_proxy(), gradient_in_quads[X]);
    _flux[Y] = mesh::from_qnodes_to_nodes(*_base::mesh_proxy(), gradient_in_quads[Y]);
    _flux[X] *= local_factor[X];
    _flux[Y] *= local_factor[Y];
}

template<class T, class I>
void heat_equation_solution_2d<T, I>::calc_nonlocal_flux(const std::array<std::vector<T>, 2>& gradient_in_quads) {
    using namespace metamath::functions;
    const std::array<T, 2> nonlocal_factor = (T{1} - _base::local_weight()) * _thermal_conductivity;
    _base::template calc_nonlocal(
        [this, &gradient_in_quads, &nonlocal_factor](const size_t e, const size_t node) {
            std::array<T, 2> integral = {};
            auto J = _base::mesh_proxy()->jacobi_matrix(e);
            auto qshift = _base::mesh_proxy()->quad_shift(e);
            auto qcoord = _base::mesh_proxy()->quad_coord(e);
            const auto& eNL = _base::mesh_proxy()->mesh().element_2d(e);
            for(size_t q = 0; q < eNL->qnodes_count(); ++q, ++qshift, ++qcoord, ++J) {
                const T influence_weight = eNL->weight(q) * mesh::jacobian(*J) *
                                           _base::influence_function()(*qcoord, _base::mesh_proxy()->mesh().node(node));
                integral[X] += influence_weight * gradient_in_quads[X][qshift];
                integral[Y] += influence_weight * gradient_in_quads[Y][qshift];
            }
            _flux[X][node] += nonlocal_factor[X] * integral[X];
            _flux[Y][node] += nonlocal_factor[Y] * integral[Y];
        }
    );
}

template<class T, class I>
const std::array<std::vector<T>, 2>& heat_equation_solution_2d<T, I>::calc_flux() {
    if (!is_flux_calculated()) {
        const std::array<std::vector<T>, 2> gradient_in_quads = mesh::approximate_gradient_in_qnodes(*_base::mesh_proxy(), temperature());
        calc_local_flux(gradient_in_quads);
        if (_base::local_weight() < MAX_NONLOCAL_WEIGHT<T>)
            calc_nonlocal_flux(gradient_in_quads);
    }
    return flux();
}

template<class T, class I>
void heat_equation_solution_2d<T, I>::save_as_vtk(std::ofstream& output) const {
    _base::save_as_vtk(output);
    _base::save_scalars(output, temperature(), "Temperature");
    if (is_flux_calculated())
        _base::save_vectors(output, flux(), "Flux");
}

template<class T, class I>
void heat_equation_solution_2d<T, I>::save_as_vtk(const std::string& path) const {
    std::ofstream output{path};
    save_as_vtk(output);
}

}

#endif