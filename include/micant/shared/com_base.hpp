#pragma once

#include <cstdint>
#include "micant/shared/ntdef.hpp"

// Mode-neutral COM base ABI used by Ring 3 interfaces.
namespace micant::ole32 {

using HRESULT = int32_t;
using IID = micant::GUID;
using REFIID = const IID&;

inline constexpr HRESULT S_OK = 0;
inline constexpr HRESULT S_FALSE = 1;
inline constexpr HRESULT E_NOTIMPL = static_cast<HRESULT>(0x80004001);
inline constexpr HRESULT E_NOINTERFACE = static_cast<HRESULT>(0x80004002);
inline constexpr HRESULT E_POINTER = static_cast<HRESULT>(0x80004003);
inline constexpr HRESULT E_ABORT = static_cast<HRESULT>(0x80004004);
inline constexpr HRESULT E_FAIL = static_cast<HRESULT>(0x80004005);
inline constexpr HRESULT E_UNEXPECTED = static_cast<HRESULT>(0x8000FFFF);
inline constexpr HRESULT E_OUTOFMEMORY = static_cast<HRESULT>(0x8007000E);
inline constexpr HRESULT E_INVALIDARG = static_cast<HRESULT>(0x80070057);
inline constexpr HRESULT E_NOT_SUFFICIENT_BUFFER = static_cast<HRESULT>(0x8007007A);

inline constexpr IID IID_IUnknown = {
    0x00000000, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 }
};

class IUnknown {
public:
    virtual HRESULT QueryInterface(REFIID riid, void** ppvObject) = 0;
    virtual uint32_t AddRef() = 0;
    virtual uint32_t Release() = 0;
    virtual ~IUnknown() = default;
};

} // namespace micant::ole32
