#pragma once
// AYFrameInterpolator.h - 帧插值器

#include <AYCore.h>
#include <cstdint>

namespace ayt::game
{

// 插值模式
enum class InterpolationMode : uint8_t {
    None,       // 无插值
    Linear,     // 线性插值
    Cubic,      // 立方插值（平滑）
    SLERP       // 球面线性插值（用于旋转）
};

// 2D 向量插值数据
struct Vec2Interpolator {
    float current[2] = {0.0f, 0.0f};
    float target[2] = {0.0f, 0.0f};
    float velocity[2] = {0.0f, 0.0f};

    void setTarget(float x, float y);
    void setTarget(float x, float y, float dt);
    void lerp(float alpha);
    void extrapolate(float dt);
};

// 3D 向量插值数据
struct Vec3Interpolator {
    float current[3] = {0.0f, 0.0f, 0.0f};
    float target[3] = {0.0f, 0.0f, 0.0f};
    float velocity[3] = {0.0f, 0.0f, 0.0f};

    void setTarget(float x, float y, float z);
    void setTarget(float x, float y, float z, float dt);
    void lerp(float alpha);
    void extrapolate(float dt);
};

// 4D 向量/四元数插值数据
struct Vec4Interpolator {
    float current[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float target[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float velocity[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    void setTarget(float x, float y, float z, float w);
    void setTarget(float x, float y, float z, float w, float dt);
    void lerp(float alpha);
    void extrapolate(float dt);
};

// 旋转插值（使用四元数 slerp）
struct QuaternionInterpolator {
    float current[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // 单位四元数
    float target[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    void setTarget(float x, float y, float z, float w);
    void slerp(float alpha);
};

// 变换插值（位置 + 旋转 + 缩放）
struct TransformInterpolator {
    Vec3Interpolator position;
    QuaternionInterpolator rotation;
    Vec3Interpolator scale;

    void setTargetPosition(float x, float y, float z, float dt = 0.0f);
    void setTargetRotation(float x, float y, float z, float w);
    void setTargetScale(float x, float y, float z, float dt = 0.0f);

    // 更新所有插值
    void interpolate(float alpha);

    // 外推（用于物理模拟）
    void extrapolate(float dt);
};

// 平滑 deltaTime 计算器
class SmoothDeltaTime {
public:
    SmoothDeltaTime();

    // 设置平滑窗口大小（帧数）
    void setWindowSize(size_t size);

    // 添加新的 deltaTime
    void add(float deltaTime);

    // 获取平滑后的 deltaTime
    float getSmoothed() const { return _smoothed; }

    // 获取原始 deltaTime
    float getRaw() const { return _raw; }

    // 获取平滑因子（0-1，越小越平滑）
    float getSmoothingFactor() const { return _smoothingFactor; }
    void setSmoothingFactor(float factor);

private:
    float _raw = 0.0f;
    float _smoothed = 0.0f;
    float _smoothingFactor = 0.1f;  // 默认 10% 新值
    std::vector<float> _history;
    size_t _windowSize = 10;
    size_t _index = 0;
};

} // namespace ayt::game