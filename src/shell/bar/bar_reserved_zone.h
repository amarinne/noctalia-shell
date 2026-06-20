#pragma once

#include "shell/surface/shadow.h"

#include <algorithm>
#include <cstdint>

/// Layer-shell exclusive zone the bar reserves on its anchored screen edge.
/// Canonical definition shared by the bar surface and attached panels so a panel
/// can anchor against the same reserved edge the compositor places the bar on.
[[nodiscard]] inline std::int32_t
reservedBarExclusiveZone(const BarConfig& barConfig, const ShellConfig::ShadowConfig& shadowConfig) {
  const std::int32_t mEdge = barConfig.marginEdge;
  // Auto-hide folds the edge margin into the surface as a gutter and sets the
  // layer margin to 0, so the compositor adds no margin to the exclusive zone.
  // The zone must then cover the full edge gap itself (thickness + edge margin);
  // the edge-side shadow is allowed to bleed beyond, matching the layer-margin path.
  if (barConfig.autoHide && mEdge > 0) {
    return barConfig.thickness + mEdge;
  }
  const auto sb = shell::surface_shadow::bleed(barConfig.shadow, shadowConfig);
  if (barConfig.position == "bottom") {
    return barConfig.thickness + std::min(mEdge, sb.down);
  }
  if (barConfig.position == "top") {
    return std::min(mEdge, sb.up) + barConfig.thickness;
  }
  if (barConfig.position == "right") {
    return barConfig.thickness + std::min(mEdge, sb.right);
  }
  if (barConfig.position == "left") {
    return std::min(mEdge, sb.left) + barConfig.thickness;
  }
  return barConfig.thickness;
}
