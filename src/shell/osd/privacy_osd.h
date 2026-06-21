#pragma once

#include <optional>
#include <regex>
#include <string>

struct Config;
class OsdOverlay;
class PipeWireService;
struct PrivacyState;

class PrivacyOsd {
public:
  void bindOverlay(OsdOverlay& overlay);
  void configure(const Config& config);
  void onConfigReload(const Config& config, const PipeWireService* service);
  void onPrivacyStateChanged(const PipeWireService& service);

private:
  struct State {
    bool mic = false;
    bool camera = false;
    bool screen = false;

    bool operator==(const State&) const = default;
  };

  [[nodiscard]] State fromPipewireState(const PrivacyState& privacyState) const;
  [[nodiscard]] bool matchesFilter(const std::optional<std::regex>& filter, const std::string& value) const;

  OsdOverlay* m_overlay = nullptr;
  std::string m_micFilterPattern;
  std::string m_camFilterPattern;
  std::optional<std::regex> m_micFilter;
  std::optional<std::regex> m_camFilter;
  // Baseline starts empty by contract: the first PipeWire enumeration announces
  // any capture already active at launch as an on-transition. Do not prime from
  // live state or these startup notifications are lost.
  State m_lastState;
};
