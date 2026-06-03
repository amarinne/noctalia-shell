#pragma once

#include "config/config_types.h"
#include "config/schema/field.h"

// Per-section field schemas: the single source of truth for reading, writing,
// and validating each config section. Field order matches the legacy
// configToToml emission order so serialization stays byte-identical.
namespace noctalia::config::schema {

  const Schema<AudioConfig>& audioSchema();
  const Schema<WeatherConfig>& weatherSchema();
  const Schema<OsdConfig>& osdSchema();
  const Schema<BackdropConfig>& backdropSchema();
  const Schema<LockscreenConfig>& lockscreenSchema();
  const Schema<SystemConfig>& systemSchema();
  const Schema<NightLightConfig>& nightlightSchema();
  const Schema<LocationConfig>& locationSchema();
  const Schema<NotificationConfig>& notificationSchema();
  const Schema<DockConfig>& dockSchema();
  const Schema<BrightnessConfig>& brightnessSchema();
  const Schema<BatteryConfig>& batterySchema();
  const Schema<ControlCenterConfig>& controlCenterSchema();
  const Schema<CalendarConfig>& calendarSchema();
  const Schema<KeybindsConfig>& keybindsSchema();
  const Schema<HooksConfig>& hooksSchema();
  const Schema<IdleConfig>& idleSchema();
  const Schema<WallpaperConfig>& wallpaperSchema();
  const Schema<ThemeConfig>& themeSchema();
  const Schema<ShellConfig>& shellSchema();

  // Bar is handled at the config root (named [bar.<name>] tables + an `order`
  // array live on Config::bars directly, not in a section struct), so its
  // schemas are consumed by hand-written loops in config_service/config_export
  // rather than a single section writeTable.
  //
  //  - barFieldsSchema: every concrete BarConfig field EXCEPT position/name.
  //    Used for the base bar (read + write) and the resolve-and-flatten write of
  //    each monitor override.
  //  - barMonitorOverrideSchema: the parallel optional fields of a monitor
  //    override, used for READ only (overrides serialize via barFieldsSchema on
  //    the resolved bar).
  const Schema<BarConfig>& barFieldsSchema();
  const Schema<BarMonitorOverride>& barMonitorOverrideSchema();

} // namespace noctalia::config::schema
