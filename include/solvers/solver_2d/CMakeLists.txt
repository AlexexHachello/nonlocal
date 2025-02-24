cmake_minimum_required(VERSION 3.16)
project(finite_element_solver_2d)

add_subdirectory(thermal)
add_subdirectory(mechanical)

set(FINITE_ELEMENT_SOLVER_2D_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_library(finite_element_solver_2d_lib INTERFACE)
target_sources(finite_element_solver_2d_lib INTERFACE
               boundary_condition_2d.hpp
               right_part_2d.hpp
               influence_functions_2d.hpp
               finite_element_matrix_2d.hpp
               solution_2d.hpp)
target_include_directories(finite_element_solver_2d_lib INTERFACE ${FINITE_ELEMENT_SOLVER_2D_LIB_DIR})
target_link_libraries(finite_element_solver_2d_lib INTERFACE heat_equation_solver_2d_lib structural_solver_2d_lib)