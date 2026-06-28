#ifndef TALON_ASYNC_IO_TRAITS_HPP_
#define TALON_ASYNC_IO_TRAITS_HPP_

#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>

namespace talon {
inline namespace v2_2_0 {
namespace task {

template <typename T>
struct FunctionTraits : FunctionTraits<decltype(&T::operator())> {};

template <typename R, typename... Args>
struct FunctionTraits<R (*)(Args...)> {
    using ReturnType = R;
    using ArgsTuple = std::tuple<Args...>;
    static constexpr size_t kArgCount = sizeof...(Args);
};

template <typename R, typename... Args>
struct FunctionTraits<std::function<R(Args...)>> {
    using ReturnType = R;
    using ArgsTuple = std::tuple<Args...>;
    static constexpr size_t kArgCount = sizeof...(Args);
};

template <typename ClassType, typename R, typename... Args>
struct FunctionTraits<R (ClassType::*)(Args...)> {
    using ReturnType = R;
    using ArgsTuple = std::tuple<Args...>;
    static constexpr size_t kArgCount = sizeof...(Args);
};

template <typename ClassType, typename R, typename... Args>
struct FunctionTraits<R (ClassType::*)(Args...) const> {
    using ReturnType = R;
    using ArgsTuple = std::tuple<Args...>;
    static constexpr size_t kArgCount = sizeof...(Args);
};

namespace detail {

template <typename Tuple>
struct SplitArgs;

template <typename First, typename... Rest>
struct SplitArgs<std::tuple<First, Rest...>> {
    using user_args_type = std::tuple<Rest...>;
};

}  // namespace detail

template <typename F, typename... Args>
inline constexpr bool kIsNothrowCallable =
    noexcept(std::declval<std::decay_t<F>>()(std::declval<Args>()...));

}  // namespace task
}  // namespace v2_2_0
}  // namespace talon

#endif  // TALON_ASYNC_IO_TRAITS_HPP_
