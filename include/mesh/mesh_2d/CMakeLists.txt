cmake_minimum_required(VERSION 3.16)
project(mesh_2d)

set(MESH_2D_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR})

add_library(mesh_2d_lib INTERFACE)
target_sources(mesh_2d_lib INTERFACE mesh_2d.hpp)
target_include_directories(mesh_2d_lib INTERFACE ${MESH_2D_LIB_DIR})