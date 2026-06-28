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
    CHECK(t->state() == TaskState::kReady);
    CHECK(t->fd() == 5);
    CHECK(t->type() == TaskType::kNone);
    CHECK_FALSE(t->repeat_forever());
    delete t;
}

TEST_CASE("AsyncTask set_task_type") {
    auto* t = CreateTaskWithHandler(3);
    t->set_task_type(TaskType::kRead);
    CHECK(t->type() == TaskType::kRead);
    t->set_task_type(TaskType::kWrite);
    CHECK(t->type() == TaskType::kWrite);
    delete t;
}

TEST_CASE("AsyncTask set_timeout") {
    auto* t = CreateTaskWithHandler(3);
    t->set_timeout(5000);
    // Timeout is stored internally; we only verify no crash.
    delete t;
}

TEST_CASE("AsyncTask set_max_retry_count") {
    auto* t = CreateTaskWithHandler(1);
    t->set_max_retry_count(3);
    // repeat_when_failed returns true when max_retry_count > 0
    CHECK(t->repeat_when_failed());
    delete t;
}

TEST_CASE("AsyncTask set_max_retry_count zero") {
    auto* t = CreateTaskWithHandler(1);
    t->set_max_retry_count(0);
    CHECK_FALSE(t->repeat_when_failed());
    delete t;
}

TEST_CASE("AsyncTask set_repeat_forever") {
    auto* t = CreateTaskWithHandler(2);
    t->set_repeat_forever(true);
    CHECK(t->repeat_forever());
    t->set_repeat_forever(false);
    CHECK_FALSE(t->repeat_forever());
    delete t;
}

TEST_CASE("AsyncTask set_debug_str") {
    auto* t = CreateTaskWithHandler(0);
    t->set_debug_str("test debug");
    CHECK(t->debug_str() == "test debug");
    delete t;
}

TEST_CASE("AsyncTask buffer exists after construction") {
    auto* t = CreateTaskWithHandler(7);
    CHECK(t->buffer() != nullptr);
    CHECK(t->buffer()->size_ == kDefaultBufferSize);
    delete t;
}

TEST_CASE("AsyncTask SetBuffer and ResetBuffer") {
    auto* t = CreateTaskWithHandler(10);
    auto new_buf = MakeKernelBuffer(512);
    CHECK(new_buf->size_ == 512);
    t->SetBuffer(std::move(new_buf));
    CHECK(t->buffer()->size_ == 512);
    t->ResetBuffer();
    CHECK(t->buffer()->size_ == 0);
    delete t;
}

TEST_CASE("AsyncTask DeepCopyBufferFrom") {
    auto* t = CreateTaskWithHandler(0);
    KernelBuf src(128);
    src[0] = 'A'; src[127] = 'Z';
    t->DeepCopyBufferFrom(src);
    CHECK(t->buffer()->size_ == 128);
    CHECK((*t->buffer())[0] == 'A');
    CHECK((*t->buffer())[127] == 'Z');
    delete t;
}
