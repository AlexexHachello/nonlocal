#include "make_element.hpp"
#include "thermal/stationary_heat_equation_solver_1d.hpp"
#include "influence_functions_1d.hpp"
#include "mesh_1d_utils.hpp"
#include <iostream>
#include <fstream>

namespace {

using T = double;
using I = int64_t;

}

int main(const int argc, const char *const *const argv) {
    try {
        std::cout.precision(3);
        const auto mesh = std::make_shared<nonlocal::mesh::mesh_1d<T>>(
            nonlocal::make_element<T>(nonlocal::element_type::QUADRATIC),
            std::vector<nonlocal::mesh::segment_data<T>>{
                {.length = 0.25, .elements = 100},
                {.length = 0.25, .elements = 100},
                {.length = 0.25, .elements = 100},
                {.length = 0.25, .elements = 100}
            }
        );
        const std::vector<T> radii = {0.05, 0., 0.1, 0.};
        const T p1 = 0.5;
        std::vector<nonlocal::equation_parameters<1, T, nonlocal::thermal::stationary_equation_parameters_1d>> parameters;
        for(const auto [conductivity, radius, local_weight] : {std::tuple{ 1., radii[0], p1 }, 
                                                               std::tuple{ 7., radii[1], 1. },
                                                               std::tuple{ 3., radii[2], 1.},
                                                               std::tuple{10., radii[3], p1 }
                                                               }) {
            parameters.push_back({
                .physical = {
                    .conductivity = conductivity
                },
                .model = {
                    .influence = nonlocal::influence::polynomial_1d<T, 1, 1>{radius},
                    .local_weight = local_weight
                }
            });
        }
        if (nonlocal::theory_type(p1) == nonlocal::theory_t::NONLOCAL)
            mesh->find_neighbours(radii);

        const auto solution = nonlocal::thermal::stationary_heat_equation_solver_1d<T, I>(
            mesh, parameters,
            {   nonlocal::thermal::boundary_condition_t::FLUX,  1.,
                nonlocal::thermal::boundary_condition_t::FLUX, -1.,
            },
            [](const T x) constexpr noexcept { return 0; }
        );
        nonlocal::mesh::utils::save_as_csv(*mesh, solution.temperature(), "./T05.csv");
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Unknown error" << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}