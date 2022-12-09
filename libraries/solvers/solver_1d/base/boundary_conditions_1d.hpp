#ifndef BOUNDARY_CONDITION_1D_HPP
#define BOUNDARY_CONDITION_1D_HPP

namespace nonlocal {

class boundary_condition_1d {
protected:
    constexpr explicit boundary_condition_1d() noexcept = default;

public:
    virtual ~boundary_condition_1d() noexcept = default;
};

template<class T>
class first_kind_1d : public virtual boundary_condition_1d {
protected:
    constexpr explicit first_kind_1d() noexcept = default;

public:
    ~first_kind_1d() noexcept override = default;
    virtual T operator()() const = 0;
};

template<class T>
class second_kind_1d : public virtual boundary_condition_1d {
protected:
    constexpr explicit second_kind_1d() noexcept = default;

public:
    ~second_kind_1d() noexcept override = default;
    virtual T operator()() const = 0;
};

}

#endif