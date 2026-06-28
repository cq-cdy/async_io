#include <talon/async_io_result.hpp>
#include <atomic>
#include <thread>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon::task;

TEST_CASE("DoneState initial") { DoneState ds; CHECK_FALSE(ds.IsReady()); }

TEST_CASE("DoneState signal+check") { DoneState ds; ds.Signal(); CHECK(ds.IsReady()); }

TEST_CASE("DoneState signal+wait") {
    DoneState ds; ds.Signal();
    CHECK(ds.Wait(0)); CHECK(ds.Wait(-1)); CHECK(ds.Wait(100));
}

TEST_CASE("DoneState wait timeout") { DoneState ds; CHECK_FALSE(ds.Wait(1)); }

TEST_CASE("DoneState reset+reuse") {
    DoneState ds; ds.Signal(); CHECK(ds.IsReady());
    ds.Reset(); CHECK_FALSE(ds.IsReady());
    ds.Signal(); CHECK(ds.IsReady());
}

TEST_CASE("DoneState idempotent signal") {
    DoneState ds; ds.Signal(); ds.Signal(); ds.Signal(); CHECK(ds.IsReady());
}

TEST_CASE("DoneState thread signal") {
    DoneState ds;
    std::thread t([&] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); ds.Signal(); });
    CHECK(ds.Wait(5000)); t.join();
}
