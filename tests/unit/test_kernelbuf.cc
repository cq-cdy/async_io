#include <talon/async_io_kernelbuf.hpp>
#include <talon/async_io_constants.hpp>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace talon::task;

TEST_CASE("KernelBuf SBO tier") {
    KernelBuf kb(64);
    CHECK(kb.size == 64); CHECK(kb.Data() != nullptr);
    kb[0] = 'A'; kb[63] = 'Z'; CHECK(kb[0] == 'A'); CHECK(kb[63] == 'Z');
}

TEST_CASE("KernelBuf pool tier") {
    KernelBuf kb(1024);
    CHECK(kb.size == 1024); CHECK(kb.Data() != nullptr);
    kb[0] = 'X'; kb[1023] = 'Y'; CHECK(kb[0] == 'X'); CHECK(kb[1023] == 'Y');
}

TEST_CASE("KernelBuf heap tier") {
    KernelBuf kb(131072);
    CHECK(kb.size == 131072); CHECK(kb.Data() != nullptr);
}

TEST_CASE("KernelBuf move constructor") {
    KernelBuf k1(128); k1[10] = 'M'; k1.SetFdOffset(4096);
    KernelBuf k2(std::move(k1));
    CHECK(k2[10] == 'M'); CHECK(k2.size == 128); CHECK(k2.FdOffset() == 4096);
    CHECK(k1.size == 0); CHECK(k1.FdOffset() == 0);
}

TEST_CASE("KernelBuf move assignment resets source offset") {
    KernelBuf k1(512); k1[50] = 'Y'; k1.SetFdOffset(2048);
    KernelBuf k2(64); k2 = std::move(k1);
    CHECK(k2[50] == 'Y'); CHECK(k2.size == 512); CHECK(k2.FdOffset() == 2048);
    CHECK(k1.size == 0); CHECK(k1.FdOffset() == 0);  // was the bug
}

TEST_CASE("KernelBuf Resize") {
    KernelBuf kb(128); kb[0] = 'D';
    kb.Resize(1024); CHECK(kb.size == 1024); CHECK(kb[0] == 'D');
}

TEST_CASE("KernelBuf MakeKernelBuffer") {
    auto p = MakeKernelBuffer(256);
    CHECK(p != nullptr); CHECK(p->size == 256); CHECK(p->Data() != nullptr);
}
