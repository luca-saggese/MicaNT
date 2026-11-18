/* C++ compatibility helpers for the C-derived command interpreter port. */
#pragma once

#include <type_traits>

template<class A, class B>
constexpr auto min(const A& left, const B& right) noexcept -> std::common_type_t<A, B>
{
    using Result = std::common_type_t<A, B>;
    return static_cast<Result>(left) < static_cast<Result>(right)
        ? static_cast<Result>(left) : static_cast<Result>(right);
}

template<class A, class B>
constexpr auto max(const A& left, const B& right) noexcept -> std::common_type_t<A, B>
{
    using Result = std::common_type_t<A, B>;
    return static_cast<Result>(left) > static_cast<Result>(right)
        ? static_cast<Result>(left) : static_cast<Result>(right);
}
