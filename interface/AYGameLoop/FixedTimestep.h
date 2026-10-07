#pragma once
#include <AYMath/DetFloat.h>
#include <cstdint>
#include <numeric>
#include <optional>

namespace ayt::game {
/** @brief Immutable fixed-step numeric input; defaults to software binary32 1/60.
 * @note Rational inputs are reduced first, then both integers must be <= 2^24
 * so conversion is exact and one software division rounds the ratio once.
 * Native inputs retain their bits with numerator/denominator zero. Persist
 * the ratio, deltaTime().bits(), scalar profile and input schema explicitly.
 */
class FixedTimestep {
public:
    FixedTimestep() noexcept
        : FixedTimestep(math::DetFloat32::fromUInt(1) / math::DetFloat32::fromUInt(60), 1, 60) {}
    /// Reject zero/unsupported reduced integers without producing a partial value.
    [[nodiscard]] static std::optional<FixedTimestep> fromRatio(
        std::uint32_t numerator, std::uint32_t denominator) noexcept {
        if (numerator == 0 || denominator == 0) return std::nullopt;
        const auto divisor = std::gcd(numerator, denominator);
        numerator /= divisor;
        denominator /= divisor;
        if (numerator > 0x01000000u || denominator > 0x01000000u) return std::nullopt;
        return FixedTimestep(math::DetFloat32::fromUInt(numerator)
            / math::DetFloat32::fromUInt(denominator), numerator, denominator);
    }
    /// Explicit legacy input boundary; finite positive native bits only, no rational claim.
    [[nodiscard]] static std::optional<FixedTimestep> fromNative(float seconds) noexcept {
        const auto value = math::DetFloat32::fromFloat(seconds);
        if (!value.isFinite() || !(value > math::DetFloat32{})) return std::nullopt;
        return FixedTimestep(value, 0, 0);
    }
    [[nodiscard]] math::DetFloat32 deltaTime() const noexcept { return _deltaTime; }
    [[nodiscard]] float nativeDeltaTime() const noexcept { return _deltaTime.toFloat(); }
    [[nodiscard]] std::uint32_t numerator() const noexcept { return _numerator; }
    [[nodiscard]] std::uint32_t denominator() const noexcept { return _denominator; }
    [[nodiscard]] bool isRational() const noexcept { return _denominator != 0; }
private:
    FixedTimestep(math::DetFloat32 value, std::uint32_t numerator, std::uint32_t denominator) noexcept
        : _deltaTime(value), _numerator(numerator), _denominator(denominator) {}
    math::DetFloat32 _deltaTime;
    std::uint32_t _numerator, _denominator;
};
} // namespace ayt::game
