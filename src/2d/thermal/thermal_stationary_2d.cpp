#include "influence_functions_2d.hpp"
#include "thermal/stationary_heat_equation_solver_2d.hpp"

int main(int argc, char** argv) {
#ifdef MPI_USE
    MPI_Init(&argc, &argv);
#endif

    if(argc < 5) {
        std::cerr << "Input format [program name] <path to mesh> <r1> <r2> <p1>" << std::endl;
#ifdef MPI_USE
        MPI_Finalize();
#endif
        return EXIT_FAILURE;
    }

    try {
        std::cout.precision(20);
        const double p1 = std::stod(argv[4]);
        const std::array<double, 2> r = {std::stod(argv[2]), std::stod(argv[3])};
        nonlocal::thermal::equation_parameters_2d<double, nonlocal::material_t::ORTHOTROPIC> eq_parameters;
        eq_parameters.thermal_conductivity[0] = r[0] / std::max(r[0], r[1]);
        eq_parameters.thermal_conductivity[1] = r[1] / std::max(r[0], r[1]);
        eq_parameters.heat_transfer = {
            {"Right", 1},
            {"Left",  1}
        };

        const auto mesh = std::make_shared<nonlocal::mesh::mesh_2d<double>>(argv[1]);
        auto mesh_proxy = std::make_shared<nonlocal::mesh::mesh_proxy<double, int32_t>>(mesh);
        if (p1 < nonlocal::MAX_NONLOCAL_WEIGHT<double>) {
            const double time = omp_get_wtime();
            mesh_proxy->find_neighbours(std::max(r[0], r[1]) + 0.015, nonlocal::mesh::balancing_t::MEMORY);
            std::cout << "find neighbours: " << omp_get_wtime() - time << std::endl;
            double mean = 0;
            for(const size_t e : std::views::iota(size_t{0}, mesh->elements_count()))
                mean += mesh_proxy->neighbors(e).size();
            std::cout << "Average neighbours = " << mean / mesh->nodes_count() << std::endl;
        }

        const std::unordered_map<std::string, nonlocal::stationary_boundary_2d_t<nonlocal::thermal::boundary_condition_t, double, 1>>
            boundary_condition = {
                {   "Right",
                    {   nonlocal::thermal::boundary_condition_t::FLUX,
                        [](const std::array<double, 2>& x) { return 1.; },
                    }
                },
                {   "Left",
                    {   nonlocal::thermal::boundary_condition_t::FLUX,
                        [](const std::array<double, 2>& x) { return -1.; },
                    }
                }
            };

        auto solution = nonlocal::thermal::stationary_heat_equation_solver_2d<double, int32_t, int32_t>(
            eq_parameters, mesh_proxy, boundary_condition,
            [](const std::array<double, 2>& x) { return 0; }, // Правая часть
            p1, nonlocal::influence::polynomial_2d<double, 2, 1>{r}
        );

        if (MPI_utils::MPI_rank() == 0) {
            solution.calc_flux();
            std::cout << "Energy = " << solution.calc_energy() << std::endl;
            solution.save_as_vtk("heat.vtk");
            const auto& [X, Y] = solution.flux();
            nonlocal::mesh::save_as_csv("T.csv", *mesh, solution.temperature());
            nonlocal::mesh::save_as_csv("TX.csv", *mesh, X);
            nonlocal::mesh::save_as_csv("TY.csv", *mesh, Y);
        }
    } catch(const std::exception& e) {
        std::cerr << e.what() << std::endl;
#ifdef MPI_USE
        MPI_Finalize();
#endif
        return EXIT_FAILURE;
    } catch(...) {
        std::cerr << "Unknown error." << std::endl;
#ifdef MPI_USE
        MPI_Finalize();
#endif
        return EXIT_FAILURE;
    }

#ifdef MPI_USE
    MPI_Finalize();
#endif
    return 0;
}