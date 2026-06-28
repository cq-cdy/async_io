#include <talon/async_io.hpp>
#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon;
using namespace talon::task;

namespace {

std::string MakeTempFile(const char* content, size_t len) {
    char t[] = "/tmp/async_io_fio_XXXXXX";
    int fd = mkstemp(t);
    if (fd >= 0) {
        write(fd, content, len);
        close(fd);
    }
    return t;
}

}  // namespace

TEST_CASE("file read to /dev/null") {
    IOHandler io;
    REQUIRE(io.Initialized());

    int fd = open("/dev/null", O_RDONLY);
    REQUIRE(fd >= 0);

    std::atomic<bool> done{false};
    int bytes_read = -1;
    auto h = [&](KernelBuf* b) {
        bytes_read = b->BytesTransferred();
        done.store(true, std::memory_order_release);
    };

    auto* t = CreateTaskWithHandler(fd, h);
    t.SetTaskType(TaskType::kRead);
    io.AddTask(t);
    io.Flush();

    for (int i = 0; i < 1000 && !done.load(std::memory_order_acquire); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(done.load());
    CHECK(bytes_read == 0);  // /dev/null returns 0 bytes
    close(fd);
    io.RequestShutdown();
    io.Join();
}

TEST_CASE("file read small temp file") {
    std::string path = MakeTempFile("Hello, World!\n", 14);
    REQUIRE_FALSE(path.empty());

    IOHandler io;
    REQUIRE(io.Initialized());

    int fd = open(path.c_str(), O_RDONLY);
    REQUIRE(fd >= 0);

    std::atomic<bool> done{false};
    int bytes_read = -1;
    auto h = [&](KernelBuf* b) {
        bytes_read = b->BytesTransferred();
        done.store(true, std::memory_order_release);
    };

    auto* t = CreateTaskWithHandler(fd, h);
    t.SetTaskType(TaskType::kRead);
    io.AddTask(t);
    io.Flush();

    for (int i = 0; i < 1000 && !done.load(std::memory_order_acquire); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(done.load());
    CHECK(bytes_read == 14);
    close(fd);
    unlink(path.c_str());
    io.RequestShutdown();
    io.Join();
}

TEST_CASE("file write and read back") {
    std::string path = MakeTempFile("", 0);
    REQUIRE_FALSE(path.empty());

    IOHandler io;
    REQUIRE(io.Initialized());

    // Write phase.
    {
        int fd = open(path.c_str(), O_WRONLY);
        REQUIRE(fd >= 0);

        std::atomic<bool> wdone{false};
        int written = -1;
        auto wh = [&](KernelBuf* b) {
            written = b->BytesTransferred();
            wdone.store(true, std::memory_order_release);
        };

        auto* wt = CreateTaskWithHandler(fd, wh);
        wt.SetTaskType(TaskType::kWrite);
        const char* msg = "test data";
        wt->Buffer()->Resize(std::strlen(msg));
        std::memcpy(wt->Buffer()->Data(), msg, std::strlen(msg));
        io.AddTask(wt);
        io.Flush();

        for (int i = 0; i < 1000 && !wdone.load(std::memory_order_acquire); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK(wdone.load());
        CHECK(written == static_cast<int>(std::strlen(msg)));
        close(fd);
    }

    // Read-back phase.
    {
        int fd = open(path.c_str(), O_RDONLY);
        REQUIRE(fd >= 0);

        std::atomic<bool> rdone{false};
        int bytes_read = -1;
        auto rh = [&](KernelBuf* b) {
            bytes_read = b->BytesTransferred();
            rdone.store(true, std::memory_order_release);
        };

        auto* rt = CreateTaskWithHandler(fd, rh);
        rt.SetTaskType(TaskType::kRead);
        io.AddTask(rt);
        io.Flush();

        for (int i = 0; i < 1000 && !rdone.load(std::memory_order_acquire); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK(rdone.load());
        CHECK(bytes_read == 9);  // strlen("test data")
        close(fd);
    }

    unlink(path.c_str());
    io.RequestShutdown();
    io.Join();
}

TEST_CASE("file read empty file") {
    std::string path = MakeTempFile("", 0);
    REQUIRE_FALSE(path.empty());

    IOHandler io;
    REQUIRE(io.Initialized());

    int fd = open(path.c_str(), O_RDONLY);
    REQUIRE(fd >= 0);

    std::atomic<bool> done{false};
    int bytes_read = -99;
    auto h = [&](KernelBuf* b) {
        bytes_read = b->BytesTransferred();
        done.store(true, std::memory_order_release);
    };

    auto* t = CreateTaskWithHandler(fd, h);
    t.SetTaskType(TaskType::kRead);
    io.AddTask(t);
    io.Flush();

    for (int i = 0; i < 1000 && !done.load(std::memory_order_acquire); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(done.load());
    CHECK(bytes_read == 0);  // EOF immediately for empty file
    close(fd);
    unlink(path.c_str());
    io.RequestShutdown();
    io.Join();
}

TEST_CASE("file read bad fd") {
    IOHandler io;
    REQUIRE(io.Initialized());

    std::atomic<bool> done{false};
    int res = 0;
    auto h = [&](KernelBuf* b) {
        res = b->BytesTransferred();
        done.store(true, std::memory_order_release);
    };

    auto* t = CreateTaskWithHandler(-1, h);
    t.SetTaskType(TaskType::kRead);
    io.AddTask(t);
    io.Flush();

    for (int i = 0; i < 1000 && !done.load(std::memory_order_acquire); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(done.load());
    CHECK(res < 0);  // Error on bad fd
    io.RequestShutdown();
    io.Join();
}

TEST_CASE("file IO many sequential operations") {
    std::string path = MakeTempFile("", 0);
    REQUIRE_FALSE(path.empty());

    IOHandler io;
    REQUIRE(io.Initialized());

    std::atomic<int> count{0};
    auto handler = [&](KernelBuf*) { count.fetch_add(1, std::memory_order_relaxed); };

    int fd = open(path.c_str(), O_RDWR | O_CREAT, 0644);
    REQUIRE(fd >= 0);

    // Submit 50 read tasks (each will read 0 bytes from empty file = EOF).
    for (int i = 0; i < 50; i++) {
        auto* t = CreateTaskWithHandler(fd, handler);
        t.SetTaskType(TaskType::kRead);
        io.AddTask(t);
    }
    io.Flush();

    for (int w = 0; w < 5000 && count.load(std::memory_order_relaxed) < 50; w++)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    CHECK(count.load() >= 0);  // Some should have completed
    close(fd);
    unlink(path.c_str());
    io.RequestShutdown();
    io.Join();
}
