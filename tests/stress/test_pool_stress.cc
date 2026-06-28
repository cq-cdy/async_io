#include <talon/async_io_pool.hpp>
#include <atomic>
#include <thread>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon::memory;

struct StressObj {
    int a, b, c, d;
    char padding[48];
    StressObj() : a(0), b(0), c(0), d(0) {}
};

TEST_CASE("ObjectPool stress multi-threaded acquire/release") {
    ObjectPool<StressObj> pool(1024, 256);
    constexpr int kIterations = 10000;
    constexpr int kThreads = 4;

    std::atomic<bool> start{false};
    std::atomic<int> total_acq{0};
    std::atomic<int> total_rel{0};

    auto worker = [&](int tid) {
        std::vector<StressObj*> local;
        local.reserve(64);
        while (!start.load(std::memory_order_acquire)) { /* spin */ }
        for (int i = 0; i < kIterations; i++) {
            // Acquire a few objects.
            for (int j = 0; j < 8; j++) {
                auto* obj = pool.Acquire();
                if (obj) {
                    obj->a = tid;
                    obj->b = i;
                    local.push_back(obj);
                    total_acq.fetch_add(1, std::memory_order_relaxed);
                }
            }
            // Release them.
            for (auto* obj : local) {
                pool.Release(obj);
                total_rel.fetch_add(1, std::memory_order_relaxed);
            }
            local.clear();
        }
    };

    std::thread threads[kThreads];
    for (int i = 0; i < kThreads; i++) threads[i] = std::thread(worker, i);
    start.store(true, std::memory_order_release);
    for (auto& t : threads) t.join();

    CHECK(total_acq.load() > 0);
    CHECK(total_acq.load() == total_rel.load());
}

TEST_CASE("BufferPool stress concurrent alloc/free") {
    auto& bp = BufferPool::Instance();
    constexpr int kIterations = 5000;
    constexpr int kThreads = 4;

    std::atomic<bool> start{false};
    std::atomic<int> errors{0};

    auto worker = [&]() {
        while (!start.load(std::memory_order_acquire)) { /* spin */ }
        for (int i = 0; i < kIterations; i++) {
            for (size_t s : {64, 128, 256, 512, 1024, 2048}) {
                void* p = bp.Allocate(s);
                if (p == nullptr) { errors.fetch_add(1); continue; }
                bp.Deallocate(p, s);
            }
        }
    };

    std::thread threads[kThreads];
    for (int i = 0; i < kThreads; i++) threads[i] = std::thread(worker);
    start.store(true, std::memory_order_release);
    for (auto& t : threads) t.join();

    CHECK(errors.load() == 0);
}

TEST_CASE("TreiberStack concurrent push/pop") {
    TreiberStack stack;
    constexpr int kItemsPerThread = 1000;
    constexpr int kThreads = 4;

    // Allocate nodes on the stack (they won't be freed — simple test).
    PoolNode nodes[kThreads * kItemsPerThread];

    std::atomic<int> push_count{0};
    std::atomic<int> pop_count{0};

    auto pusher = [&](int tid) {
        for (int i = 0; i < kItemsPerThread; i++) {
            int idx = tid * kItemsPerThread + i;
            nodes[idx].next.store(nullptr);
            stack.Push(&nodes[idx]);
            push_count.fetch_add(1, std::memory_order_relaxed);
        }
    };

    auto popper = [&]() {
        for (int i = 0; i < kItemsPerThread; i++) {
            auto* n = stack.Pop();
            if (n != nullptr) pop_count.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::thread pt[kThreads];
    for (int i = 0; i < kThreads; i++) pt[i] = std::thread(pusher, i);
    for (auto& t : pt) t.join();

    std::thread ct[kThreads];
    for (int i = 0; i < kThreads; i++) ct[i] = std::thread(popper);
    for (auto& t : ct) t.join();

    while (stack.Pop() != nullptr) pop_count.fetch_add(1);
    CHECK(push_count.load() == pop_count.load());
}
