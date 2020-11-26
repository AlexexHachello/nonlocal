#include <iostream>
#include <mpi.h>
#include "mesh.hpp"
#include <sstream>

void print_mesh(const mesh::mesh_2d<double>& msh) {
    std::cout << "nodes:\n";
    for(size_t n = 0; n < msh.nodes_count(); ++n)
        std::cout << msh.node(n)[0] << ' ' << msh.node(n)[1] << '\n';
    std::cout << std::endl;


    std::cout << "elements:\n";
    for(size_t el = 0; el < msh.elements_count(); ++el) {
        const auto& e = msh.element_2d(el);
        for(size_t i = 0; i < e->nodes_count(); ++i)
            std::cout << msh.node_number(el, i) << ' ';
        std::cout << '\n';
    }
    std::cout << std::endl;

    std::cout << "boundary:\n";
    for(size_t b = 0; b < msh.boundary_groups_count(); ++b) {
        std::cout << "group" << b << ":\n";
        for(size_t el = 0; el < msh.elements_count(b); ++el) {
            const auto& e = msh.element_1d(b, el);
            for(size_t i = 0; i < e->nodes_count(); ++i)
                std::cout << msh.node_number(b, el, i) << ' ';
            std::cout << '\n';
        }
    }
}

int main(int argc, char** argv) {
    if(argc < 2) {
        std::cerr << "Input format [program name] <path to mesh>" << std::endl;
        return EXIT_FAILURE;
    }

    MPI_Init(&argc, &argv);

    try {
        int size = -1, rank = -1;
        MPI_Comm_size(MPI_COMM_WORLD, &size);
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        mesh::mesh_2d<double> msh{argv[1]};
        for(int i = 0; i < size; ++i) {
            if (i == rank) {
                std::cout << "RANK: " << rank << std::endl;
                print_mesh(msh);
                std::cout << std::endl << std::endl;
            }
            MPI_Barrier(MPI_COMM_WORLD);
        }
    } catch(const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    } catch(...) {
        std::cerr << "Unknown error." << std::endl;
        return EXIT_FAILURE;
    }

    MPI_Finalize();

    return 0;
}