#pragma once
// AYGameLoop/FrameInterpolator.h - 帧插值器

#include <AYCore.h>
#include <cstdint>
#include <vector>

namespace ayt::game
{

// Interpolation modes (reserved; not all implementations consume this).
enum class InterpolationMode : uint8_t {
    None,       // 无插值
    Linear,     // 线性插值
    Cubic,      // 立方插值（平滑）
    SLERP       // 球面线性插值（用于旋转）
};

// NOTE on semantics:
//   The interpolators below are *first-order low-pass filters* that
//   advance `current` toward `target` by `alpha`. They are NOT
//   "lerp(prev, curr, alpha)" frame-interpolation functions.
//
//   Usage pattern:
//     Vec3Interpolator pos;
//     pos.setTarget(10, 0, 0);              // initial target
//     // each frame:
//     pos.setTarget(newX, 0, 0, deltaTime); // re-aim; velocity is updated
//     pos.lerp(0.1f);                       // ease current toward target
//
//   For frame-state interpolation between two discrete states, write a
//   separate helper that holds (prev, curr) and computes lerp(slerp).

struct Vec2Interpolator {
    float current[2] = {0.0f, 0.0f};
    float target[2]  = {0.0f, 0.0f};
    float velocity[2] = {0.0f, 0.0f};

    void setTarget(float x, float y);
    void setTarget(float x, float y, float dt);
    void lerp(float alpha);
    void extrapolate(float dt);
};

struct Vec3Interpolator {
    float current[3] = {0.0f, 0.0f, 0.0f};
    float target[3]  = {0.0f, 0.0f, 0.0f};
    float velocity[3] = {0.0f, 0.0f, 0.0f};

    void setTarget(float x, float y, float z);
    void setTarget(float x, float y, float z, float dt);
    void lerp(float alpha);
    void extrapolate(float dt);
};

struct Vec4Interpolator {
    float current[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float target[4]  = {0.0f, 0.0f, 0.0f, 0.0f};
    float velocity[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    void setTarget(float x, float y, float z, float w);
    void setTarget(float x, float y, float z, float w, float dt);
    void lerp(float alpha);
    void extrapolate(float dt);
};

// QuaternionInterpolator: target MUST be a unit quaternion; non-unit
// targets will accumulate magnitude drift in `current`. See header note above.
struct QuaternionInterpolator {
    float current[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // 单位四元数
    float target[4]  = {0.0f, 0.0f, 0.0f, 1.0f};

    void setTarget(float x, float y, float z, float w);
    void slerp(float alpha);
};

struct TransformInterpolator {
    Vec3Interpolator position;
    QuaternionInterpolator rotation;
    Vec3Interpolator scale;

    void setTargetPosition(float x, float y, float z, float dt = 0.0f);
    void setTargetRotation(float x, float y, float z, float w);
    void setTargetScale(float x, float y, float z, float dt = 0.0f);

    void interpolate(float alpha);
    void extrapolate(float dt);
};

// Exponential moving-average deltaTime smoother (low-pass filter).
class SmoothDeltaTime {
public:
    SmoothDeltaTime();

    // size is clamped to >= 1.
    void setWindowSize(size_t size);

    void add(float deltaTime);

    [[nodiscard]] float getSmoothed() const { return _smoothed; }
    [[nodiscard]] float getRaw() const { return _raw; }
    [[nodiscard]] float getSmoothingFactor() const { return _smoothingFactor; }
    void setSmoothingFactor(float factor);

private:
    float _raw = 0.0f;
    float _smoothed = 0.0f;
    float _smoothingFactor = 0.1f;
    std::vector<float> _history;
    size_t _windowSize = 10;
    size_t _index = 0;
};

} // namespace ayt::game