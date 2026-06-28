#include <talon/async_io_traits.hpp>
#include <functional>
#include <string>
#include <type_traits>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon::task;

// ============================================================================
// Test callables
// ============================================================================

static void FreeFunction(int, double) {}
static int FreeFunctionRet(double, char) { return 0; }

struct Functor {
    void operator()(int, int, int) {}
};

struct FunctorRet {
    std::string operator()(float) const { return ""; }
};

// ============================================================================
// FunctionTraits tests
// ============================================================================

TEST_CASE("FunctionTraits free function pointer") {
    using Traits = FunctionTraits<decltype(&FreeFunction)>;
    CHECK(std::is_same_v<Traits::ReturnType, void>);
    CHECK(Traits::kArgCount == 2);
    CHECK(std::is_same_v<Traits::ArgsTuple, std::tuple<int, double>>);
}

TEST_CASE("FunctionTraits free function pointer with return") {
    using Traits = FunctionTraits<decltype(&FreeFunctionRet)>;
    CHECK(std::is_same_v<Traits::ReturnType, int>);
    CHECK(Traits::kArgCount == 2);
    CHECK(std::is_same_v<Traits::ArgsTuple, std::tuple<double, char>>);
}

TEST_CASE("FunctionTraits std::function") {
    using F = std::function<void(int, char, double)>;
    using Traits = FunctionTraits<F>;
    CHECK(std::is_same_v<Traits::ReturnType, void>);
    CHECK(Traits::kArgCount == 3);
    CHECK(std::is_same_v<Traits::ArgsTuple, std::tuple<int, char, double>>);
}

TEST_CASE("FunctionTraits mutable member function") {
    using Traits = FunctionTraits<decltype(&Functor::operator())>;
    CHECK(std::is_same_v<Traits::ReturnType, void>);
    CHECK(Traits::kArgCount == 3);
}

TEST_CASE("FunctionTraits const member function") {
    using Traits = FunctionTraits<decltype(&FunctorRet::operator())>;
    CHECK(std::is_same_v<Traits::ReturnType, std::string>);
    CHECK(Traits::kArgCount == 1);
    CHECK(std::is_same_v<Traits::ArgsTuple, std::tuple<float>>);
}

TEST_CASE("FunctionTraits lambda via decltype") {
    auto lambda = [](int a, char b) -> bool { return a > static_cast<int>(b); };
    using Traits = FunctionTraits<decltype(lambda)>;
    CHECK(std::is_same_v<Traits::ReturnType, bool>);
    CHECK(Traits::kArgCount == 2);
}

TEST_CASE("FunctionTraits void lambda") {
    auto lambda = [](const std::string&) {};
    using Traits = FunctionTraits<decltype(lambda)>;
    CHECK(std::is_same_v<Traits::ReturnType, void>);
    CHECK(Traits::kArgCount == 1);
}

TEST_CASE("FunctionTraits zero-arg function") {
    using Traits = FunctionTraits<std::function<int()>>;
    CHECK(std::is_same_v<Traits::ReturnType, int>);
    CHECK(Traits::kArgCount == 0);
}

// ============================================================================
// SplitArgs tests
// ============================================================================

TEST_CASE("SplitArgs tuple") {
    using U = typename detail::SplitArgs<std::tuple<int, char, double>>::user_args_type;
    CHECK(std::is_same_v<U, std::tuple<char, double>>);
}

TEST_CASE("SplitArgs single-element tuple") {
    using U = typename detail::SplitArgs<std::tuple<int>>::user_args_type;
    CHECK(std::is_same_v<U, std::tuple<>>);
}

// ============================================================================
// kIsNothrowCallable tests
// ============================================================================

static void NothrowFunc(int) noexcept {}

TEST_CASE("kIsNothrowCallable noexcept function") {
    auto lambda = [](int) noexcept {};
    CHECK(kIsNothrowCallable<decltype(lambda), int>);
    CHECK(kIsNothrowCallable<decltype(&NothrowFunc), int>);
}

TEST_CASE("kIsNothrowCallable potentially-throwing function") {
    auto lambda = [](int) {};
    CHECK_FALSE(kIsNothrowCallable<decltype(lambda), int>);
}

TEST_CASE("kIsNothrowCallable std::function is not noexcept") {
    // std::function copy/assignment is not noexcept, so call is not noexcept
    CHECK_FALSE(kIsNothrowCallable<std::function<void(int)>, int>);
}
