#ifndef SYMBOLIC_INTEGRAL_CONSTANT_HPP
#define SYMBOLIC_INTEGRAL_CONSTANT_HPP

#include "expression.hpp"

#include <cstdint>
#include <type_traits>

namespace SYMBOLIC_NAMESPACE {

template<auto N>
struct integral_constant : expression<integral_constant<N>> {
    static_assert(std::is_integral_v<decltype(N)>);

    template<class... Args>
    constexpr auto operator()(const Args&...) const noexcept {
        return N;
    }

    template<auto X>
    constexpr integral_constant<0> derivative() const noexcept {
        return {};
    }
};

template<auto N>
constexpr integral_constant<N> simplify(const integral_constant<N>) noexcept {
    return {};
}

}

#endif