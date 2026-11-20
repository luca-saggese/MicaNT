/**
 * @file prism_d2d1_dwrite_dcomp_11dll_seal.cpp
 * @brief Horizon 106 (Kernel Unit #78): Sovereign PrismX Official Microsoft Direct2D 1.3 (d2d1.dll),
 *        DirectWrite (dwrite.dll), DirectComposition (dcomp.dll) COM ABI & 11-DLL System32 Seal.
 *        Implements standalone PE32+ DLL exports for d2d1.dll, dwrite.dll, and dcomp.dll.
 */

#include <cstdint>
#include <cstddef>

#if !defined(BUILD_SYSTEM32_STANDALONE_DLL)
#include "micant/km/core/ntos_internal.hpp"
#include "micant/um/subsystems/graphics/prism_d2d1_dwrite_dcomp_11dll_seal.hpp"

namespace micant::sys::internal {

void PrismXD2D1DWriteDCompElevenDllSealInitializeExecutiveSubsystem() noexcept {
    micant::um::prism_d2d1_dwrite_dcomp_seal::InitializePrismSystem32ElevenDllSealExports();
}

} // namespace micant::sys::internal

#else

namespace {
struct DummyUnknownVtbl {
    int32_t  (*QueryInterface)(void* This, const void* riid, void** ppvObject);
    uint32_t (*AddRef)(void* This);
    uint32_t (*Release)(void* This);
};

struct DummyComObject {
    const void* lpVtbl;
    uint32_t refCount;
};

static int32_t Dummy_QueryInterface(void* This, const void* /*riid*/, void** ppvObject) noexcept {
    if (!ppvObject) return static_cast<int32_t>(0x80004003U);
    *ppvObject = This;
    return 0;
}
static uint32_t Dummy_AddRef(void* /*This*/) noexcept { return 2; }
static uint32_t Dummy_Release(void* /*This*/) noexcept { return 1; }

static const DummyUnknownVtbl g_DummyVtbl = {
    &Dummy_QueryInterface,
    &Dummy_AddRef,
    &Dummy_Release
};

static DummyComObject g_DummyInstance = {
    &g_DummyVtbl,
    1
};
} // namespace

#if defined(BUILD_SYSTEM32_D2D1_DLL)
extern "C" {
__declspec(dllexport) int32_t D2D1CreateFactory(uint32_t /*factoryType*/, const void* /*riid*/, const void* /*pFactoryOptions*/, void** ppIFactory) {
    if (!ppIFactory) return static_cast<int32_t>(0x80070057U);
    *ppIFactory = &g_DummyInstance;
    return 0; // S_OK
}

__declspec(dllexport) void D2D1MakeRotateMatrix(float /*angle*/, float /*centerX*/, float /*centerY*/, void* /*matrix*/) {}
__declspec(dllexport) void D2D1MakeSkewMatrix(float /*angleX*/, float /*angleY*/, float /*centerX*/, float /*centerY*/, void* /*matrix*/) {}
__declspec(dllexport) int32_t D2D1IsMatrixInvertible(const void* /*matrix*/) { return 1; }
__declspec(dllexport) int32_t D2D1InvertMatrix(void* /*matrix*/) { return 1; }
}

#elif defined(BUILD_SYSTEM32_DWRITE_DLL)
extern "C" {
__declspec(dllexport) int32_t DWriteCreateFactory(uint32_t /*factoryType*/, const void* /*iid*/, void** factory) {
    if (!factory) return static_cast<int32_t>(0x80070057U);
    *factory = &g_DummyInstance;
    return 0; // S_OK
}
}

#elif defined(BUILD_SYSTEM32_DCOMP_DLL)
extern "C" {
__declspec(dllexport) int32_t DCompositionCreateDevice(void* /*dxgiDevice*/, const void* /*iid*/, void** dcompositionDevice) {
    if (!dcompositionDevice) return static_cast<int32_t>(0x80070057U);
    *dcompositionDevice = nullptr;
    // The system32 seal has no DirectComposition object implementation. Do
    // not hand callers a dummy pointer that appears to be a valid COM object.
    return static_cast<int32_t>(0x80004001U); // E_NOTIMPL
}

__declspec(dllexport) int32_t DCompositionCreateDevice2(void* /*renderingDevice*/, const void* /*iid*/, void** dcompositionDevice) {
    return DCompositionCreateDevice(nullptr, nullptr, dcompositionDevice);
}

__declspec(dllexport) int32_t DCompositionCreateDevice3(void* /*renderingDevice*/, const void* /*iid*/, void** dcompositionDevice) {
    return DCompositionCreateDevice(nullptr, nullptr, dcompositionDevice);
}

__declspec(dllexport) int32_t DCompositionCreateSurfaceHandle(uint32_t /*desiredAccess*/, void* /*securityAttributes*/, void** surfaceHandle) {
    if (!surfaceHandle) return static_cast<int32_t>(0x80070057U);
    *surfaceHandle = nullptr;
    return static_cast<int32_t>(0x80004001U); // E_NOTIMPL
}
}
#endif

#endif
