# Local Noctalia v5 deployment

## Deployment source of truth

Read the desktop-shell
[`README.md`](/home/ez/Projects/desktop-stack/ops/stacks/desktop-shell/README.md)
before a deployment. Use these operations files as the source of truth:

- `/home/ez/Projects/desktop-stack/ops/stacks/desktop-shell/scripts/deploy/deploy-laptop`: full laptop deployment.
- `/home/ez/Projects/desktop-stack/ops/stacks/desktop-shell/scripts/deploy/deploy-v5-runtime-to-laptop`: runtime-only laptop deployment.
- `/home/ez/Projects/desktop-stack/ops/stacks/desktop-shell/scripts/deploy/install-v5-runtime-bundle`: laptop runtime installer and rollback logic.
- `/home/ez/Projects/desktop-stack/ops/stacks/desktop-shell/inventory.json`: host and repository paths.
- `/home/ez/Projects/desktop-stack/ops/stacks/desktop-shell/hosts/laptop/sync.json`: laptop profile settings.

Do not start with manual file copies. Preview the operations deployment first.
Use manual commands only when the dotfiles scripts cannot run.

## Desktop deployment

- Source: `/home/ez/Projects/desktop-stack/sources/noctalia-shell`
- Fork branch: `main`
- Upstream baseline: `a064c063f`
- Active prefix: `/home/ez/.local/opt/noctalia-v5-patched`
- Active launcher: `/home/ez/.local/bin/noctalia-v5`
- Active message client: `/home/ez/.local/bin/noctalia-v5-msg`
- Active config: `/home/ez/.config/noctalia/config.toml`
- Niri startup file: `/home/ez/.config/niri/config.d/40-startup-and-general.kdl`
- v4 rollback: `/home/ez/.local/share/noctalia-rollback/v4-desktop-20260728`

Niri starts the local launcher. The desktop does not use `/usr/bin/noctalia`.
Do not start the old `qs -c noctalia-shell` process with v5.

## Laptop deployment

- Host: `eza@192.168.1.221`
- Build host: desktop
- Active prefix: `/home/eza/.local/opt/noctalia-v5-patched`
- Active launcher: `/home/eza/.local/bin/noctalia-v5`
- Active message client: `/home/eza/.local/bin/noctalia-v5-msg`

Do not compile Noctalia on the laptop. Build and test Noctalia on the desktop.
Copy the verified installation from the desktop to the laptop.

For a complete laptop deployment, including the machine profile, run:

```sh
cd /home/ez/Projects/desktop-stack/ops
stacks/desktop-shell/scripts/deploy/deploy-laptop --dry-run
stacks/desktop-shell/scripts/deploy/deploy-laptop
```

For a runtime-only deployment, run:

```sh
cd /home/ez/Projects/desktop-stack/ops
stacks/desktop-shell/scripts/deploy/deploy-v5-runtime-to-laptop --dry-run --no-source-sync
stacks/desktop-shell/scripts/deploy/deploy-v5-runtime-to-laptop --no-source-sync
```

The runtime script creates a versioned artifact. The installer verifies file
hashes, architecture, Fedora version, glibc version, and RPATH. The installer
also creates a rollback backup before it replaces the active runtime.

Use this manual restart only for recovery:

```sh
pid="$(pgrep -o -f '(^|/)noctalia( |$)')"
niri_socket="$(tr '\0' '\n' <"/proc/$pid/environ" | sed -n 's/^NIRI_SOCKET=//p')"
kill -TERM "$pid"
NIRI_SOCKET="$niri_socket" niri msg action spawn -- \
  "$HOME/.local/bin/noctalia-v5" --daemon
```

Verify the laptop deployment:

```sh
pgrep -a -f '(^|/)noctalia( |$)'
$HOME/.local/opt/noctalia-v5-patched/bin/noctalia --version
WAYLAND_DISPLAY=wayland-1 XDG_RUNTIME_DIR=/run/user/1000 \
  $HOME/.local/bin/noctalia-v5-msg status
ldd $HOME/.local/opt/noctalia-v5-patched/bin/noctalia | rg 'not found'
```

The `ldd` command must produce no output.

## Fedora build dependencies

```sh
sudo dnf install meson gcc-c++ just \
  wayland-devel wayland-protocols-devel \
  libEGL-devel mesa-libGLES-devel \
  freetype-devel fontconfig-devel cairo-devel pango-devel harfbuzz-devel \
  libxkbcommon-devel glib2-devel libsecret-devel libsodium-devel \
  sdbus-cpp-devel pipewire-devel wireplumber-devel pam-devel polkit-devel \
  libcurl-devel libwebp-devel libjxl-devel libsndfile-devel librsvg2-devel \
  libqalculate-devel libxml2-devel md4c-devel tomlplusplus-devel \
  libical-devel json-devel stb_image_resize2-devel stb_image_write-devel \
  jemalloc-devel
```

Fedora package name is `json-devel`, not `nlohmann-json-devel`.

## Build and test

```sh
cd /home/ez/Projects/desktop-stack/sources/noctalia-shell
meson setup build-release --buildtype=release -Dcpp_std=c++23 \
  -Dtests=enabled -Db_lto=true \
  --prefix="$HOME/.local/opt/noctalia-v5-patched"
meson compile -C build-release
meson test -C build-release --print-errorlogs
```

All 90 tests must pass. Run the build on the desktop. Deploy the verified
installation to both machines. Do not compile Noctalia on the laptop.

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

- Grouped-taskbar active marker and icon padding.
- Niri workspace task ordering by layout.
- Taskbar explicit target outputs and workspace drag/drop routing.
- Plugin bar drag sources with click support.

Use the upstream `WorkspacesWidget`. The XMB plugin provides the custom workspace strip.

Keep upstream plugin frame ticks, service exit reasons, tray fixes, and bar
fixes. Keep the local taskbar fields `target_output`, `drag_drop_command`,
and `middle_click_command`.

## Desktop cutover checks

- Confirm no Noctalia bar appears on `HDMI-A-1`.
- Confirm one top bar appears on `HDMI-A-2`.
- Confirm the bar contains one taskbar for each output.
- Confirm each taskbar shows only its target output.
- Confirm taskbar clicks and same-monitor drag work.
- Confirm `Mod+Z` and XMB drag work.
- Confirm tray, audio, network, notifications, lock, and wallpaper work.
- Confirm `pgrep -a qs` and `pgrep -a neowall` return no process.
