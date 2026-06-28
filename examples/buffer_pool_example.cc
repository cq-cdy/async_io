// ============================================================================
// Example: Direct BufferPool and ObjectPool usage
//
// Demonstrates: BufferPool::Instance(), Allocate(), Deallocate(),
//               ClassForSize(), MakeKernelBuffer(), KernelBuf SBO/Pool/Heap
//               tier selection, ObjectPool create/destroy.
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include <talon/async_io.hpp>

using namespace talon;
using namespace talon::task;
using namespace talon::memory;

// ============================================================================
// Demo 1: BufferPool — buffer size classes and allocation
// ============================================================================
void DemoBufferPool() {
    auto& bp = BufferPool::Instance();
    printf("=== BufferPool: %zu size classes ===\n", bp.kNumClasses);

    // Show all size classes.
    for (size_t i = 0; i < bp.kNumClasses; i++) {
        size_t sz = kBufferPoolClassSizes[i];
        size_t cls = BufferPool::ClassForSize(sz);
        printf("  class %zu: size=%zu bytes\n", cls, sz);
    }

    // Allocate from each tier.
    printf("\nAllocating from different tiers:\n");
    for (size_t sz : {64UL, 128UL, 512UL, 4096UL, 65536UL}) {
        void* p = bp.Allocate(sz);
        printf("  %5zu bytes -> %p (class=%zu)\n",
               sz, p, BufferPool::ClassForSize(sz));
        if (p != nullptr) {
            // Scribble to prove the memory is valid.
            std::memset(p, 0xAB, sz);
            bp.Deallocate(p, sz);
        }
    }

    // Beyond max — returns nullptr.
    void* too_big = bp.Allocate(131072);
    printf("  131072 bytes -> %p (expected: nullptr — beyond max)\n", too_big);

    // Deallocate nullptr is safe.
    bp.Deallocate(nullptr, 64);
    printf("  Deallocate(nullptr) — safe no-op\n");
}

// ============================================================================
// Demo 2: KernelBuf — three-tier storage (SBO / Pool / Heap)
// ============================================================================
void DemoKernelBuf() {
    printf("\n=== KernelBuf: three-tier storage ===\n");

    // SBO tier (<= 256 bytes): zero heap allocation.
    {
        KernelBuf kb(256);
        printf("  SBO:    size=%zu, data=%p (inline storage)\n",
               kb.size, static_cast<void*>(kb.Data()));
        kb[0] = 'H'; kb[255] = '!';
    }

    // Pool tier (257-65536 bytes): BufferPool allocation.
    {
        KernelBuf kb(4096);
        printf("  Pool:   size=%zu, data=%p (BufferPool)\n",
               kb.size, static_cast<void*>(kb.Data()));
        kb[0] = 'P'; kb[4095] = 'Q';
    }

    // Heap tier (> 65536 bytes): std::vector fallback.
    {
        KernelBuf kb(131072);
        printf("  Heap:   size=%zu, data=%p (std::vector)\n",
               kb.size, static_cast<void*>(kb.Data()));
    }

    // Resize triggers tier transition (SBO -> Pool).
    {
        KernelBuf kb(128);
        kb[0] = 'X';
        printf("  Before Resize: size=%zu\n", kb.size);
        kb.Resize(4096);
        printf("  After  Resize: size=%zu (data preserved: '%c')\n",
               kb.size, kb[0]);
    }

    // Move constructor: ownership transfer, no copy.
    {
        KernelBuf k1(1024);
        k1[100] = 'M';
        KernelBuf k2(std::move(k1));
        printf("  Move: k2[100]='%c', k2.size=%zu, k1.size=%zu\n",
               k2[100], k2.size, k1.size);
    }

    // MakeKernelBuffer factory.
    auto ptr = MakeKernelBuffer(512);
    printf("  MakeKernelBuffer: size=%zu, data=%p\n",
           ptr->size, static_cast<void*>(ptr->Data()));
}

// ============================================================================
// Demo 3: ObjectPool — task object pooling
// ============================================================================
struct PoolDemo {
    int id;
    char label[32];
    PoolDemo() : id(-1) { std::memset(label, 0, sizeof(label)); }
};

void DemoObjectPool() {
    printf("\n=== ObjectPool<PoolDemo> ===\n");

    ObjectPool<PoolDemo> pool(128, 32);

    // Acquire objects.
    std::vector<PoolDemo*> objects;
    for (int i = 0; i < 10; i++) {
        auto* obj = pool.Acquire();
        if (obj != nullptr) {
            obj->id = i;
            snprintf(obj->label, sizeof(obj->label), "obj-%d", i);
            objects.push_back(obj);
        }
    }
    printf("  Acquired %zu objects\n", objects.size());

    // Show approximate free count.
    printf("  Approx free: %zu\n", pool.ApproximateFreeCount());

    // Release all.
    for (auto* obj : objects) {
        pool.Release(obj);
    }

    // Re-acquire — should get the same objects back (pooled).
    auto* a = pool.Acquire();
    auto* b = pool.Acquire();
    printf("  Re-acquired: a=%p b=%p (pooled)\n",
           static_cast<void*>(a), static_cast<void*>(b));
    pool.Release(a);
    pool.Release(b);
}

// ============================================================================
int main() {
    printf("Async I/O Memory Subsystem Demo\n");
    printf("================================\n\n");

    DemoBufferPool();
    DemoKernelBuf();
    DemoObjectPool();

    printf("\nAll memory subsystem demos complete.\n");
    return 0;
}
