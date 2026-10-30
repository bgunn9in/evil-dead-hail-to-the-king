#pragma once
#include <algorithm>
#include <cmath>

struct Vec2 { float x = 0, z = 0; };
inline float length(Vec2 v) { return std::hypot(v.x, v.z); }
inline Vec2 radialStick(float x, float y, float deadzone = 0.18f) {
    if (!std::isfinite(x) || !std::isfinite(y)) return {};
    const float magnitude = std::hypot(x, y);
    if (magnitude <= deadzone) return {};
    const float speed = (std::min(magnitude, 1.0f) - deadzone) / (1.0f - deadzone);
    return {x / magnitude * speed, -y / magnitude * speed};
}
inline float frameSeconds(double seconds) {
    return std::isfinite(seconds) ? static_cast<float>(std::clamp(seconds, 0.0, 0.05)) : 0;
}
inline bool pressedEdge(bool down, bool& held) {
    const bool pressed=down&&!held;
    held=down;
    return pressed;
}

struct Movement {
    bool held = false;
    Vec2 reference{}, right{}, forward{};
    Vec2 world(Vec2 stick, Vec2 cameraRight, Vec2 cameraForward) {
        const float magnitude = length(stick);
        if (magnitude <= 0.001f) { held = false; return {}; }
        const Vec2 direction{stick.x / magnitude, stick.z / magnitude};
        // Keep the current world direction across camera cuts until the stick turns.
        if (!held || direction.x * reference.x + direction.z * reference.z < 0.8660254f) {
            reference = direction; right = cameraRight; forward = cameraForward; held = true;
        }
        Vec2 result{right.x * stick.x + forward.x * stick.z,
                    right.z * stick.x + forward.z * stick.z};
        const float worldLength = length(result);
        if (worldLength > 0.001f) {
            result.x *= magnitude / worldLength;
            result.z *= magnitude / worldLength;
        }
        return result;
    }
};
