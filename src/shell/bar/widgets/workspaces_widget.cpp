#include "shell/bar/widgets/workspaces_widget.h"

#include "config/config_service.h"
#include "core/ui_phase.h"
#include "render/animation/animation.h"
#include "render/animation/animation_manager.h"
#include "render/core/renderer.h"
#include "render/scene/input_area.h"
#include "render/scene/node.h"
#include "system/app_identity.h"
#include "system/desktop_entry.h"
#include "system/internal_app_metadata.h"
#include "ui/app_icon_colorization.h"
#include "ui/builders.h"
#include "ui/controls/image.h"
#include "ui/style.h"
#include "util/string_utils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <optional>
#include <utility>
#include <wayland-client-protocol.h>

namespace {
  [[nodiscard]] bool isEmptyWorkspace(const Workspace& workspace) {
    return !workspace.occupied && !workspace.active && !workspace.urgent;
  }

  constexpr float kWorkspaceGap = Style::spaceXs;
  constexpr float kWorkspacePillDefaultHeight = Style::baseGlyphSize;
  constexpr float kWorkspaceAnimDurationMs = static_cast<float>(Style::animNormal);

  [[nodiscard]] FontWeight workspaceFontWeight(FontWeight baseWeight, bool minimal, bool active) {
    if (minimal && active) {
      return static_cast<FontWeight>(static_cast<int>(baseWeight) + 200);
    }
    return baseWeight;
  }

  // Numeric workspace IDs ("10", "11") must not be truncated like word labels.
  [[nodiscard]] bool isNumericLabel(std::string_view label) {
    return !label.empty()
        && std::ranges::all_of(label, [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
  }

  [[nodiscard]] std::string workspaceIdentityKey(const Workspace& workspace) {
    if (!workspace.id.empty()) {
      return "id:" + workspace.id;
    }
    if (!workspace.name.empty()) {
      return "name:" + workspace.name;
    }
    if (!workspace.coordinates.empty()) {
      std::string key = "coords:";
      for (const auto coord : workspace.coordinates) {
        key += "/" + std::to_string(coord);
      }
      return key;
    }
    if (workspace.index > 0) {
      return "index:" + std::to_string(workspace.index);
    }
    return {};
  }
} // namespace

WorkspacesWidget::WorkspacesWidget(
    CompositorPlatform& platform, ConfigService& configService, wl_output* output, Options options
)
    : m_platform(platform), m_configService(configService), m_output(output), m_displayMode(options.displayMode),
      m_maxLabelChars(options.maxLabelChars), m_labelsOnlyWhenOccupied(options.labelsOnlyWhenOccupied),
      m_hideWhenEmpty(options.hideWhenEmpty), m_pillScale(options.pillScale),
      m_activePillSize(std::clamp(options.activePillSize, 0.25f, 8.0f)),
      m_inactivePillSize(std::clamp(options.inactivePillSize, 0.25f, 8.0f)), m_minimal(options.minimal),
      m_focusedOutputOnly(options.focusedOutputOnly), m_enableScroll(options.enableScroll),
      m_focusedColor(options.focusedColor),
      m_occupiedColor(options.occupiedColor), m_emptyColor(options.emptyColor), m_urgentColor(options.urgentColor) {
  buildDesktopIconIndex();
}

WorkspacesWidget::DisplayMode WorkspacesWidget::effectiveDisplayMode() const noexcept {
  if (m_minimal && m_displayMode == DisplayMode::None) {
    return DisplayMode::Id;
  }
  return m_displayMode;
}

bool WorkspacesWidget::shouldShowWorkspaceLabel(const Workspace& workspace, std::string_view label) const noexcept {
  if (effectiveDisplayMode() == DisplayMode::None || label.empty()) {
    return false;
  }
  if (m_labelsOnlyWhenOccupied && !workspace.occupied && !workspace.active) {
    return false;
  }
  return true;
}

bool WorkspacesWidget::isWorkspaceHidden(const Workspace& workspace) const noexcept {
  return m_hideWhenEmpty && isEmptyWorkspace(workspace);
}

void WorkspacesWidget::create() {
  auto container = std::make_unique<InputArea>();
  container->setOnAxis([this](const InputArea::PointerData& data) {
    if (!m_enableScroll) {
      return;
    }
    if (data.axis != WL_POINTER_AXIS_VERTICAL_SCROLL) {
      return;
    }
    const float steps = data.scrollSteps();
    if (steps == 0.0f) {
      return;
    }
    // Wayland reports positive wheel deltas for "scroll down", so treat that
    // as moving to the next workspace and negative as previous.
    activateAdjacentWorkspace(steps > 0.0f ? 1 : -1);
  });
  m_container = container.get();
  setRoot(std::move(container));
}

void WorkspacesWidget::doLayout(Renderer& renderer, float containerWidth, float containerHeight) {
  const bool wasVertical = m_isVertical;
  m_isVertical = containerHeight > containerWidth;
  if (wasVertical != m_isVertical) {
    m_rebuildPending = true;
    m_rebuildSnapshot.clear();
  }
  const std::uint64_t textMetricsGeneration = renderer.textMetricsGeneration();
  if (m_textMetricsGeneration != textMetricsGeneration) {
    m_textMetricsGeneration = textMetricsGeneration;
    m_rebuildPending = true;
  }
  if (m_rebuildPending) {
    rebuild(renderer);
    m_rebuildPending = false;
  }
}

void WorkspacesWidget::syncWidgetVisibility(bool showWidget) {
  if (Node* rootNode = root(); rootNode != nullptr) {
    rootNode->setVisible(showWidget);
    rootNode->setParticipatesInLayout(showWidget);
  }
}

void WorkspacesWidget::setWorkspaceClickHandler(InputArea& area, const Workspace& workspace) {
  area.setOnClick([this, workspace](const InputArea::PointerData& data) {
    if (data.button == BTN_LEFT) {
      m_platform.activateWorkspace(m_output, workspace);
    }
  });
}

void WorkspacesWidget::applyItemVisualStyle(Item& item) {
  if (item.indicator != nullptr) {
    item.indicator->setFill(workspaceFillColor(item.visualWorkspace));
    item.indicator->clearBorder();
  }
  if (item.text != nullptr && item.showLabel) {
    item.text->setColor(workspaceTextColor(item.visualWorkspace));
  }
}

bool WorkspacesWidget::shouldHoldPreviousVisualWorkspace(
    const Workspace& previousVisualWorkspace, const Workspace& currentWorkspace
) const noexcept {
  return previousVisualWorkspace.active && isWorkspaceHidden(currentWorkspace);
}

bool WorkspacesWidget::releaseHeldVisualStyles() {
  bool changed = false;
  for (auto& item : m_items) {
    if (!item.releaseVisualAfterAnimation || item.exiting) {
      continue;
    }
    item.visualWorkspace = item.workspace;
    item.releaseVisualAfterAnimation = false;
    applyItemVisualStyle(item);
    changed = true;
  }
  return changed;
}

void WorkspacesWidget::doUpdate(Renderer& renderer) {
  auto current = m_platform.workspaces(m_output);
  auto currentAppsByWorkspace = m_platform.appIdsByWorkspace(m_output);

  if (!m_cachedState.empty() && !current.empty() && !std::ranges::any_of(current, [](const Workspace& ws) {
        return ws.active;
      })) {
    return;
  }

  const bool showWidget = !current.empty()
      && (!m_hideWhenEmpty || std::ranges::any_of(current, [](const Workspace& ws) { return !isEmptyWorkspace(ws); }));
  syncWidgetVisibility(showWidget);
  if (!showWidget) {
    m_rebuildSnapshot.clear();
    if (m_animations != nullptr) {
      m_animations->cancelForOwner(&m_hoverProgress);
    }
    m_hoveredArea = nullptr;
    m_hoverOverlay = nullptr;
    m_hoverProgress = 0.0f;
    if (!m_cachedState.empty() || !m_items.empty()) {
      cancelAnimation();
      m_cachedState.clear();
      m_cachedAppsByWorkspace.clear();
      m_items.clear();
      if (m_container != nullptr) {
        m_container->setFrameSize(0.0f, 0.0f);
        while (!m_container->children().empty()) {
          m_container->removeChild(m_container->children().back().get());
        }
      }
      m_rebuildPending = true;
      if (root() != nullptr) {
        root()->markLayoutDirty();
      }
    }
    return;
  }

  if (m_cachedState.empty() && current.empty()) {
    return;
  }

  bool structuralChange = current.size() != m_cachedState.size();
  bool activeChange = false;
  const bool appsChange = currentAppsByWorkspace != m_cachedAppsByWorkspace;
  bool hideWhenEmptyTransition = false;
  if (!structuralChange) {
    for (std::size_t i = 0; i < current.size(); ++i) {
      const auto& a = current[i];
      const auto& b = m_cachedState[i];
      if (a.id != b.id || a.name != b.name || a.index != b.index || a.coordinates != b.coordinates) {
        structuralChange = true;
        break;
      }
      if (a.active != b.active || a.urgent != b.urgent) {
        activeChange = true;
      }
      if (a.occupied != b.occupied) {
        activeChange = true;
      }
      if (m_hideWhenEmpty && isEmptyWorkspace(a) != isEmptyWorkspace(b)) {
        hideWhenEmptyTransition = true;
      }
    }
  }
  if (!structuralChange && m_rebuildPending && !m_items.empty()) {
    structuralChange = !std::ranges::equal(m_items, m_cachedState, {}, &Item::key, workspaceIdentityKey);
  }

  if (!structuralChange && !activeChange && !hideWhenEmptyTransition && !appsChange) {
    if (m_focusedOutputOnly) {
      const bool isFocused = isFocusedOutput();
      if (isFocused != m_wasFocusedOutput) {
        m_wasFocusedOutput = isFocused;
        retarget(renderer);
      }
    }
    return;
  }

  if ((structuralChange || appsChange) && m_rebuildSnapshot.empty()) {
    snapshotItemsForRebuild();
  }

  m_cachedState.clear();
  m_cachedState.reserve(current.size());
  for (const auto& ws : current) {
    m_cachedState.push_back(
        Workspace{
            .id = ws.id,
            .name = ws.name,
            .coordinates = ws.coordinates,
            .index = ws.index,
            .active = ws.active,
            .urgent = ws.urgent,
            .occupied = ws.occupied
        }
    );
  }
  m_cachedAppsByWorkspace = std::move(currentAppsByWorkspace);

  if (structuralChange || hideWhenEmptyTransition || appsChange) {
    m_rebuildPending = true;
    if (root() != nullptr) {
      root()->markLayoutDirty();
    }
  } else {
    retarget(renderer);
  }
}

void WorkspacesWidget::rebuild(Renderer& renderer) {
  uiAssertNotRendering("WorkspacesWidget::rebuild");
  const bool animateFromSnapshot = !m_minimal && !m_rebuildSnapshot.empty();
  m_activeUsesFocusedColor = !m_focusedOutputOnly || isFocusedOutput();
  cancelAnimation();
  if (m_animations != nullptr) {
    m_animations->cancelForOwner(&m_hoverProgress);
  }
  m_hoveredArea = nullptr;
  m_hoverOverlay = nullptr;
  m_hoverProgress = 0.0f;
  while (!m_container->children().empty()) {
    m_container->removeChild(m_container->children().back().get());
  }
  m_items.clear();
  buildDesktopIconIndex();

  struct RebuildEntry {
    Workspace workspace;
    std::string key;
    std::string label;
    std::vector<std::string> iconPaths;
    bool showLabel = false;
    bool showIcons = false;
    bool exiting = false;
    const ItemSnapshot* snapshot = nullptr;
  };

  const auto& workspaces = m_cachedState;
  auto currentHasKey = [&](const std::string& key) {
    return std::ranges::any_of(workspaces, [&](const Workspace& ws) { return workspaceIdentityKey(ws) == key; });
  };
  auto snapshotIndexForKey = [&](const std::string& key) -> std::optional<std::size_t> {
    for (std::size_t i = 0; i < m_rebuildSnapshot.size(); ++i) {
      if (m_rebuildSnapshot[i].key == key) {
        return i;
      }
    }
    return std::nullopt;
  };

  std::vector<RebuildEntry> entries;
  entries.reserve(workspaces.size() + m_rebuildSnapshot.size());

  if (animateFromSnapshot) {
    std::size_t snapshotCursor = 0;
    auto appendOldOnlyBefore = [&](std::size_t limit) {
      while (snapshotCursor < limit && snapshotCursor < m_rebuildSnapshot.size()) {
        const auto& snapshot = m_rebuildSnapshot[snapshotCursor++];
        if (snapshot.key.empty() || currentHasKey(snapshot.key)) {
          continue;
        }
        entries.push_back(
            RebuildEntry{
                .workspace = snapshot.workspace,
                .key = snapshot.key,
                .label = snapshot.label,
                .iconPaths = snapshot.iconPaths,
                .showLabel = snapshot.showLabel,
                .showIcons = snapshot.showIcons,
                .exiting = true,
                .snapshot = &snapshot,
            }
        );
      }
    };

    for (std::size_t i = 0; i < workspaces.size(); ++i) {
      const auto& workspace = workspaces[i];
      const std::string key = workspaceIdentityKey(workspace);
      const auto snapshotIndex = snapshotIndexForKey(key);
      if (snapshotIndex.has_value()) {
        appendOldOnlyBefore(*snapshotIndex);
      }

      const std::string label = workspaceLabel(workspace, i);
      const auto iconPaths = workspaceAppIcons(workspace, i);
      entries.push_back(
          RebuildEntry{
              .workspace = workspace,
              .key = key,
              .label = label,
              .iconPaths = iconPaths,
              .showLabel = iconPaths.empty() && shouldShowWorkspaceLabel(workspace, label),
              .showIcons = !iconPaths.empty(),
              .exiting = false,
              .snapshot = snapshotIndex.has_value() ? &m_rebuildSnapshot[*snapshotIndex] : nullptr,
          }
      );

      if (snapshotIndex.has_value() && snapshotCursor == *snapshotIndex) {
        ++snapshotCursor;
      }
    }
    appendOldOnlyBefore(m_rebuildSnapshot.size());
  } else {
    for (std::size_t i = 0; i < workspaces.size(); ++i) {
      const auto& workspace = workspaces[i];
      const std::string label = workspaceLabel(workspace, i);
      const auto iconPaths = workspaceAppIcons(workspace, i);
      entries.push_back(
          RebuildEntry{
              .workspace = workspace,
              .key = workspaceIdentityKey(workspace),
              .label = label,
              .iconPaths = iconPaths,
              .showLabel = iconPaths.empty() && shouldShowWorkspaceLabel(workspace, label),
              .showIcons = !iconPaths.empty(),
          }
      );
    }
  }

  const float gap = kWorkspaceGap * m_contentScale;
  const float labelFontSize = Style::fontSizeMini * m_contentScale;
  const float pillHeight = std::round(kWorkspacePillDefaultHeight * m_contentScale * m_pillScale);
  const FontWeight configuredFontWeight = labelFontWeight();

  // Measure text and compute per-slot widths along the bar main axis.
  // Width = max(baseSize * pill_size, textWidth + padding); pill_size comes from active/inactive settings.
  struct SlotMetrics {
    float textWidth = 0.0f;
    float iconsWidth = 0.0f;
    float inactiveWidth = 0.0f;
    float activeWidth = 0.0f;
  };
  std::vector<SlotMetrics> slots(entries.size());
  const float baseSize = std::round(pillHeight);
  const float padding = m_minimal ? (Style::spaceXs * m_contentScale) : (baseSize * 0.6f);
  const float iconSize = std::max(10.0f, std::round(baseSize * 0.70f));
  const float iconGap = std::max(1.0f, std::round(Style::spaceXs * m_contentScale * 0.5f));

  for (std::size_t i = 0; i < entries.size(); ++i) {
    auto& slot = slots[i];
    const auto& entry = entries[i];

    if (entry.showLabel) {
      const FontWeight slotFontWeight = workspaceFontWeight(configuredFontWeight, m_minimal, entry.workspace.active);
      const TextMetrics tm = renderer.measureText(entry.label, labelFontSize, slotFontWeight);
      slot.textWidth = std::max(tm.right - tm.left, tm.inkRight - tm.inkLeft);
    }
    if (entry.showIcons) {
      slot.iconsWidth = static_cast<float>(entry.iconPaths.size()) * iconSize
          + static_cast<float>(entry.iconPaths.size() - 1) * iconGap;
    }
  }

  float maxLabelHeight = labelFontSize;

  for (std::size_t i = 0; i < entries.size(); ++i) {
    auto& slot = slots[i];
    auto& entry = entries[i];
    if (!entry.exiting && isWorkspaceHidden(entry.workspace)) {
      entry.showLabel = false;
      slot.inactiveWidth = 0.0f;
      slot.activeWidth = 0.0f;
      continue;
    }

    if (m_minimal) {
      const float minWidth = baseSize;
      if (!entry.showLabel) {
        const float iconBasedWidth = entry.showIcons ? slot.iconsWidth + padding * 2.0f : 0.0f;
        slot.inactiveWidth = std::max(minWidth, iconBasedWidth);
        slot.activeWidth = slot.inactiveWidth;
      } else {
        const float textBasedWidth = slot.textWidth + padding * 2.0f;
        slot.inactiveWidth = std::max(minWidth, textBasedWidth);
        slot.activeWidth = slot.inactiveWidth;
      }
      if (entry.showLabel) {
        const FontWeight slotFontWeight = workspaceFontWeight(configuredFontWeight, m_minimal, entry.workspace.active);
        const TextMetrics tm = renderer.measureText(entry.label, labelFontSize, slotFontWeight);
        maxLabelHeight = std::max(maxLabelHeight, tm.bottom - tm.top);
      }
      continue;
    }

    const float minWidth = workspaceMainAxisMinWidth(baseSize, false);
    const float minActiveWidth = workspaceMainAxisMinWidth(baseSize, true);

    if (!entry.showLabel) {
      const float iconBasedWidth = entry.showIcons ? slot.iconsWidth + padding : 0.0f;
      slot.inactiveWidth = std::max(minWidth, iconBasedWidth);
      slot.activeWidth = std::max(minActiveWidth, iconBasedWidth);
    } else {
      const float textBasedWidth = slot.textWidth + padding;
      slot.inactiveWidth = std::max(minWidth, textBasedWidth);
      slot.activeWidth = std::max(minActiveWidth, textBasedWidth);
    }
  }

  m_gap = gap;
  m_indicatorHeight = m_minimal ? std::round(maxLabelHeight + padding) : pillHeight;

  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    const auto& ws = entry.workspace;
    const auto& slot = slots[i];

    auto area = std::make_unique<InputArea>();
    area->setClipChildren(true);
    const float w = entry.exiting && entry.snapshot != nullptr ? entry.snapshot->width
        : ws.active                                            ? slot.activeWidth
                                                               : slot.inactiveWidth;
    area->setFrameSize(w, m_indicatorHeight);

    const bool hasSnapshot = entry.snapshot != nullptr;
    Item item{};
    item.workspace = ws;
    item.visualWorkspace = ws;
    item.key = entry.key;
    item.active = ws.active;
    item.exiting = entry.exiting;
    item.label = entry.label;
    item.iconPaths = entry.iconPaths;
    item.showLabel = entry.showLabel;
    item.showIcons = entry.showIcons;
    item.inactiveWidth = slot.inactiveWidth;
    item.activeWidth = slot.activeWidth;
    if (hasSnapshot) {
      item.fromWidth = entry.snapshot->width;
      item.fromOpacity = entry.snapshot->opacity;
    } else {
      item.fromOpacity = 0.0f;
    }

    if (!m_minimal) {
      const float indicatorW = m_isVertical ? m_indicatorHeight : w;
      const float indicatorH = m_isVertical ? w : m_indicatorHeight;
      item.indicator = static_cast<Box*>(area->addChild(
          ui::box({
              .fill = workspaceFillColor(ws),
              .radius = workspacePillRadius(indicatorW, indicatorH),
              .width = w,
              .height = m_indicatorHeight,
              .configure = [](Box& box) { box.clearBorder(); },
          })
      ));
    }

    if (entry.showLabel) {
      item.text = static_cast<Label*>(area->addChild(
          ui::label({
              .text = entry.label,
              .fontSize = labelFontSize,
              .fontWeight = workspaceFontWeight(configuredFontWeight, m_minimal, ws.active),
              .fontFamily = labelFontFamily(),
              .color = workspaceTextColor(ws),
              .baselineMode = LabelBaselineMode::Text,
          })
      ));
      item.text->measure(renderer);
    }

    if (entry.showIcons) {
      const int iconRequestSize = std::max(32, static_cast<int>(std::round(iconSize * 2.0f)));
      const auto appIconTint = effectiveShellAppIconColorizationTint(m_configService.config().shell);
      for (const auto& iconPath : entry.iconPaths) {
        auto image = ui::image({
            .fit = ImageFit::Contain,
            .width = iconSize,
            .height = iconSize,
        });
        image->setAppIconColorization(appIconTint);
        if (image->setSourceFile(renderer, iconPath, iconRequestSize, true)) {
          item.icons.push_back(image.get());
          area->addChild(std::move(image));
        }
      }
      item.showIcons = !item.icons.empty();
    }

    InputArea* areaPtr = area.get();
    if (!entry.exiting) {
      setWorkspaceClickHandler(*area, ws);

      area->setOnEnter([this, areaPtr](const InputArea::PointerData&) {
        const auto itemIt = std::ranges::find(m_items, areaPtr, &Item::area);
        if (itemIt == m_items.end() || itemIt->exiting) {
          return;
        }

        m_hoveredArea = areaPtr;
        updateHoverOverlay();

        const ColorSpec fill = widgetForegroundOr(colorSpecFromRole(ColorRole::OnSurface));
        auto applyHoverProgress = [this, fill](float p) {
          m_hoverProgress = p;
          if (m_hoverOverlay != nullptr) {
            m_hoverOverlay->setVisible(p > 0.001f);
            ColorSpec color = fill;
            color.alpha = 0.1f * p;
            m_hoverOverlay->setFill(color);
          }
          updateHoverOverlay();
          requestRedraw();
        };

        if (m_animations == nullptr) {
          applyHoverProgress(1.0f);
          return;
        }

        m_animations->cancelForOwner(&m_hoverProgress);
        m_animations->animate(
            m_hoverProgress, 1.0f, Style::animFast, Easing::EaseOutCubic, applyHoverProgress, {}, &m_hoverProgress
        );
        requestFrameTick();
      });

      area->setOnLeave([this, areaPtr]() {
        if (m_hoveredArea != areaPtr) {
          return;
        }

        m_hoveredArea = nullptr;
        updateHoverOverlay();

        const ColorSpec fill = widgetForegroundOr(colorSpecFromRole(ColorRole::OnSurface));
        auto applyHoverProgress = [this, fill](float p) {
          m_hoverProgress = p;
          if (m_hoverOverlay != nullptr) {
            m_hoverOverlay->setVisible(p > 0.001f);
            ColorSpec color = fill;
            color.alpha = 0.1f * p;
            m_hoverOverlay->setFill(color);
          }
          requestRedraw();
        };

        if (m_animations == nullptr) {
          applyHoverProgress(0.0f);
          return;
        }

        m_animations->cancelForOwner(&m_hoverProgress);
        m_animations->animate(
            m_hoverProgress, 0.0f, Style::animFast, Easing::EaseOutCubic, applyHoverProgress, {}, &m_hoverProgress
        );
        requestFrameTick();
      });
    }
    item.area = static_cast<InputArea*>(m_container->addChild(std::move(area)));
    m_items.push_back(item);
  }

  // Size the container after targets are known.
  computeTargets();
  for (auto& it : m_items) {
    if (!animateFromSnapshot) {
      it.fromWidth = it.targetWidth;
      it.fromOpacity = it.targetOpacity;
    }
    it.currentWidth = it.fromWidth;
    it.currentOpacity = it.fromOpacity;
  }
  updateItemFlowPositions();

  const bool needsAnimation = std::ranges::any_of(m_items, [](const Item& it) {
    return std::fabs(it.targetX - it.currentX) > 0.5f
        || std::fabs(it.targetWidth - it.currentWidth) > 0.5f
        || std::fabs(it.targetOpacity - it.currentOpacity) > 0.01f;
  });
  applyItemLayouts();
  m_rebuildSnapshot.clear();

  float total = 0.0f;
  for (const auto& item : m_items) {
    if (item.currentWidth > 0.0f) {
      total = std::max(total, item.currentX + item.currentWidth);
    }
  }
  if (total <= 0.0f) {
    if (m_isVertical) {
      m_container->setFrameSize(m_indicatorHeight, 0.0f);
    } else {
      m_container->setFrameSize(0.0f, m_indicatorHeight);
    }
  } else if (m_isVertical) {
    m_container->setFrameSize(m_indicatorHeight, total);
  } else {
    m_container->setFrameSize(total, m_indicatorHeight);
  }

  ColorSpec hoverFill = widgetForegroundOr(colorSpecFromRole(ColorRole::OnSurface));
  hoverFill.alpha = 0.0f;
  m_hoverOverlay = static_cast<Box*>(m_container->addChild(
      ui::box({
          .fill = hoverFill,
          .visible = false,
          .configure = [](Box& box) {
            box.setParticipatesInLayout(false);
            box.setHitTestVisible(false);
          },
      })
  ));

  if (needsAnimation) {
    startAnimation();
  } else {
    finishAnimation();
  }
}

void WorkspacesWidget::computeTargets() {
  float cursor = 0.0f;
  for (auto& it : m_items) {
    const bool hidden = isWorkspaceHidden(it.workspace);
    const float w = (it.exiting || hidden) ? 0.0f : (it.workspace.active ? it.activeWidth : it.inactiveWidth);
    it.targetX = cursor;
    it.targetWidth = w;
    it.targetOpacity = (it.exiting || hidden) ? 0.0f : 1.0f;
    it.active = it.workspace.active;
    if (w > 0.0f) {
      cursor += w + m_gap;
    }
  }
}

void WorkspacesWidget::updateItemFlowPositions() {
  // A gap precedes an item only in proportion to how visible both it and the items before it are, so
  // gaps grow and collapse with the pills they separate. precedingProgress is the accumulated (clamped)
  // visibility of earlier items: it keeps the first visible pill from getting a leading gap.
  float cursor = 0.0f;
  float precedingProgress = 0.0f;
  for (auto& item : m_items) {
    const float itemProgress = std::clamp(item.currentOpacity, 0.0f, 1.0f);
    cursor += m_gap * std::min(precedingProgress, itemProgress);
    item.currentX = cursor;
    cursor += item.currentWidth;
    precedingProgress = std::min(1.0f, precedingProgress + itemProgress);
  }
}

void WorkspacesWidget::updateContainerSize() {
  if (m_container == nullptr || m_items.empty()) {
    return;
  }
  float total = 0.0f;
  for (const auto& item : m_items) {
    if (item.currentWidth > 0.0f) {
      total = std::max(total, item.currentX + item.currentWidth);
    }
  }

  if (m_animId != 0) {
    // The container is not clipped: it must always enclose the pills, so reserve the larger of the
    // current and target bounds. Taking the max keeps a shrinking transition from clipping pills that
    // are still wide, and a growing one from snapping the bar wider than the pills have reached.
    float targetTotal = 0.0f;
    for (const auto& item : m_items) {
      if (item.targetWidth > 0.0f) {
        targetTotal = std::max(targetTotal, item.targetX + item.targetWidth);
      }
    }
    total = std::max(total, targetTotal);
  }
  const float nextWidth = m_isVertical ? m_indicatorHeight : total;
  const float nextHeight = m_isVertical ? total : m_indicatorHeight;
  const bool sizeChanged = m_container->width() != nextWidth || m_container->height() != nextHeight;
  m_container->setFrameSize(nextWidth, nextHeight);
  if (Node* bounds = layoutBoundsNode(); sizeChanged && bounds != nullptr) {
    bounds->markLayoutDirty();
    requestUpdate();
  }
}

void WorkspacesWidget::ensureItemLabel(Renderer& renderer, Item& item, const Workspace& workspace) {
  if (!item.showLabel || item.area == nullptr) {
    return;
  }
  if (item.text != nullptr) {
    return;
  }

  const float labelFontSize = Style::fontSizeMini * m_contentScale;
  item.text = static_cast<Label*>(item.area->addChild(
      ui::label({
          .text = item.label,
          .fontSize = labelFontSize,
          .fontWeight = workspaceFontWeight(labelFontWeight(), m_minimal, workspace.active),
          .fontFamily = labelFontFamily(),
          .color = workspaceTextColor(workspace),
          .baselineMode = LabelBaselineMode::Text,
      })
  ));
  item.text->measure(renderer);
}

void WorkspacesWidget::recalculateItemMetrics(
    Renderer& renderer, Item& item, const Workspace& workspace, std::size_t displayIndex
) {
  const std::string label = workspaceLabel(workspace, displayIndex);
  const float labelFontSize = Style::fontSizeMini * m_contentScale;
  const float pillHeight = std::round(kWorkspacePillDefaultHeight * m_contentScale * m_pillScale);
  const float baseSize = std::round(pillHeight);
  const float padding = m_minimal ? (Style::spaceXs * m_contentScale) : (baseSize * 0.6f);
  const float iconSize = std::max(10.0f, std::round(baseSize * 0.70f));
  const float iconGap = std::max(1.0f, std::round(Style::spaceXs * m_contentScale * 0.5f));
  const FontWeight configuredFontWeight = labelFontWeight();

  item.label = label;
  item.showLabel = !item.showIcons && shouldShowWorkspaceLabel(workspace, label);

  if (isWorkspaceHidden(workspace)) {
    item.inactiveWidth = 0.0f;
    item.activeWidth = 0.0f;
    if (item.text != nullptr) {
      item.text->setVisible(false);
    }
    for (Image* icon : item.icons) {
      if (icon != nullptr) {
        icon->setVisible(false);
      }
    }
    return;
  }

  float textWidth = 0.0f;
  if (item.showLabel) {
    const FontWeight slotFontWeight = workspaceFontWeight(configuredFontWeight, m_minimal, workspace.active);
    const TextMetrics tm = renderer.measureText(label, labelFontSize, slotFontWeight);
    textWidth = std::max(tm.right - tm.left, tm.inkRight - tm.inkLeft);
  }

  if (m_minimal) {
    const float minWidth = baseSize;
    if (!item.showLabel) {
      const float iconsWidth = item.showIcons
          ? static_cast<float>(item.icons.size()) * iconSize + static_cast<float>(item.icons.size() - 1) * iconGap
          : 0.0f;
      const float iconBasedWidth = item.showIcons ? iconsWidth + padding * 2.0f : 0.0f;
      item.inactiveWidth = std::max(minWidth, iconBasedWidth);
      item.activeWidth = item.inactiveWidth;
    } else {
      const float textBasedWidth = textWidth + padding * 2.0f;
      item.inactiveWidth = std::max(minWidth, textBasedWidth);
      item.activeWidth = item.inactiveWidth;
    }
  } else {
    const float minWidth = workspaceMainAxisMinWidth(baseSize, false);
    const float minActiveWidth = workspaceMainAxisMinWidth(baseSize, true);
    if (!item.showLabel) {
      const float iconsWidth = item.showIcons
          ? static_cast<float>(item.icons.size()) * iconSize + static_cast<float>(item.icons.size() - 1) * iconGap
          : 0.0f;
      const float iconBasedWidth = item.showIcons ? iconsWidth + padding : 0.0f;
      item.inactiveWidth = std::max(minWidth, iconBasedWidth);
      item.activeWidth = std::max(minActiveWidth, iconBasedWidth);
    } else {
      const float textBasedWidth = textWidth + padding;
      item.inactiveWidth = std::max(minWidth, textBasedWidth);
      item.activeWidth = std::max(minActiveWidth, textBasedWidth);
    }
  }

  ensureItemLabel(renderer, item, workspace);
  if (item.text != nullptr) {
    item.text->setVisible(item.showLabel);
    if (item.showLabel) {
      item.text->setText(label);
      item.text->setFontWeight(workspaceFontWeight(configuredFontWeight, m_minimal, workspace.active));
      item.text->setColor(workspaceTextColor(workspace));
      item.text->measure(renderer);
    }
  }
  for (Image* icon : item.icons) {
    if (icon != nullptr) {
      icon->setVisible(item.showIcons);
    }
  }
}

void WorkspacesWidget::retarget(Renderer& renderer) {
  if (std::ranges::any_of(m_items, [](const Item& item) { return item.exiting; })) {
    scheduleRebuildFromSnapshot();
    return;
  }
  if (m_items.size() != m_cachedState.size()) {
    scheduleRebuildFromSnapshot();
    return;
  }

  m_activeUsesFocusedColor = !m_focusedOutputOnly || isFocusedOutput();
  for (auto& item : m_items) {
    const auto workspaceIt = std::ranges::find(m_cachedState, item.key, workspaceIdentityKey);
    if (workspaceIt == m_cachedState.end()) {
      scheduleRebuildFromSnapshot();
      return;
    }

    const auto displayIndex = static_cast<std::size_t>(std::ranges::distance(m_cachedState.begin(), workspaceIt));

    const auto& workspace = *workspaceIt;
    const Workspace previousVisualWorkspace = item.visualWorkspace;
    const bool holdPreviousVisualWorkspace = shouldHoldPreviousVisualWorkspace(previousVisualWorkspace, workspace);
    item.workspace = workspace;
    item.visualWorkspace = holdPreviousVisualWorkspace ? previousVisualWorkspace : workspace;
    item.releaseVisualAfterAnimation = holdPreviousVisualWorkspace;
    item.active = workspace.active;
    if (item.area != nullptr) {
      setWorkspaceClickHandler(*item.area, workspace);
    }
    recalculateItemMetrics(renderer, item, workspace, displayIndex);
    applyItemVisualStyle(item);
  }

  if (m_minimal) {
    computeTargets();
    for (auto& it : m_items) {
      it.currentX = it.targetX;
      it.currentWidth = it.targetWidth;
      it.currentOpacity = it.targetOpacity;
    }
    applyItemLayouts();
    updateContainerSize();
    if (root() != nullptr) {
      root()->markPaintDirty();
    }
    if (releaseHeldVisualStyles()) {
      requestRedraw();
    }
    return;
  }

  for (auto& it : m_items) {
    it.fromWidth = it.currentWidth;
    it.fromOpacity = it.currentOpacity;
  }
  computeTargets();
  startAnimation();
}

void WorkspacesWidget::startAnimation() {
  auto* mgr = m_animations;
  if (mgr == nullptr) {
    for (auto& item : m_items) {
      item.currentWidth = item.targetWidth;
      item.currentOpacity = item.targetOpacity;
    }
    updateItemFlowPositions();
    applyItemLayouts();
    updateContainerSize();
    finishAnimation();
    return;
  }
  cancelAnimation();
  requestFrameTick();
  requestRedraw();
  m_animId = mgr->animate(
      0.0f, 1.0f, kWorkspaceAnimDurationMs, Easing::EaseOutCubic,
      [this](float t) {
        for (auto& item : m_items) {
          item.currentWidth = std::lerp(item.fromWidth, item.targetWidth, t);
          item.currentOpacity = std::lerp(item.fromOpacity, item.targetOpacity, t);
        }
        updateItemFlowPositions();
        applyItemLayouts();
        updateContainerSize();
        if (root() != nullptr) {
          root()->markPaintDirty();
        }
      },
      [this]() { finishAnimation(); }, this
  );

  // Reserve final bounds before the first animated frame to avoid one-frame overflow.
  updateContainerSize();

  if (root() != nullptr) {
    root()->markPaintDirty();
  }
}

void WorkspacesWidget::cancelAnimation() {
  if (m_animId != 0 && m_animations != nullptr) {
    m_animations->cancel(m_animId);
  }
  m_animId = 0;
}

void WorkspacesWidget::finishAnimation() {
  m_animId = 0;
  const bool hasExitingItems = std::ranges::any_of(m_items, [](const Item& item) { return item.exiting; });

  if (m_container != nullptr && hasExitingItems) {
    for (const auto& item : m_items) {
      if (item.exiting && item.area != nullptr) {
        m_container->removeChild(item.area);
      }
    }
    std::erase_if(m_items, [](const Item& item) { return item.exiting; });
    updateContainerSize();
    requestUpdate();
  }

  if (releaseHeldVisualStyles()) {
    requestRedraw();
  }
}

void WorkspacesWidget::snapshotItemsForRebuild() {
  m_rebuildSnapshot.clear();
  if (m_minimal || m_items.empty()) {
    return;
  }

  m_rebuildSnapshot.reserve(m_items.size());
  for (const auto& item : m_items) {
    if (item.key.empty()) {
      continue;
    }
    m_rebuildSnapshot.push_back(
        ItemSnapshot{
            .key = item.key,
            .workspace = item.visualWorkspace,
            .label = item.label,
            .iconPaths = item.iconPaths,
            .showLabel = item.showLabel,
            .showIcons = item.showIcons,
            .width = item.currentWidth,
            .opacity = item.currentOpacity,
        }
    );
  }
}

void WorkspacesWidget::scheduleRebuildFromSnapshot() {
  if (m_rebuildSnapshot.empty()) {
    snapshotItemsForRebuild();
  }
  m_rebuildPending = true;
  if (root() != nullptr) {
    root()->markLayoutDirty();
  }
}

void WorkspacesWidget::applyItemLayouts() {
  for (auto& item : m_items) {
    applyItemLayout(item);
  }
}

void WorkspacesWidget::applyItemLayout(Item& it) {
  if (it.area == nullptr) {
    return;
  }
  const bool hidden = it.exiting || isWorkspaceHidden(it.workspace);
  const bool visible = !hidden || it.currentWidth > 0.0f;
  it.area->setVisible(visible);
  it.area->setParticipatesInLayout(visible);
  it.area->setOpacity(std::clamp(it.currentOpacity, 0.0f, 1.0f));

  const float position = std::round(it.currentX);
  const float itemW = m_isVertical ? m_indicatorHeight : it.currentWidth;
  const float itemH = m_isVertical ? it.currentWidth : m_indicatorHeight;

  it.area->setPosition(m_isVertical ? 0.0f : position, m_isVertical ? position : 0.0f);
  it.area->setFrameSize(itemW, itemH);
  if (it.indicator != nullptr) {
    it.indicator->setFrameSize(itemW, itemH);
    it.indicator->setRadius(workspacePillRadius(itemW, itemH));
  }

  if (m_hoveredArea == it.area) {
    updateHoverOverlay();
  }

  if (!it.icons.empty()) {
    const float baseSize = std::round(kWorkspacePillDefaultHeight * m_contentScale * m_pillScale);
    const float iconSize = std::max(10.0f, std::round(baseSize * 0.70f));
    const float iconGap = std::max(1.0f, std::round(Style::spaceXs * m_contentScale * 0.5f));
    const float iconsWidth =
        static_cast<float>(it.icons.size()) * iconSize + static_cast<float>(it.icons.size() - 1) * iconGap;
    const bool showIcons = it.showIcons && it.currentWidth + 0.5f >= it.inactiveWidth;
    float x = std::max(0.0f, (itemW - iconsWidth) * 0.5f);
    const float y = std::max(0.0f, (itemH - iconSize) * 0.5f);
    for (Image* icon : it.icons) {
      if (icon == nullptr) {
        continue;
      }
      icon->setVisible(showIcons);
      icon->setPosition(x, y);
      x += iconSize + iconGap;
    }
  }

  if (it.text == nullptr) {
    return;
  }

  const bool showText = it.showLabel && it.currentWidth + 0.5f >= it.inactiveWidth;
  it.text->setVisible(showText);
  if (!showText) {
    return;
  }

  // Box-center the (text-only) label, unrounded: the renderer snaps the glyph
  // quad to the pixel grid, so rounding here would double-round the baseline.
  const float textX = (itemW - it.text->width()) * 0.5f;
  const float textY = (itemH - it.text->height()) * 0.5f;
  it.text->setPosition(std::max(0.0f, textX), textY);
}

void WorkspacesWidget::updateHoverOverlay() {
  const auto hoveredIt =
      m_hoveredArea != nullptr ? std::ranges::find(m_items, m_hoveredArea, &Item::area) : m_items.end();
  if (hoveredIt == m_items.end() || hoveredIt->exiting) {
    m_hoveredArea = nullptr;
    for (auto& item : m_items) {
      applyItemVisualStyle(item);
    }
    return;
  }

  Item& hoveredItem = *hoveredIt;

  if (!m_minimal) {
    if (m_hoverOverlay != nullptr) {
      m_hoverOverlay->setVisible(false);
    }
    for (auto& item : m_items) {
      if (&item == &hoveredItem) {
        if (item.indicator != nullptr) {
          item.indicator->setFill(colorSpecFromRole(ColorRole::Hover));
        }
        if (item.text != nullptr) {
          item.text->setColor(colorSpecFromRole(ColorRole::OnHover));
        }
      } else {
        applyItemVisualStyle(item);
      }
    }
    return;
  }

  // Minimal mode uses the translucent overlay
  if (m_hoverOverlay == nullptr) {
    return;
  }
  const float w = hoveredItem.currentWidth;
  const float indicatorW = m_isVertical ? m_indicatorHeight : w;
  const float indicatorH = m_isVertical ? w : m_indicatorHeight;

  m_hoverOverlay->setRadius(workspacePillRadius(indicatorW, indicatorH));
  if (m_isVertical) {
    m_hoverOverlay->setPosition(0.0f, std::round(hoveredItem.currentX));
    m_hoverOverlay->setFrameSize(m_indicatorHeight, w);
  } else {
    m_hoverOverlay->setPosition(std::round(hoveredItem.currentX), 0.0f);
    m_hoverOverlay->setFrameSize(w, m_indicatorHeight);
  }
}

float WorkspacesWidget::workspacePillRadius(float width, float height) const noexcept {
  return resolvedBarCapsuleRadius(width, height);
}

float WorkspacesWidget::workspaceMainAxisMinWidth(float baseSize, bool active) const noexcept {
  return baseSize * (active ? m_activePillSize : m_inactivePillSize);
}

WorkspacesWidget::~WorkspacesWidget() { cancelAnimation(); }

std::optional<std::size_t> WorkspacesWidget::activeWorkspaceIndex() const {
  for (std::size_t i = 0; i < m_cachedState.size(); ++i) {
    if (m_cachedState[i].active) {
      return i;
    }
  }
  return std::nullopt;
}

void WorkspacesWidget::activateAdjacentWorkspace(int direction) {
  if (m_cachedState.empty() || direction == 0) {
    return;
  }

  const auto active = activeWorkspaceIndex();
  std::size_t targetIndex = 0;
  if (!active.has_value()) {
    targetIndex = direction > 0 ? 0 : (m_cachedState.size() - 1);
  } else {
    const std::size_t current = *active;
    if (direction > 0) {
      if (current + 1 >= m_cachedState.size()) {
        return;
      }
      targetIndex = current + 1;
    } else {
      if (current == 0) {
        return;
      }
      targetIndex = current - 1;
    }
  }

  m_platform.activateWorkspace(m_output, m_cachedState[targetIndex]);
}

std::string WorkspacesWidget::workspaceLabel(const Workspace& workspace, std::size_t displayIndex) const {
  const DisplayMode displayMode = effectiveDisplayMode();
  if (displayMode == DisplayMode::Id) {
    if (workspace.index > 0) {
      return std::to_string(workspace.index);
    }
    if (const auto numericId = numericWorkspaceId(workspace); numericId.has_value()) {
      return std::to_string(*numericId);
    }
    return std::to_string(displayIndex + 1);
  }
  if (displayMode == DisplayMode::Name) {
    std::string label = !workspace.name.empty() ? workspace.name : workspace.id;
    // Only truncate non-numeric labels (words like "VESKTOP" → "VE").
    // Numeric labels (workspace IDs like "10", "11") stay as-is.
    if (!isNumericLabel(label) && m_maxLabelChars > 0) {
      label = StringUtils::truncateUtf8CodePoints(label, m_maxLabelChars);
    }
    return label;
  }
  return {};
}

std::string WorkspacesWidget::workspaceKey(const Workspace& workspace, std::size_t displayIndex) const {
  if (workspace.index > 0) {
    return std::to_string(workspace.index);
  }
  if (const auto numericId = numericWorkspaceId(workspace); numericId.has_value()) {
    return std::to_string(*numericId);
  }
  if (!workspace.id.empty()) {
    return workspace.id;
  }
  if (!workspace.name.empty()) {
    return workspace.name;
  }
  return std::to_string(displayIndex + 1);
}

bool WorkspacesWidget::hasWorkspaceApps(const Workspace& workspace, std::size_t displayIndex) const {
  const auto appsIt = m_cachedAppsByWorkspace.find(workspaceKey(workspace, displayIndex));
  return appsIt != m_cachedAppsByWorkspace.end() && !appsIt->second.empty();
}

std::vector<std::string> WorkspacesWidget::workspaceAppIcons(const Workspace& workspace, std::size_t displayIndex) {
  const auto appsIt = m_cachedAppsByWorkspace.find(workspaceKey(workspace, displayIndex));
  if (appsIt == m_cachedAppsByWorkspace.end() || appsIt->second.empty()) {
    return {};
  }

  std::vector<std::string> icons;
  icons.reserve(std::min<std::size_t>(appsIt->second.size(), 4));
  for (const auto& appId : appsIt->second) {
    if (icons.size() >= 4) {
      break;
    }
    const std::string iconPath = resolveAppIconPath(appId);
    if (!iconPath.empty()) {
      icons.push_back(iconPath);
    }
  }
  return icons;
}

void WorkspacesWidget::buildDesktopIconIndex() {
  const std::uint64_t version = desktopEntriesVersion();
  if (m_desktopEntriesVersion == version && !m_appIconsByLower.empty()) {
    return;
  }

  m_appIconsByLower.clear();
  for (const auto& entry : desktopEntries()) {
    if (entry.icon.empty()) {
      continue;
    }
    if (!entry.idLower.empty()) {
      m_appIconsByLower[entry.idLower] = entry.icon;
    }
    if (!entry.startupWmClassLower.empty()) {
      m_appIconsByLower[entry.startupWmClassLower] = entry.icon;
    }
    if (!entry.nameLower.empty()) {
      m_appIconsByLower[entry.nameLower] = entry.icon;
    }
  }
  m_desktopEntriesVersion = version;
}

std::string WorkspacesWidget::resolveAppIconPath(const std::string& appId) {
  const int iconTargetSize = static_cast<int>(std::round(48.0f * m_contentScale));
  auto resolveIconName = [this, iconTargetSize](const std::string& name) -> std::string {
    if (name.empty()) {
      return {};
    }
    return m_iconResolver.resolve(name, iconTargetSize);
  };

  if (appId.starts_with("steam_app_")) {
    if (const auto entry = app_identity::findDesktopEntry(appId, desktopEntries());
        entry.has_value() && !entry->icon.empty()) {
      if (const std::string steamIcon = resolveIconName(entry->icon); !steamIcon.empty()) {
        return steamIcon;
      }
    }
  }

  if (const auto internal = internal_apps::metadataForAppId(appId); internal.has_value()) {
    return internal->iconPath;
  }

  const std::string appIdLower = StringUtils::toLower(appId);
  if (const auto it = m_appIconsByLower.find(appIdLower); it != m_appIconsByLower.end()) {
    if (const std::string desktopIcon = resolveIconName(it->second); !desktopIcon.empty()) {
      return desktopIcon;
    }
  }

  if (const std::string appIcon = resolveIconName(appId); !appIcon.empty()) {
    return appIcon;
  }
  return resolveIconName("application-x-executable");
}

std::optional<std::size_t> WorkspacesWidget::numericWorkspaceId(const Workspace& workspace) {
  const auto parseLeadingNumber = [](const std::string& value) -> std::optional<std::size_t> {
    if (value.empty() || !std::isdigit(static_cast<unsigned char>(value.front()))) {
      return std::nullopt;
    }

    std::size_t parsed = 0;
    std::size_t index = 0;
    while (index < value.size() && std::isdigit(static_cast<unsigned char>(value[index]))) {
      parsed = (parsed * 10) + static_cast<std::size_t>(value[index] - '0');
      ++index;
    }
    return parsed > 0 ? std::optional<std::size_t>(parsed) : std::nullopt;
  };

  if (const auto id = parseLeadingNumber(workspace.id); id.has_value()) {
    return id;
  }
  if (const auto name = parseLeadingNumber(workspace.name); name.has_value()) {
    return name;
  }
  return std::nullopt;
}

bool WorkspacesWidget::isFocusedOutput() const { return m_platform.preferredInteractiveOutput() == m_output; }

ColorSpec WorkspacesWidget::workspaceFillColor(const Workspace& workspace) const {
  if (workspace.active) {
    if (m_activeUsesFocusedColor) {
      return m_focusedColor;
    }
    return m_occupiedColor;
  }
  if (workspace.urgent) {
    return m_urgentColor;
  }
  if (workspace.occupied) {
    return m_occupiedColor;
  }
  ColorSpec color = m_emptyColor;
  color.alpha *= 0.55f;
  return color;
}

ColorSpec WorkspacesWidget::workspaceTextColor(const Workspace& workspace) const {
  if (workspace.urgent) {
    return m_minimal ? m_urgentColor : readableColorForFill(m_urgentColor);
  }
  if (!m_minimal) {
    return readableColorForFill(workspaceFillColor(workspace));
  }
  if (workspace.active) {
    if (m_activeUsesFocusedColor) {
      return m_focusedColor;
    }
    return m_occupiedColor;
  }
  if (workspace.occupied) {
    return m_occupiedColor;
  }
  ColorSpec color = widgetForegroundOr(colorSpecFromRole(ColorRole::OnSurfaceVariant));
  color.alpha *= 0.55f;
  return color;
}

ColorRole WorkspacesWidget::onRoleForFill(ColorRole fill) {
  switch (fill) {
  case ColorRole::Primary:
    return ColorRole::OnPrimary;
  case ColorRole::Secondary:
    return ColorRole::OnSecondary;
  case ColorRole::Tertiary:
    return ColorRole::OnTertiary;
  case ColorRole::Error:
    return ColorRole::OnError;
  case ColorRole::Surface:
  case ColorRole::SurfaceVariant:
  case ColorRole::Outline:
  case ColorRole::Shadow:
  case ColorRole::Hover:
  case ColorRole::OnPrimary:
  case ColorRole::OnSecondary:
  case ColorRole::OnTertiary:
  case ColorRole::OnError:
  case ColorRole::OnSurface:
  case ColorRole::OnSurfaceVariant:
  case ColorRole::OnHover:
    return ColorRole::OnSurface;
  }
  return ColorRole::OnSurface;
}

ColorSpec WorkspacesWidget::readableColorForFill(const ColorSpec& fill) {
  if (fill.role.has_value()) {
    return colorSpecFromRole(onRoleForFill(*fill.role));
  }
  return fixedColorSpec(readableTextColorForBackground(resolveColorSpec(fill)));
}
