#include <talon/async_io_result.hpp>
#include <chrono>
#include <string>
#include <thread>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon::task;

// ============================================================================
// IOResult tests
// ============================================================================

TEST_CASE("IOResult default values") {
    IOResult r(0, 1, false, "");
    CHECK(r.IoRet() == 0);
    CHECK(r.EventFd() == 1);
    CHECK_FALSE(r.IoDone());
    CHECK(r.ErrMsg().empty());
}

TEST_CASE("IOResult construction with all args") {
    IOResult r(42, 7, true, "success");
    CHECK(r.IoRet() == 42);
    CHECK(r.EventFd() == 7);
    CHECK(r.IoDone());
    CHECK(r.ErrMsg() == "success");
}

TEST_CASE("IOResult negative io_ret") {
    IOResult r(-1, 3, false, "error occurred");
    CHECK(r.IoRet() == -1);
    CHECK(r.EventFd() == 3);
    CHECK_FALSE(r.IoDone());
    CHECK(r.ErrMsg() == "error occurred");
}

TEST_CASE("IOResult large values") {
    IOResult r(2147483647, 65535, true, std::string(1024, 'x'));
    CHECK(r.IoRet() == 2147483647);
    CHECK(r.EventFd() == 65535);
    CHECK(r.IoDone());
    CHECK(r.ErrMsg().size() == 1024);
}

TEST_CASE("IOResult zero-length error message") {
    IOResult r(0, 0, false);
    CHECK(r.ErrMsg().empty());
}

// ============================================================================
// DoneState tests
// ============================================================================

TEST_CASE("DoneState initial state") {
    DoneState ds;
    CHECK_FALSE(ds.IsReady());
}

TEST_CASE("DoneState signal then check") {
    DoneState ds;
    ds.Signal();
    CHECK(ds.IsReady());
}

TEST_CASE("DoneState signal then wait zero") {
    DoneState ds;
    ds.Signal();
    CHECK(ds.Wait(0));
}

TEST_CASE("DoneState signal then wait negative (blocking)") {
    DoneState ds;
    ds.Signal();
    CHECK(ds.Wait(-1));
}

TEST_CASE("DoneState signal then wait positive") {
    DoneState ds;
    ds.Signal();
    CHECK(ds.Wait(100));
}

TEST_CASE("DoneState wait timeout (zero)") {
    DoneState ds;
    CHECK_FALSE(ds.Wait(0));
}

TEST_CASE("DoneState wait timeout (positive)") {
    DoneState ds;
    CHECK_FALSE(ds.Wait(1));
}

TEST_CASE("DoneState wait negative blocks until signal") {
    DoneState ds;
    std::thread t([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ds.Signal();
    });
    CHECK(ds.Wait(-1));
    t.join();
}

TEST_CASE("DoneState reset and reuse") {
    DoneState ds;
    ds.Signal();
    CHECK(ds.IsReady());
    ds.Reset();
    CHECK_FALSE(ds.IsReady());
    ds.Signal();
    CHECK(ds.IsReady());
}

TEST_CASE("DoneState idempotent signal") {
    DoneState ds;
    ds.Signal();
    ds.Signal();
    ds.Signal();
    CHECK(ds.IsReady());
    CHECK(ds.Wait(0));
}

TEST_CASE("DoneState thread signal with wait") {
    DoneState ds;
    std::thread t([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ds.Signal();
    });
    CHECK(ds.Wait(5000));
    t.join();
}

TEST_CASE("DoneState concurrent signals") {
    DoneState ds;
    std::thread t1([&] { ds.Signal(); });
    std::thread t2([&] { ds.Signal(); });
    t1.join(); t2.join();
    CHECK(ds.IsReady());
}

TEST_CASE("DoneState post-wait reset then reuse") {
    DoneState ds;
    ds.Signal();
    CHECK(ds.Wait(-1));
    ds.Reset();
    CHECK_FALSE(ds.IsReady());
    std::thread t([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        ds.Signal();
    });
    CHECK(ds.Wait(1000));
    t.join();
}
