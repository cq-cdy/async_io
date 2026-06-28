#include <talon/async_io.hpp>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon;

TEST_CASE("adversarial AddTask(nullptr)") {
    IOHandler io; if (!io.initialized()) return;
    CHECK_FALSE(io.AddTask(nullptr));
    io.RequestShutdown(); io.Join();
}

TEST_CASE("adversarial Cancel with nullptr uring") {
    auto* t = task::CreateTaskWithHandler(1);
    t->set_task_type(task::TaskType::kRead);
    CHECK(t->Cancel(nullptr) == 0);
    delete t;
}

TEST_CASE("adversarial WaitForCompletion never submitted") {
    auto* t = task::CreateTaskWithHandler(0);
    auto r = t->WaitForCompletion(100);
    CHECK_FALSE(r.iodone());
    CHECK(r.err_msg().find("not detached") != std::string::npos);
    delete t;
}
