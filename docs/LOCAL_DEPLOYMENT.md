# Local Noctalia v5 deployment

## Active deployment

- Source: `/home/eza/Projects/noctalia-shell`
- Branch: `noctalia-v5-workspace-apps`
- Current upstream baseline: `98f0d2b4a` (`v5.0.0-beta.3-86-g98f0d2b4a`)
- Active prefix: `$HOME/.local/opt/noctalia-v5-patched`
- Active binary: `$HOME/.local/opt/noctalia-v5-patched/bin/noctalia`
- Active launcher: `$HOME/.local/bin/noctalia-v5 --daemon`
- Niri startup config: `$HOME/.config/niri/config.d/40-startup-and-general.kdl`
- Runtime config/state/data: `$HOME/.local/share/noctalia-v5-test/{config,state,data}`

`/usr/bin/noctalia` is not active. Niri starts local launcher above.

## Fedora build dependencies

```sh
sudo dnf install md4c-devel json-devel tomlplusplus-devel stb-devel
```

Fedora package name is `json-devel`, not `nlohmann-json-devel`.
Upstream now requires system md4c, nlohmann/json, toml++, and stb. Vendored copies removed.

## Build, test, install

```sh
cd /home/eza/Projects/noctalia-shell
just configure release "$HOME/.local/opt/noctalia-v5-patched"
just build release
just test release
just install release
```

`just test release` must finish `42/42` tests with zero failures before install.
Install local prefix as normal user. Do not use `sudo` for this prefix.

If Meson install fails on `build-release/meson-logs/install-log.txt` permission, stale log came from prior root install. Remove that generated log, then retry:

```sh
rm build-release/meson-logs/install-log.txt
just install release
```

## Restart active shell

No `noctalia.service` exists. Niri owns process scope.

```sh
systemctl --user list-units --all --no-legend | rg 'niri-noctalia'
systemctl --user stop '<exact scope name from previous command>'
niri msg action spawn -- "$HOME/.local/bin/noctalia-v5" --daemon
```

Verify:

```sh
pgrep -a -f '(^|/)noctalia( |$)'
$HOME/.local/opt/noctalia-v5-patched/bin/noctalia --version
systemctl --user list-units --all --no-legend | rg 'niri-noctalia'
```

Expected process path: `$HOME/.local/opt/noctalia-v5-patched/bin/noctalia`.

## Current local changes retained across upstream merge

- Workspace app initials and icons.
- Grouped-taskbar active marker and icon padding.
- Niri workspace task ordering by layout.
- Taskbar explicit target outputs and workspace drag/drop routing.

Merge resolved workspace widget conflict by keeping upstream hover and entry/exit animation behavior plus local workspace app-icon pills.
