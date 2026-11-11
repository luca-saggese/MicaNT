#if defined(MICANT_KERNEL_CXX_LIFECYCLE_SELFTEST)

#include "micant/km/executive/freestanding.hpp"

extern "C" void* malloc(size_t);
extern "C" void free(void*);

namespace {

constinit unsigned g_constructed{};
constinit unsigned g_destroyed{};
constinit unsigned g_order{};
constinit bool g_bad{};
constinit unsigned g_cInitialized{};

int initializeCFixture() {
    if (++g_cInitialized != 1 || g_constructed != 0) {
        micant::ex::stopBootstrapRuntime("ExCxxTest: C initializer order failed\r\n");
    }
    return 0;
}

__attribute__((section(".CRT$XIU"), used))
int (*const g_cInitializer)() = initializeCFixture;

struct GlobalAllocation {
    unsigned id;
    unsigned char* array;
    unsigned char* block;

    explicit GlobalAllocation(unsigned value) noexcept
        : id(value), array(new unsigned char[37]),
          block(static_cast<unsigned char*>(malloc(53))) {
        if (!array || !block || g_cInitialized != 1 || ++g_constructed != id) {
            micant::ex::stopBootstrapRuntime("ExCxxTest: constructor allocation/order failed\r\n");
        }
        for (size_t index = 0; index < 37; ++index) {
            array[index] = static_cast<unsigned char>(index ^ id);
        }
        for (size_t index = 0; index < 53; ++index) {
            block[index] = static_cast<unsigned char>(index + id);
        }
    }

    [[nodiscard]] bool valid() const noexcept {
        if (!array || !block) return false;
        for (size_t index = 0; index < 37; ++index) {
            if (array[index] != static_cast<unsigned char>(index ^ id)) return false;
        }
        for (size_t index = 0; index < 53; ++index) {
            if (block[index] != static_cast<unsigned char>(index + id)) return false;
        }
        return true;
    }

    ~GlobalAllocation() noexcept {
        if (!valid() || id != 2 - g_destroyed) g_bad = true;
        g_order = g_order * 10 + id;
        ++g_destroyed;
        delete[] array;
        free(block);
        array = nullptr;
        block = nullptr;
    }
};

GlobalAllocation g_first{1};
GlobalAllocation g_second{2};

} // namespace

namespace micant::ex::test {

bool verifyCxxBeforeInitialization() noexcept {
    return g_cInitialized == 0 && g_constructed == 0 &&
        g_destroyed == 0 && g_order == 0 && !g_bad;
}

bool verifyCxxAfterInitialization() noexcept {
    return g_cInitialized == 1 && g_constructed == 2 &&
        g_destroyed == 0 && g_order == 0 &&
        !g_bad && g_first.valid() && g_second.valid();
}

bool verifyCxxAfterDestruction() noexcept {
    // Do not access the allocation objects after their lifetimes have ended.
    return g_cInitialized == 1 && g_constructed == 2 &&
        g_destroyed == 2 && g_order == 21 && !g_bad;
}

} // namespace micant::ex::test

#endif
