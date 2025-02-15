#ifndef NONLOCAL_SOLVERS_CONSTANTS_HPP
#define NONLOCAL_SOLVERS_CONSTANTS_HPP

#include <cstdint>

namespace nonlocal {

enum axis : size_t {
    X,
    Y,
    Z
};

enum class boundary_condition_t : uint8_t {
    FIRST_KIND,
    SECOND_KIND,
    THIRD_KIND,
    FOURTH_KIND
};

enum class material_t : uint8_t {
    ISOTROPIC,
    ORTHOTROPIC,
    ANISOTROPIC
};

enum class theory_t : uint8_t {
    LOCAL,
    NONLOCAL
};

template<class T>
inline constexpr T MAX_NONLOCAL_WEIGHT = T{0.999};

namespace thermal {

enum class boundary_condition_t : uint8_t {
    TEMPERATURE = uint8_t(nonlocal::boundary_condition_t::FIRST_KIND),
    FLUX = uint8_t(nonlocal::boundary_condition_t::SECOND_KIND),
    CONVECTION = uint8_t(nonlocal::boundary_condition_t::THIRD_KIND)
};

template<class T>
inline constexpr T NEUMANN_PROBLEM_MAX_BOUNDARY_ERROR = std::is_same_v<T, float> ? T{1e-5} : T{1e-10};

template<class T>
inline constexpr T NEUMANN_PROBLEM_ALPHA_THRESHOLD = std::is_same_v<T, float> ? T{1e-5} : T{1e-10};

template<class T>
inline constexpr T ROBIN_PROBLEM_ALPHA_THRESHOLD = std::is_same_v<T, float> ? T{1e-5} : T{1e-10};

}

namespace mechanical {

enum class boundary_condition_t : uint8_t {
    DISPLACEMENT = uint8_t(nonlocal::boundary_condition_t::FIRST_KIND),
    PRESSURE = uint8_t(nonlocal::boundary_condition_t::SECOND_KIND),
};

}

}

#endif