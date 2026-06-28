#include <talon/async_io_pool.hpp>
#include <atomic>
#include <thread>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon::memory;
using namespace talon;

// Simple test object for pool testing.
struct TestObj {
    int value = 0;
    TestObj() = default;
    explicit TestObj(int v) : value(v) {}
};

// ============================================================================
// TreiberStack tests
// ============================================================================

TEST_CASE("TreiberStack empty on construction") {
    TreiberStack stack;
    CHECK(stack.Empty());
    CHECK(stack.Pop() == nullptr);
}

TEST_CASE("TreiberStack push and pop single") {
    TreiberStack stack;
    PoolNode node;
    node.next.store(nullptr);
    stack.Push(&node);
    CHECK_FALSE(stack.Empty());
    auto* popped = stack.Pop();
    CHECK(popped == &node);
    CHECK(stack.Empty());
}

TEST_CASE("TreiberStack push and pop multiple") {
    TreiberStack stack;
    PoolNode nodes[5];
    for (int i = 0; i < 5; i++) {
        nodes[i].next.store(nullptr);
        stack.Push(&nodes[i]);
    }
    int count = 0;
    while (stack.Pop() != nullptr) count++;
    CHECK(count == 5);
    CHECK(stack.Empty());
}

TEST_CASE("TreiberStack push chain") {
    TreiberStack stack;
    PoolNode nodes[4];
    for (int i = 0; i < 3; i++) {
        nodes[i].next.store(&nodes[i + 1]);
    }
    nodes[3].next.store(nullptr);
    stack.PushChain(&nodes[0], &nodes[3]);
    CHECK_FALSE(stack.Empty());
    int count = 0;
    while (stack.Pop() != nullptr) count++;
    CHECK(count == 4);
}

// ============================================================================
// ObjectPool tests
// ============================================================================

TEST_CASE("ObjectPool acquire and release") {
    ObjectPool<TestObj> pool(16, 8);
    std::vector<TestObj*> objs;
    for (int i = 0; i < 100; i++) {
        auto* obj = pool.Acquire();
        REQUIRE(obj != nullptr);
        objs.push_back(obj);
    }
    for (auto* obj : objs) pool.Release(obj);
    MESSAGE("100 acquire/release cycles");
}

TEST_CASE("ObjectPool reuse after release") {
    ObjectPool<TestObj> pool(4, 4);
    auto* a = pool.Acquire();
    REQUIRE(a != nullptr);
    pool.Release(a);
    auto* b = pool.Acquire();
    CHECK(a == b);  // Should get the same object back
    pool.Release(b);
}

TEST_CASE("ObjectPool approximate free count") {
    ObjectPool<TestObj> pool(64, 16);
    // Initially, all objects are free.
    size_t free = pool.ApproximateFreeCount();
    CHECK(free >= 64);
    auto* a = pool.Acquire();
    REQUIRE(a != nullptr);
    // After acquiring one object, the free count should be non-zero
    // and at most the initial count (may equal free if acquired from TLS cache).
    size_t after = pool.ApproximateFreeCount();
    CHECK(after <= free);
    pool.Release(a);
    // After releasing, free count should approximately match the initial count.
    size_t restored = pool.ApproximateFreeCount();
    CHECK(restored >= free - 1);
}

TEST_CASE("ObjectPool empty capacity zero") {
    ObjectPool<TestObj> pool(0, 4);
    auto* obj = pool.Acquire();
    CHECK(obj != nullptr);  // Grow batch kicks in
    pool.Release(obj);
}

// ============================================================================
// BufferPool tests
// ============================================================================

TEST_CASE("BufferPool ClassForSize") {
    CHECK(BufferPool::ClassForSize(0) == 0);
    CHECK(BufferPool::ClassForSize(1) == 0);
    CHECK(BufferPool::ClassForSize(64) == 0);
    CHECK(BufferPool::ClassForSize(65) == 1);
    CHECK(BufferPool::ClassForSize(128) == 1);
    CHECK(BufferPool::ClassForSize(256) == 2);
    CHECK(BufferPool::ClassForSize(512) == 3);
    CHECK(BufferPool::ClassForSize(1024) == 4);
    CHECK(BufferPool::ClassForSize(2048) == 5);
    CHECK(BufferPool::ClassForSize(4096) == 6);
    CHECK(BufferPool::ClassForSize(8192) == 7);
    CHECK(BufferPool::ClassForSize(16384) == 8);
    CHECK(BufferPool::ClassForSize(32768) == 9);
    CHECK(BufferPool::ClassForSize(65536) == 10);
    CHECK(BufferPool::ClassForSize(65537) == 11);  // Beyond max
}

TEST_CASE("BufferPool allocate and deallocate") {
    auto& bp = BufferPool::Instance();
    void* p = bp.Allocate(64);
    REQUIRE(p != nullptr);
    bp.Deallocate(p, 64);
}

TEST_CASE("BufferPool allocate different sizes") {
    auto& bp = BufferPool::Instance();
    for (size_t s : {64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384}) {
        void* p = bp.Allocate(s);
        REQUIRE(p != nullptr);
        bp.Deallocate(p, s);
    }
}

TEST_CASE("BufferPool beyond max falls back") {
    auto& bp = BufferPool::Instance();
    void* p = bp.Allocate(131072);
    CHECK(p == nullptr);  // Beyond max pooled size
}

TEST_CASE("BufferPool deallocate nullptr is safe") {
    auto& bp = BufferPool::Instance();
    bp.Deallocate(nullptr, 64);
    MESSAGE("no crash");
}

TEST_CASE("BufferPool many alloc/free cycles") {
    auto& bp = BufferPool::Instance();
    for (int c = 0; c < 1000; c++) {
        for (size_t s : {64, 128, 256, 512, 1024}) {
            void* p = bp.Allocate(s);
            REQUIRE(p != nullptr);
            bp.Deallocate(p, s);
        }
    }
    MESSAGE("1000 cycles ok");
}

// ============================================================================
// GetTaskPool tests
// ============================================================================

TEST_CASE("GetTaskPool singleton") {
    auto& p1 = task::GetTaskPool<TestObj>();
    auto& p2 = task::GetTaskPool<TestObj>();
    CHECK(&p1 == &p2);  // Same pool
}

struct TestObj2 { int x = 0; };

TEST_CASE("GetTaskPool different types") {
    auto& p1 = task::GetTaskPool<TestObj>();
    auto& p2 = task::GetTaskPool<TestObj2>();
    CHECK(static_cast<const void*>(&p1) != static_cast<const void*>(&p2));  // Different pools for different types
}
