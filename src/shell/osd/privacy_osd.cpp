#include "shell/osd/privacy_osd.h"

#include "config/config_types.h"
#include "core/log.h"
#include "i18n/i18n.h"
#include "pipewire/pipewire_service.h"
#include "shell/osd/osd_overlay.h"

#include <string_view>

namespace {
  constexpr Logger kLog("osd");

  enum class PrivacyKind {
    Mic,
    Camera,
    Screen,
  };

  OsdContent makePrivacyContent(PrivacyKind kind, bool active) {
    switch (kind) {
    case PrivacyKind::Mic:
      return OsdContent{
          .kind = OsdKind::Privacy,
          .icon = active ? "microphone" : "microphone-off",
          .value = i18n::tr(active ? "osd.privacy.mic-on" : "osd.privacy.mic-off"),
          .showProgress = false,
      };
    case PrivacyKind::Camera:
      return OsdContent{
          .kind = OsdKind::Privacy,
          .icon = active ? "camera" : "camera-off",
          .value = i18n::tr(active ? "osd.privacy.camera-on" : "osd.privacy.camera-off"),
          .showProgress = false,
      };
    case PrivacyKind::Screen:
      return OsdContent{
          .kind = OsdKind::Privacy,
          .icon = active ? "screen-share" : "screen-share-off",
          .value = i18n::tr(active ? "osd.privacy.screen-on" : "osd.privacy.screen-off"),
          .showProgress = false,
      };
    }

    return OsdContent{};
  }

  [[nodiscard]] std::optional<std::regex> compileFilter(std::string_view key, const std::string& pattern) {
    if (pattern.empty()) {
      return std::nullopt;
    }
    try {
      return std::regex(pattern);
    } catch (const std::regex_error& e) {
      kLog.warn("privacy osd: invalid {} '{}': {}", key, pattern, e.what());
      return std::nullopt;
    }
  }

} // namespace

PrivacyOsd::State PrivacyOsd::fromPipewireState(const PrivacyState& privacyState) const {
  State out;
  for (const auto& capture : privacyState.captures) {
    switch (capture.kind) {
    case PrivacyCaptureKind::Microphone:
      if (!matchesFilter(m_micFilter, capture.appName)) {
        out.mic = true;
      }
      break;
    case PrivacyCaptureKind::Camera:
      if (!matchesFilter(m_camFilter, capture.appName)) {
        out.camera = true;
      }
      break;
    case PrivacyCaptureKind::Screen:
      out.screen = true;
      break;
    }
  }
  return out;
}

void PrivacyOsd::bindOverlay(OsdOverlay& overlay) { m_overlay = &overlay; }

void PrivacyOsd::configure(const Config& config) {
  const std::string& micPattern = config.shell.privacy.micFilterRegex;
  const std::string& camPattern = config.shell.privacy.camFilterRegex;

  if (micPattern != m_micFilterPattern) {
    m_micFilterPattern = micPattern;
    m_micFilter = compileFilter("shell.privacy.mic_filter_regex", m_micFilterPattern);
  }
  if (camPattern != m_camFilterPattern) {
    m_camFilterPattern = camPattern;
    m_camFilter = compileFilter("shell.privacy.cam_filter_regex", m_camFilterPattern);
  }
}

void PrivacyOsd::onConfigReload(const Config& config, const PipeWireService* service) {
  configure(config);
  if (service != nullptr) {
    m_lastState = fromPipewireState(service->privacyState());
  }
}

void PrivacyOsd::onPrivacyStateChanged(const PipeWireService& service) {
  const State current = fromPipewireState(service.privacyState());

  if (m_overlay == nullptr) {
    m_lastState = current;
    return;
  }

  // Surface each capture independently: multiple kinds can change in one update
  // (e.g. mic + screen both stop on call-leave, or both already active at startup).
  if (m_lastState.mic != current.mic) {
    m_overlay->show(makePrivacyContent(PrivacyKind::Mic, current.mic));
  }
  if (m_lastState.camera != current.camera) {
    m_overlay->show(makePrivacyContent(PrivacyKind::Camera, current.camera));
  }
  if (m_lastState.screen != current.screen) {
    m_overlay->show(makePrivacyContent(PrivacyKind::Screen, current.screen));
  }

  m_lastState = current;
}

bool PrivacyOsd::matchesFilter(const std::optional<std::regex>& filter, const std::string& value) const {
  return filter.has_value() && std::regex_search(value, *filter);
}
