#ifndef FINITE_ELEMENTS_LAGRANGIAN_ELEMENTS_1D_HPP
#define FINITE_ELEMENTS_LAGRANGIAN_ELEMENTS_1D_HPP

#include "symbolic_base.hpp"
#include "geometry_1d.hpp"
#include "uniform_partition.hpp"

namespace metamath::finite_element {

template<class T, size_t N>
class lagrangian_element_1d : public geometry_1d<T, standart_segment_geometry> {
protected:
    using geometry_1d<T, standart_segment_geometry>::x;

    static inline constexpr std::array<T, N + 1> nodes = utils::uniform_partition<N + 1>(geometry_1d<T, standart_segment_geometry>::shape_t::boundary);
    static inline constexpr auto basis = SYMBOLIC_NAMESPACE::generate_lagrangian_basis<x>(nodes);

    constexpr explicit lagrangian_element_1d() noexcept = default;
    ~lagrangian_element_1d() override = default;
};

}

#endif