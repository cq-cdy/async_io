#include <talon/async_io_constants.hpp>
#include <talon/async_io_task.hpp>
#include <atomic>
#include <string_view>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon;
using namespace talon::task;

// ============================================================================
// TaskState enumeration tests
// ============================================================================

TEST_CASE("TaskState integer values") {
    CHECK(static_cast<int>(TaskState::kReady) == 1000);
    CHECK(static_cast<int>(TaskState::kSubmitted) == 1001);
    CHECK(static_cast<int>(TaskState::kSuccess) == 1002);
    CHECK(static_cast<int>(TaskState::kFailed) == 1003);
    CHECK(static_cast<int>(TaskState::kTimeout) == 1004);
    CHECK(static_cast<int>(TaskState::kCanceled) == 1005);
}

TEST_CASE("TaskState names") {
    CHECK(TaskStateName(TaskState::kReady) == "READY");
    CHECK(TaskStateName(TaskState::kSubmitted) == "SUBMITTED");
    CHECK(TaskStateName(TaskState::kSuccess) == "SUCCESS");
    CHECK(TaskStateName(TaskState::kFailed) == "FAILED");
    CHECK(TaskStateName(TaskState::kTimeout) == "TIMEOUT");
    CHECK(TaskStateName(TaskState::kCanceled) == "CANCELED");
}

// ============================================================================
// TaskType enumeration tests
// ============================================================================

TEST_CASE("TaskType integer values") {
    CHECK(static_cast<int>(TaskType::kNone) == 0);
    CHECK(static_cast<int>(TaskType::kRead) == 1);
    CHECK(static_cast<int>(TaskType::kWrite) == 2);
    CHECK(static_cast<int>(TaskType::kAccept) == 3);
    CHECK(static_cast<int>(TaskType::kConnect) == 4);
    CHECK(static_cast<int>(TaskType::kClose) == 5);
    CHECK(static_cast<int>(TaskType::kTimeout) == 6);
    CHECK(static_cast<int>(TaskType::kCancel) == 7);
    CHECK(static_cast<int>(TaskType::kLinkTimeout) == 8);
    CHECK(static_cast<int>(TaskType::kTimeoutRemove) == 9);
    CHECK(static_cast<int>(TaskType::kTimeoutUpdate) == 10);
}

TEST_CASE("TaskType names") {
    CHECK(TaskTypeName(TaskType::kNone) == "NONE");
    CHECK(TaskTypeName(TaskType::kRead) == "READ");
    CHECK(TaskTypeName(TaskType::kWrite) == "WRITE");
    CHECK(TaskTypeName(TaskType::kAccept) == "ACCEPT");
    CHECK(TaskTypeName(TaskType::kConnect) == "CONNECT");
    CHECK(TaskTypeName(TaskType::kClose) == "CLOSE");
    CHECK(TaskTypeName(TaskType::kTimeout) == "TIMEOUT");
    CHECK(TaskTypeName(TaskType::kCancel) == "CANCEL");
    CHECK(TaskTypeName(TaskType::kLinkTimeout) == "LINK_TIMEOUT");
    CHECK(TaskTypeName(TaskType::kTimeoutRemove) == "TIMEOUT_REMOVE");
    CHECK(TaskTypeName(TaskType::kTimeoutUpdate) == "TIMEOUT_UPDATE");
}

// ============================================================================
// EventFlag tests
// ============================================================================

TEST_CASE("EventFlag values match constants") {
    CHECK(static_cast<uint64_t>(EventFlag::kNone) == 0);
    CHECK(static_cast<uint64_t>(EventFlag::kTimeout) == kTimeoutFlag);
    CHECK(static_cast<uint64_t>(EventFlag::kError) == kErrorFlag);
    CHECK(static_cast<uint64_t>(EventFlag::kCancel) == kCancelFlag);
    CHECK(static_cast<uint64_t>(EventFlag::kMask) == kEventMaskFlags);
}

// ============================================================================
// AsyncTask basic state tests
// ============================================================================

TEST_CASE("AsyncTask default state is kReady") {
    auto* t = CreateTaskWithHandler(5);
    CHECK(t->State() == TaskState::kReady);
    CHECK(t->Fd() == 5);
    CHECK(t->Type() == TaskType::kNone);
    CHECK_FALSE(t->RepeatForever());
    delete t;
}

TEST_CASE("AsyncTask set_task_type") {
    auto* t = CreateTaskWithHandler(3);
    t.SetTaskType(TaskType::kRead);
    CHECK(t->Type() == TaskType::kRead);
    t.SetTaskType(TaskType::kWrite);
    CHECK(t->Type() == TaskType::kWrite);
    delete t;
}

TEST_CASE("AsyncTask set_timeout") {
    auto* t = CreateTaskWithHandler(3);
    t.SetTimeout(5000);
    // Timeout is stored internally; we only verify no crash.
    delete t;
}

TEST_CASE("AsyncTask set_max_retry_count") {
    auto* t = CreateTaskWithHandler(1);
    t.SetMaxRetryCount(3);
    // repeat_when_failed returns true when max_retry_count > 0
    CHECK(t->RepeatWhenFailed());
    delete t;
}

TEST_CASE("AsyncTask set_max_retry_count zero") {
    auto* t = CreateTaskWithHandler(1);
    t.SetMaxRetryCount(0);
    CHECK_FALSE(t->RepeatWhenFailed());
    delete t;
}

TEST_CASE("AsyncTask set_repeat_forever") {
    auto* t = CreateTaskWithHandler(2);
    t.SetRepeatForever(true);
    CHECK(t->RepeatForever());
    t.SetRepeatForever(false);
    CHECK_FALSE(t->RepeatForever());
    delete t;
}

TEST_CASE("AsyncTask set_debug_str") {
    auto* t = CreateTaskWithHandler(0);
    t.SetDebugStr("test debug");
    CHECK(t->DebugStr() == "test debug");
    delete t;
}

TEST_CASE("AsyncTask buffer exists after construction") {
    auto* t = CreateTaskWithHandler(7);
    CHECK(t->Buffer() != nullptr);
    CHECK(t->Buffer()->size == kDefaultBufferSize);
    delete t;
}

TEST_CASE("AsyncTask SetBuffer and ResetBuffer") {
    auto* t = CreateTaskWithHandler(10);
    auto new_buf = MakeKernelBuffer(512);
    CHECK(new_buf->size == 512);
    t->SetBuffer(std::move(new_buf));
    CHECK(t->Buffer()->size == 512);
    t->ResetBuffer();
    CHECK(t->Buffer()->size == 0);
    delete t;
}

TEST_CASE("AsyncTask DeepCopyBufferFrom") {
    auto* t = CreateTaskWithHandler(0);
    KernelBuf src(128);
    src[0] = 'A'; src[127] = 'Z';
    t->DeepCopyBufferFrom(src);
    CHECK(t->Buffer()->size == 128);
    CHECK((*t->Buffer())[0] == 'A');
    CHECK((*t->Buffer())[127] == 'Z');
    delete t;
}
