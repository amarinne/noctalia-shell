#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>

class VolumeScrollRamp {
public:
  using Clock = std::chrono::steady_clock;

  [[nodiscard]] float advance(
      std::uint32_t sinkId, int direction, float baseStep, float current, float maxVolume,
      Clock::time_point now = Clock::now()
  ) {
    const bool continues = m_sinkId == sinkId && m_direction == direction && now - m_lastAt <= kResetDelay;
    if (!continues) {
      m_eventCount = 0;
      m_target = current;
    }

    const std::size_t stage = std::min(m_eventCount, kStepMultipliers.size() - 1);
    m_target =
        std::clamp(m_target + static_cast<float>(direction) * baseStep * kStepMultipliers[stage], 0.0F, maxVolume);
    m_sinkId = sinkId;
    m_direction = direction;
    m_lastAt = now;
    ++m_eventCount;
    return m_target;
  }

private:
  static constexpr auto kResetDelay = std::chrono::milliseconds(500);
  static constexpr std::array kStepMultipliers{1.0F, 2.0F, 4.0F, 8.0F};

  Clock::time_point m_lastAt{};
  float m_target = 0.0F;
  std::uint32_t m_sinkId = 0;
  std::size_t m_eventCount = 0;
  int m_direction = 0;
};
