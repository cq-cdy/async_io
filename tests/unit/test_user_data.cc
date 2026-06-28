#include <talon/async_io_constants.hpp>
#include <talon/async_io_task.hpp>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon;
using namespace talon::task;

TEST_CASE("user_data flag isolation") {
    CHECK((kTimeoutFlag & kErrorFlag) == 0);
    CHECK((kTimeoutFlag & kCancelFlag) == 0);
    CHECK((kErrorFlag & kCancelFlag) == 0);
}

TEST_CASE("user_data pointer preservation") {
    auto* t = CreateTaskWithHandler(1);
    uintptr_t p = reinterpret_cast<uintptr_t>(t);
    uint64_t e = p | kTimeoutFlag;
    CHECK((e & kTimeoutFlag) != 0);
    CHECK(reinterpret_cast<decltype(t)>(e & ~kEventMaskFlags) == t);
    delete t;
}

TEST_CASE("user_data EventFlag matches bits") {
    CHECK(static_cast<uint64_t>(EventFlag::kTimeout) == kTimeoutFlag);
    CHECK(static_cast<uint64_t>(EventFlag::kError) == kErrorFlag);
    CHECK(static_cast<uint64_t>(EventFlag::kCancel) == kCancelFlag);
    CHECK(static_cast<uint64_t>(EventFlag::kNone) == uint64_t(0));
}

TEST_CASE("user_data constexpr constants") {
    static_assert(kTimeoutBit == 63, ""); static_assert(kErrorBit == 62, "");
    static_assert(kCancelBit == 61, "");  static_assert(kDefaultBufferSize == 2048, "");
}
