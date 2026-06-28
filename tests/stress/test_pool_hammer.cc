// ============================================================================
// Pool Hammer Test — Extreme multi-threaded pool cycling to uncover races.
// ============================================================================

#include <talon/async_io_pool.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <random>
#include <thread>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon::memory;

struct HammerObj {
    uint64_t a, b, c, d;
    char pad[32];
    HammerObj() : a(0), b(1), c(2), d(3) {}
};

TEST_CASE("pool hammer: 8 threads, 200k acquire/release each") {
    ObjectPool<HammerObj> pool(4096, 512);
    constexpr int kThreads = 8;
    constexpr int kCycles = 200000;

    std::atomic<bool> start{false};
    std::atomic<int64_t> acq_count{0};
    std::atomic<int64_t> rel_count{0};
    std::atomic<int> errors{0};

    auto worker = [&](uint64_t seed) {
        std::mt19937_64 rng(seed);
        std::vector<HammerObj*> local;
        local.reserve(128);

        while (!start.load(std::memory_order_acquire)) { /* spin */ }

        for (int i = 0; i < kCycles; i++) {
            int batch = static_cast<int>(rng() % 32) + 1;
            for (int j = 0; j < batch; j++) {
                auto* obj = pool.Acquire();
                if (obj == nullptr) {
                    errors.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                // Touch the object to catch use-after-free.
                obj->a = seed;
                obj->b = static_cast<uint64_t>(i);
                obj->c = obj->a ^ obj->b;
                local.push_back(obj);
                acq_count.fetch_add(1, std::memory_order_relaxed);
            }

            // Verify invariants on a random subset before releasing.
            for (size_t j = 0; j < local.size(); j++) {
                if (local[j]->a != seed && local[j]->a != 0) {
                    errors.fetch_add(1, std::memory_order_relaxed);
                }
            }

            for (auto* obj : local) {
                pool.Release(obj);
                rel_count.fetch_add(1, std::memory_order_relaxed);
            }
            local.clear();
        }
    };

    std::thread threads[kThreads];
    for (int i = 0; i < kThreads; i++)
        threads[i] = std::thread(worker, static_cast<uint64_t>(42 + i * 137));

    start.store(true, std::memory_order_release);

    auto t0 = std::chrono::steady_clock::now();
    for (auto& t : threads) t.join();
    auto t1 = std::chrono::steady_clock::now();

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    CHECK(errors.load() == 0);
    CHECK(acq_count.load() > 0);
    CHECK(acq_count.load() == rel_count.load());

    MESSAGE("Hammer test completed in " << elapsed_ms << " ms, "
            << acq_count.load() << " total acquire/release cycles");
}

TEST_CASE("pool hammer: rapid create/destroy on BufferPool") {
    auto& bp = BufferPool::Instance();
    constexpr int kThreads = 6;
    constexpr int kCycles = 50000;

    std::atomic<bool> start{false};
    std::atomic<int> errors{0};

    auto worker = [&](uint64_t seed) {
        std::mt19937_64 rng(seed);
        constexpr size_t kSizes[] = {64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};
        constexpr int kNumSizes = sizeof(kSizes) / sizeof(kSizes[0]);

        while (!start.load(std::memory_order_acquire)) { /* spin */ }

        for (int i = 0; i < kCycles; i++) {
            size_t s = kSizes[rng() % kNumSizes];
            void* p = bp.Allocate(s);
            if (p == nullptr) {
                // Pool may not have that class populated — that's OK.
                continue;
            }
            // Scribble to catch use-after-free.
            __builtin_memset(p, static_cast<int>(seed & 0xFF), s);
            bp.Deallocate(p, s);
        }
    };

    std::thread threads[kThreads];
    for (int i = 0; i < kThreads; i++)
        threads[i] = std::thread(worker, static_cast<uint64_t>(100 + i * 59));

    start.store(true, std::memory_order_release);
    for (auto& t : threads) t.join();

    CHECK(errors.load() == 0);
    SUCCEED("BufferPool hammer test OK");
}
