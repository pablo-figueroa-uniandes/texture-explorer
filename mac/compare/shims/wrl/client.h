// A minimal Microsoft::WRL::ComPtr, enough for src/Material.h and src/Material.cpp to
// compile on macOS. The probe never creates real COM objects, so it holds only a pointer.
#pragma once

namespace Microsoft::WRL {
template <class T>
class ComPtr {
public:
    T* Get() const { return p_; }
    T** GetAddressOf() { return &p_; }
    T** operator&() { return &p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    void Reset() { p_ = nullptr; }

private:
    T* p_ = nullptr;
};
} // namespace Microsoft::WRL
