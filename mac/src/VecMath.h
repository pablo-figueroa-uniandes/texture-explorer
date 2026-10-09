#pragma once
// The small part of DirectXMath that Texture Explorer needs, written with Apple's simd
// library. simd::float3 plays the role of XMVECTOR (a 16-byte SIMD register); Float3 and
// Float4 play the role of XMFLOAT3/XMFLOAT4 (tightly packed storage for GPU buffers).
// Matrices are column-major and multiply column vectors (M * v), the opposite of
// DirectXMath's row vectors (v * M); each function below builds the transpose of its
// DirectXMath namesake, so both programs compute the same numbers.
#include <simd/simd.h>
#include <cmath>

struct Float3 {
    float x = 0, y = 0, z = 0;
    Float3() = default;
    Float3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Float3(simd::float3 v) : x(v.x), y(v.y), z(v.z) {}
    operator simd::float3() const { return simd::float3{x, y, z}; }
};

struct Float4 {
    float x = 0, y = 0, z = 0, w = 0;
    Float4() = default;
    Float4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

constexpr float kDegToRad = 3.14159265358979f / 180.0f;

// XMMatrixRotationY: rotation about +y by angle a.
inline simd::float4x4 MatrixRotationY(float a) {
    const float c = std::cos(a), s = std::sin(a);
    return simd::float4x4(simd::float4{c, 0, -s, 0}, simd::float4{0, 1, 0, 0}, simd::float4{s, 0, c, 0},
                          simd::float4{0, 0, 0, 1});
}

// XMMatrixLookAtRH: the camera at eye looks at focus, down its own -z axis.
inline simd::float4x4 MatrixLookAtRH(simd::float3 eye, simd::float3 focus, simd::float3 up) {
    const simd::float3 z = simd::normalize(eye - focus);
    const simd::float3 x = simd::normalize(simd::cross(up, z));
    const simd::float3 y = simd::cross(z, x);
    return simd::float4x4(simd::float4{x.x, y.x, z.x, 0}, simd::float4{x.y, y.y, z.y, 0},
                          simd::float4{x.z, y.z, z.z, 0},
                          simd::float4{-simd::dot(x, eye), -simd::dot(y, eye), -simd::dot(z, eye), 1});
}

// XMMatrixPerspectiveFovRH: depth 0 at the near plane and 1 at the far plane, which is the
// clip-space convention of Metal as well as of Direct3D.
inline simd::float4x4 MatrixPerspectiveFovRH(float fovY, float aspect, float zn, float zf) {
    const float h = 1.0f / std::tan(0.5f * fovY), w = h / aspect, r = zf / (zn - zf);
    return simd::float4x4(simd::float4{w, 0, 0, 0}, simd::float4{0, h, 0, 0}, simd::float4{0, 0, r, -1},
                          simd::float4{0, 0, r * zn, 0});
}

// XMVector3TransformCoord and XMVector3TransformNormal.
inline simd::float3 TransformPoint(const simd::float4x4& m, simd::float3 p) {
    const simd::float4 r = m * simd::float4{p.x, p.y, p.z, 1};
    return r.xyz / r.w;
}
inline simd::float3 TransformNormal(const simd::float4x4& m, simd::float3 n) {
    return (m * simd::float4{n.x, n.y, n.z, 0}).xyz;
}
