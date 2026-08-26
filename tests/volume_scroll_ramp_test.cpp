#include "pipewire/volume_scroll_ramp.h"
#include "tests/test_check.h"

#include <chrono>
#include <cmath>
#include <cstdlib>

int main() {
  using namespace std::chrono_literals;
  using Clock = VolumeScrollRamp::Clock;

  VolumeScrollRamp ramp;
  const auto start = Clock::time_point{} + 1s;
  const auto closeTo = [](float actual, float expected) { return std::abs(actual - expected) < 0.0001F; };

  TEST_CHECK(closeTo(ramp.advance(10, 1, 0.01F, 0.50F, 1.0F, start), 0.51F));
  TEST_CHECK(closeTo(ramp.advance(10, 1, 0.01F, 0.50F, 1.0F, start + 20ms), 0.53F));
  TEST_CHECK(closeTo(ramp.advance(10, 1, 0.01F, 0.50F, 1.0F, start + 40ms), 0.57F));
  TEST_CHECK(closeTo(ramp.advance(10, 1, 0.01F, 0.50F, 1.0F, start + 60ms), 0.65F));
  TEST_CHECK(closeTo(ramp.advance(10, 1, 0.01F, 0.50F, 1.0F, start + 80ms), 0.73F));

  TEST_CHECK(closeTo(ramp.advance(10, -1, 0.01F, 0.73F, 1.0F, start + 100ms), 0.72F));
  TEST_CHECK(closeTo(ramp.advance(10, -1, 0.01F, 0.72F, 1.0F, start + 601ms), 0.71F));
  TEST_CHECK(closeTo(ramp.advance(11, -1, 0.01F, 0.40F, 1.0F, start + 620ms), 0.39F));

  TEST_CHECK(closeTo(ramp.advance(11, 1, 0.01F, 0.995F, 1.0F, start + 640ms), 1.0F));
  TEST_CHECK(closeTo(ramp.advance(11, -1, 0.01F, 0.005F, 1.0F, start + 660ms), 0.0F));

  return EXIT_SUCCESS;
}
