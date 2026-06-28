#include <talon/async_io.hpp>
#include <atomic>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon;
using namespace talon::task;

static std::string MakeTempFile() {
    char t[] = "/tmp/async_io_leak_XXXXXX";
    int fd = mkstemp(t); if (fd >= 0) { write(fd, "data\n", 5); close(fd); }
    return t;
}

TEST_CASE("adversarial create/destroy 1000 tasks") {
    for (int i = 0; i < 1000; i++) {
        auto* t = CreateTaskWithHandler(i % 100);
        t.SetTaskType(TaskType::kRead);
        delete t;
    }
    SUCCEED("1000 create/destroy ok");
}

TEST_CASE("adversarial full I/O cycle 500 tasks") {
    std::string p = MakeTempFile(); REQUIRE_FALSE(p.empty());
    IOHandler io; REQUIRE(io.Initialized());
    std::atomic<int> done{0};
    auto h = [&](KernelBuf*) { done.fetch_add(1); };
    for (int i = 0; i < 500; i++) {
        int fd = open(p.c_str(), O_RDONLY); if (fd < 0) continue;
        auto* t = CreateTaskWithHandler(fd, h); t.SetTaskType(TaskType::kRead);
        io.AddTask(t);
    }
    io.Flush();
    for (int w = 0; w < 10000 && done.load() < 500; w++) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(done.load() == 500);
    io.RequestShutdown(); io.Join(); unlink(p.c_str());
}

TEST_CASE("adversarial BufferPool 10000 cycles") {
    for (int c = 0; c < 10000; c++)
        for (size_t s : {64, 128, 256, 512, 1024, 2048, 4096})
            { auto b = std::make_unique<KernelBuf>(s); REQUIRE(b->Data() != nullptr); }
    SUCCEED("10000 alloc/free ok");
}
