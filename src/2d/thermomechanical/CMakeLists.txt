cmake_minimum_required(VERSION 3.16)

project(thermomechanical_2d)

add_executable(constant_temperature constant_temperature.cpp)
target_include_directories(constant_temperature PUBLIC ".")
target_link_libraries(constant_temperature metamath_lib mesh_2d_lib slae_solver_lib finite_element_solver_2d_lib MPI_utils_lib)

add_executable(normal_distribution normal_distribution.cpp)
target_include_directories(normal_distribution PUBLIC ".")
target_link_libraries(normal_distribution metamath_lib mesh_2d_lib slae_solver_lib finite_element_solver_2d_lib MPI_utils_lib)