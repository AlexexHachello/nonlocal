#ifndef METAMATH_OPERATORS_HPP
#define METAMATH_OPERATORS_HPP

#include <algorithm>
#include <functional>
#include <ranges>
#include <stdexcept>
#include <string>

namespace metamath::functions {

class _operators final {
    constexpr explicit _operators() noexcept = default;

    template<std::ranges::random_access_range Container>
    static void check_containers(Container& lhs, const Container& rhs, const std::string& operation_name);

    template<class Operation, std::ranges::random_access_range Container>
    static Container sum(const Container& lhs, const Container& rhs);

    template<class Operation, std::ranges::random_access_range Container>
    static Container& sum_assignment(Container& lhs, const Container& rhs);

    template<class Operation, std::ranges::random_access_range Container, class T>
    static Container product(Container container, const T& value);

    template<class Operation, std::ranges::random_access_range Container, class T>
    static Container& product_assignment(Container& container, const T& value);

public:
    template<std::ranges::random_access_range Container>
    friend Container operator+(const Container& lhs, const Container& rhs);

    template<std::ranges::random_access_range Container>
    friend Container operator-(const Container& lhs, const Container& rhs);

    template<std::ranges::random_access_range Container>
    friend Container& operator+=(Container& lhs, const Container& rhs);

    template<std::ranges::random_access_range Container>
    friend Container& operator-=(Container& lhs, const Container& rhs);

    template<std::ranges::random_access_range Container, class T>
    friend Container operator*(const Container& lhs, const T& rhs);

    template<class T, std::ranges::random_access_range Container>
    friend Container operator*(const T& rhs, const Container& lhs);

    template<std::ranges::random_access_range Container, class T>
    friend Container operator/(const Container& lhs, const T& rhs);

    template<std::ranges::random_access_range Container, class T>
    friend Container& operator*=(Container& lhs, const T& rhs);

    template<std::ranges::random_access_range Container, class T>
    friend Container& operator/=(Container& lhs, const T& rhs);
};

template<std::ranges::random_access_range Container>
void _operators::check_containers(Container& lhs, const Container& rhs, const std::string& operation_name) {
    if (lhs.size() != rhs.size())
        throw std::logic_error{"Error in " + operation_name + ". " +
                               "Containers sizes do not match: lhs.size() == " + std::to_string(lhs.size()) +
                                                            ", rhs.size() == " + std::to_string(rhs.size())};
}

template<class Operation, std::ranges::random_access_range Container>
Container _operators::sum(const Container& lhs, const Container& rhs) {
    static const std::string operation_name = typeid(Operation).name();
    check_containers(lhs, rhs, operation_name);
    Container result;
    std::transform(lhs.cbegin(), lhs.cend(), rhs.cbegin(), result.begin(), Operation{});
    return result;
}

template<class Operation, std::ranges::random_access_range Container>
Container& _operators::sum_assignment(Container& lhs, const Container& rhs) {
    static const std::string operation_name = typeid(Operation).name();
    check_containers(lhs, rhs, operation_name);
    std::transform(lhs.cbegin(), lhs.cend(), rhs.cbegin(), lhs.begin(), Operation{});
    return lhs;
}

template<class Operation, std::ranges::random_access_range Container, class T>
Container _operators::product(Container container, const T& value) {
    return product_assignment<Operation>(container, value);
}

template<class Operation, std::ranges::random_access_range Container, class T>
Container& _operators::product_assignment(Container& container, const T& value) {
    std::transform(container.cbegin(), container.cend(), container.begin(),
                   [operation = Operation{}, &value](const auto& lhs) { return operation(lhs, value); });
    return container;
}

template<std::ranges::random_access_range Container>
Container operator+(const Container& lhs, const Container& rhs) {
    return _operators::sum<std::plus<>>(lhs, rhs);
}

template<std::ranges::random_access_range Container>
Container operator-(const Container& lhs, const Container& rhs) {
    return _operators::sum<std::minus<>>(lhs, rhs);
}

template<std::ranges::random_access_range Container>
Container& operator+=(Container& lhs, const Container& rhs) {
    return _operators::sum_assignment<std::plus<>>(lhs, rhs);
}

template<std::ranges::random_access_range Container>
Container& operator-=(Container& lhs, const Container& rhs) {
    return _operators::sum_assignment<std::minus<>>(lhs, rhs);
}

template<std::ranges::random_access_range Container, class T>
Container operator*(const Container& lhs, const T& rhs) {
    return _operators::product<std::multiplies<>>(lhs, rhs);
}

template<class T, std::ranges::random_access_range Container>
Container operator*(const T& lhs, const Container& rhs) {
    return _operators::product<std::multiplies<>>(rhs, lhs);
}

template<std::ranges::random_access_range Container, class T>
Container operator/(const Container& lhs, const T& rhs) {
    return _operators::product<std::divides<>>(lhs, rhs);
}

template<std::ranges::random_access_range Container, class T>
Container& operator*=(Container& lhs, const T& rhs) {
    return _operators::product_assignment<std::multiplies<>>(lhs, rhs);
}

template<std::ranges::random_access_range Container, class T>
Container& operator/=(Container& lhs, const T& rhs) {
    return _operators::product_assignment<std::divides<>>(lhs, rhs);
}

}

#endif