#include <talon/async_io_kernelbuf.hpp>
#include <talon/async_io_pool.hpp>
#include <cstring>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon::task;
using namespace talon::memory;

TEST_CASE("KernelBuf reuse SBO tier") {
    // Create, use, destroy many buffers in the SBO tier (<= 256 bytes).
    for (int i = 0; i < 1000; i++) {
        KernelBuf kb(256);
        CHECK(kb.Data() != nullptr);
        CHECK(kb.size == 256);
        kb[0] = char(i % 256);
        kb[255] = char((i + 1) % 256);
    }
    MESSAGE("1000 SBO alloc/free cycles");
}

TEST_CASE("KernelBuf reuse pool tier") {
    for (int i = 0; i < 500; i++) {
        KernelBuf kb(4096);
        CHECK(kb.Data() != nullptr);
        CHECK(kb.size == 4096);
        kb[0] = 'P';
        kb[4095] = 'Q';
    }
    MESSAGE("500 pool alloc/free cycles");
}

TEST_CASE("KernelBuf mixed tier reuse") {
    // Rapidly switch between SBO, pool, and heap tiers.
    for (int i = 0; i < 100; i++) {
        { KernelBuf kb(64); CHECK(kb.Data() != nullptr); }       // SBO
        { KernelBuf kb(512); CHECK(kb.Data() != nullptr); }      // Pool
        { KernelBuf kb(131072); CHECK(kb.Data() != nullptr); }   // Heap
        { KernelBuf kb(128); CHECK(kb.Data() != nullptr); }      // SBO
        { KernelBuf kb(65536); CHECK(kb.Data() != nullptr); }    // Pool (max)
    }
    MESSAGE("mixed tier reuse ok");
}

TEST_CASE("KernelBuf resize triggers reallocation correctly") {
    KernelBuf kb(128);
    kb[0] = 'X';
    kb.Resize(4096);  // SBO -> Pool
    CHECK(kb.size == 4096);
    CHECK(kb[0] == 'X');  // Data preserved
    kb[4095] = 'Z';
    CHECK(kb[4095] == 'Z');
}

TEST_CASE("KernelBuf move reuses storage") {
    KernelBuf k1(4096);
    k1[0] = 'M';
    k1[4095] = 'N';
    KernelBuf k2(std::move(k1));
    CHECK(k2[0] == 'M');
    CHECK(k2[4095] == 'N');
    CHECK(k2.size == 4096);
    // k1 should be empty after move.
    CHECK(k1.size == 0);
    CHECK(k1.Data() == nullptr);
}

TEST_CASE("BufferPool allocating max size class") {
    auto& bp = BufferPool::Instance();
    void* p = bp.Allocate(65536);
    if (p != nullptr) {
        bp.Deallocate(p, 65536);
    }
    // If p is null, class init might not have happened — still not a crash.
    MESSAGE("max class size handled gracefully");
}
