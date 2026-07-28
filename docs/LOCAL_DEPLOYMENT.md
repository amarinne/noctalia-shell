# Local Noctalia v5 deployment

## Desktop deployment

- Source: `/home/ez/Projects/noctalia-shell`
- Fork merge: `06a5cbe9c`
- Upstream baseline: `082e0e386`
- Active prefix: `/home/ez/.local/opt/noctalia-v5-patched`
- Active launcher: `/home/ez/.local/bin/noctalia-v5`
- Active message client: `/home/ez/.local/bin/noctalia-v5-msg`
- Active config: `/home/ez/.config/noctalia/config.toml`
- Niri startup file: `/home/ez/.config/niri/config.d/40-startup-and-general.kdl`
- v4 rollback: `/home/ez/.local/share/noctalia-rollback/v4-desktop-20260728`

Niri starts the local launcher. The desktop does not use `/usr/bin/noctalia`.
Do not start the old `qs -c noctalia-shell` process with v5.

## Fedora build dependencies

```sh
sudo dnf install md4c-devel json-devel tomlplusplus-devel stb-devel
```

Fedora package name is `json-devel`, not `nlohmann-json-devel`.
Upstream now requires system md4c, nlohmann/json, toml++, and stb. Vendored copies removed.

## Build and test

```sh
cd /home/ez/Projects/noctalia-shell
meson setup build-release --buildtype=release -Dcpp_std=c++23 \
  -Dtests=enabled -Db_lto=true \
  --prefix="$HOME/.local/opt/noctalia-v5-patched"
meson compile -C build-release
meson test -C build-release --print-errorlogs
```

All 62 tests must pass. Run the build on the desktop. Do not compile Noctalia
on the laptop.

The desktop package bundles private libraries in `lib/`. The installed binary
must have this RPATH:

```text
$ORIGIN/../lib
```

Do not deploy a binary with an absolute build-host RPATH.

## Restart the active shell

No `noctalia.service` exists. Niri owns process scope.

```sh
qs -c noctalia-shell kill --any-display
niri msg action spawn -- "$HOME/.local/bin/noctalia-v5" --daemon
```

Verify:

```sh
pgrep -a -f '(^|/)noctalia( |$)'
$HOME/.local/opt/noctalia-v5-patched/bin/noctalia --version
systemctl --user list-units --all --no-legend | rg 'niri-noctalia'
```

The expected process path is
`$HOME/.local/opt/noctalia-v5-patched/bin/noctalia`.

## Local changes retained across upstream merges

- Workspace app initials and icons.
- Grouped-taskbar active marker and icon padding.
- Niri workspace task ordering by layout.
- Taskbar explicit target outputs and workspace drag/drop routing.
- Plugin bar drag sources with click support.

Keep upstream plugin frame ticks, service exit reasons, tray fixes, and bar
fixes. Keep the local taskbar fields `target_output` and `drag_drop_command`.

## Desktop cutover checks

- Confirm no Noctalia bar appears on `HDMI-A-1`.
- Confirm one top bar appears on `HDMI-A-2`.
- Confirm the bar contains one taskbar for each output.
- Confirm each taskbar shows only its target output.
- Confirm taskbar clicks and same-monitor drag work.
- Confirm `Mod+Z` and XMB drag work.
- Confirm tray, audio, network, notifications, lock, and wallpaper work.
- Confirm `pgrep -a qs` and `pgrep -a neowall` return no process.
