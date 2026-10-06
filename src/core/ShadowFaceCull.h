#pragma once

inline bool ShadowSphereTouchesFace(float x, float y, float z, float radius,
                                   float dx, float dy, float dz) {
    if (!(radius > 0.0f && radius < 1.0e20f) ||
        !(x > -1.0e20f && x < 1.0e20f) ||
        !(y > -1.0e20f && y < 1.0e20f) ||
        !(z > -1.0e20f && z < 1.0e20f)) return true;
    const float forward = x * dx + y * dy + z * dz;
    const float a = dx != 0.0f ? y : x;
    const float b = dz != 0.0f ? y : z;
    const float margin = radius * 1.414214f + 0.01f;
    return forward + a >= -margin && forward - a >= -margin &&
           forward + b >= -margin && forward - b >= -margin;
}
