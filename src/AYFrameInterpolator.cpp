// AYFrameInterpolator.cpp - 帧插值器实现

#include <AYFrameInterpolator.h>
#include <algorithm>
#include <cmath>

namespace ayt::game
{

// ========== Vec2Interpolator ==========
void Vec2Interpolator::setTarget(float x, float y) {
    target[0] = x;
    target[1] = y;
}

void Vec2Interpolator::setTarget(float x, float y, float dt) {
    if (dt > 0.0f) {
        velocity[0] = (x - target[0]) / dt;
        velocity[1] = (y - target[1]) / dt;
    }
    target[0] = x;
    target[1] = y;
}

void Vec2Interpolator::lerp(float alpha) {
    current[0] += (target[0] - current[0]) * alpha;
    current[1] += (target[1] - current[1]) * alpha;
}

void Vec2Interpolator::extrapolate(float dt) {
    current[0] += velocity[0] * dt;
    current[1] += velocity[1] * dt;
}

// ========== Vec3Interpolator ==========
void Vec3Interpolator::setTarget(float x, float y, float z) {
    target[0] = x;
    target[1] = y;
    target[2] = z;
}

void Vec3Interpolator::setTarget(float x, float y, float z, float dt) {
    if (dt > 0.0f) {
        velocity[0] = (x - target[0]) / dt;
        velocity[1] = (y - target[1]) / dt;
        velocity[2] = (z - target[2]) / dt;
    }
    target[0] = x;
    target[1] = y;
    target[2] = z;
}

void Vec3Interpolator::lerp(float alpha) {
    current[0] += (target[0] - current[0]) * alpha;
    current[1] += (target[1] - current[1]) * alpha;
    current[2] += (target[2] - current[2]) * alpha;
}

void Vec3Interpolator::extrapolate(float dt) {
    current[0] += velocity[0] * dt;
    current[1] += velocity[1] * dt;
    current[2] += velocity[2] * dt;
}

// ========== Vec4Interpolator ==========
void Vec4Interpolator::setTarget(float x, float y, float z, float w) {
    target[0] = x;
    target[1] = y;
    target[2] = z;
    target[3] = w;
}

void Vec4Interpolator::setTarget(float x, float y, float z, float w, float dt) {
    if (dt > 0.0f) {
        velocity[0] = (x - target[0]) / dt;
        velocity[1] = (y - target[1]) / dt;
        velocity[2] = (z - target[2]) / dt;
        velocity[3] = (w - target[3]) / dt;
    }
    target[0] = x;
    target[1] = y;
    target[2] = z;
    target[3] = w;
}

void Vec4Interpolator::lerp(float alpha) {
    current[0] += (target[0] - current[0]) * alpha;
    current[1] += (target[1] - current[1]) * alpha;
    current[2] += (target[2] - current[2]) * alpha;
    current[3] += (target[3] - current[3]) * alpha;
}

void Vec4Interpolator::extrapolate(float dt) {
    current[0] += velocity[0] * dt;
    current[1] += velocity[1] * dt;
    current[2] += velocity[2] * dt;
    current[3] += velocity[3] * dt;
}

// ========== QuaternionInterpolator ==========
void QuaternionInterpolator::setTarget(float x, float y, float z, float w) {
    target[0] = x;
    target[1] = y;
    target[2] = z;
    target[3] = w;
}

void QuaternionInterpolator::slerp(float alpha) {
    // 四元数球面线性插值
    float dot = current[0] * target[0] + current[1] * target[1] +
                 current[2] * target[2] + current[3] * target[3];

    // 确保插值走短路径
    float qx = target[0], qy = target[1], qz = target[2], qw = target[3];
    if (dot < 0.0f) {
        qx = -qx;
        qy = -qy;
        qz = -qz;
        qw = -qw;
        dot = -dot;
    }

    // 如果非常接近，可以线性插值
    if (dot > 0.9995f) {
        float omAlpha = 1.0f - alpha;
        current[0] = current[0] * omAlpha + qx * alpha;
        current[1] = current[1] * omAlpha + qy * alpha;
        current[2] = current[2] * omAlpha + qz * alpha;
        current[3] = current[3] * omAlpha + qw * alpha;

        // 归一化
        float len = std::sqrt(current[0] * current[0] + current[1] * current[1] +
                        current[2] * current[2] + current[3] * current[3]);
        if (len > 0.0f) {
            current[0] /= len;
            current[1] /= len;
            current[2] /= len;
            current[3] /= len;
        }
        return;
    }

    // Slerp
    float theta0 = std::acos(std::clamp(dot, -1.0f, 1.0f));
    float theta = theta0 * alpha;
    float sinTheta = std::sin(theta);
    float sinTheta0 = std::sin(theta0);

    float s0 = std::cos(theta) - dot * sinTheta / sinTheta0;
    float s1 = sinTheta / sinTheta0;

    current[0] = current[0] * s0 + qx * s1;
    current[1] = current[1] * s0 + qy * s1;
    current[2] = current[2] * s0 + qz * s1;
    current[3] = current[3] * s0 + qw * s1;
}

// ========== TransformInterpolator ==========
void TransformInterpolator::setTargetPosition(float x, float y, float z, float dt) {
    position.setTarget(x, y, z, dt);
}

void TransformInterpolator::setTargetRotation(float x, float y, float z, float w) {
    rotation.setTarget(x, y, z, w);
}

void TransformInterpolator::setTargetScale(float x, float y, float z, float dt) {
    scale.setTarget(x, y, z, dt);
}

void TransformInterpolator::interpolate(float alpha) {
    position.lerp(alpha);
    rotation.slerp(alpha);
    scale.lerp(alpha);
}

void TransformInterpolator::extrapolate(float dt) {
    position.extrapolate(dt);
    scale.extrapolate(dt);
}

// ========== SmoothDeltaTime ==========
SmoothDeltaTime::SmoothDeltaTime() {
    _history.resize(_windowSize, 0.0f);
}

void SmoothDeltaTime::setWindowSize(size_t size) {
    // Clamp to >= 1 to prevent division by zero in add().
    _windowSize = size > 0 ? size : 1;
    _history.resize(_windowSize, 0.0f);
    _index = 0;
}

void SmoothDeltaTime::add(float deltaTime) {
    _raw = deltaTime;
    _history[_index] = deltaTime;
    _index = (_index + 1) % _windowSize;

    // 计算指数移动平均
    _smoothed = _smoothed * (1.0f - _smoothingFactor) + deltaTime * _smoothingFactor;
}

void SmoothDeltaTime::setSmoothingFactor(float factor) {
    _smoothingFactor = std::clamp(factor, 0.0f, 1.0f);
}

} // namespace ayt::game