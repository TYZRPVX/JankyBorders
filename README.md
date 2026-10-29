# JankyBorders

A lightweight window border tool for macOS, forked from
[FelixKratz/JankyBorders](https://github.com/FelixKratz/JankyBorders).
This fork focuses on reliable focus borders across displays and Spaces, local
service installation, adaptive colors, and configurable window corners.

![Window border example](images/screenshot.png)

## What this fork adds

Recent changes include:

- **Configurable corners:** `radius=auto` or a numeric radius, fractional system
  radii, and a 16-point fallback on macOS 27 when a window reports no usable
  radius. Rounded borders use the system continuous-corner path on macOS 26+
  when available and all four radii are equal.
- **Focus and window lifecycle fixes:** restore borders across displays and
  Spaces, hide them during Mission Control, remove them when a window starts
  closing, and bound Accessibility queries to avoid stalled updates.
- **Consistent placement:** `position=auto|inside|center|outside`, including
  adjustments for edges that would otherwise be clipped by a display boundary.
- **Focused-window mode:** `active_only=on` keeps only the focused window's
  border, reducing the number of backing surfaces.
- **Adaptive colors:** choose contrast colors per edge or one consistent color
  for the entire focused border.
- **Local service management:** build and install a signed, versioned app with
  update, restart, and rollback commands.

The upstream deployment target is macOS 14+. This fork has a documented
verification baseline on macOS 26.5.2 and subsequent local validation on
macOS 27.0.1. This is not a guarantee of compatibility with every release or
application: rendering and window tracking use private macOS APIs.

## Install this fork

Build from this checkout to get the features described here. The upstream
Homebrew package does not install this fork.

You need macOS and the Xcode Command Line Tools (`clang` and `make`).

```bash
git clone https://github.com/TYZRPVX/JankyBorders.git
cd JankyBorders
make install-service
```

The installer builds the source, creates a signed `JankyBorders.app`, and
starts a per-user LaunchAgent. The installation uses:

| Item | Location |
| --- | --- |
| Versioned app and rollback versions | `~/.local/opt/jankyborders` |
| Command | `~/.local/bin/borders` |
| Man page | `~/.local/share/man/man1/borders.1` |
| LaunchAgent label | `io.github.qeesung.jankyborders.local` |
| Logs | `~/Library/Logs/JankyBorders` |

Put `~/.local/bin` on your shell's `PATH` before other locations containing
`borders`. Check which command your shell uses with `command -v borders`.
The managed service supplies its own `PATH` with this directory first.

Use one startup mechanism. Before installing, stop an existing Homebrew service
with `brew services stop borders`, or stop a manually launched instance and
remove its startup entry from `yabairc` or `aerospace.toml`. The installer
refuses conflicting processes and unmanaged files at its installation paths.
If you previously copied a binary to `~/.local/bin/borders`, back it up and
move it out of that path before migrating to the managed service.

On first installation, signing setup creates a stable local code-signing
identity in your login keychain. macOS may ask you to unlock the keychain and
approve code-signing trust. This identity lets compatible service updates and
rollbacks retain the same application identity for screen-recording permission.
It is local to this Mac, not a Developer ID for distributing builds.
Use `make signing-status` to inspect it, or `make signing-help` for manual setup.

### Build and run without a service

For a foreground development run:

```bash
make
PATH="$PWD/bin:$PATH" ./bin/borders
```

Stop it with `Ctrl-C`. If another instance is already running, invoking
`borders` sends settings to that instance instead of starting another daemon.
When a configuration script calls `borders`, ensure its `PATH` resolves to the
build you intend to use.

## Configure borders

When started without arguments, Borders executes the first configuration file
found in this order:

1. `$XDG_CONFIG_HOME/borders/bordersrc`, when `XDG_CONFIG_HOME` is absolute.
2. `$HOME/.config/borders/bordersrc`.
3. `$HOME/.bordersrc`, the legacy location.

A relative `XDG_CONFIG_HOME` is ignored. The managed service starts without
arguments, so it follows this lookup order. A configuration file is a shell
script, not a list of key-value pairs. For example, save this as
`~/.config/borders/bordersrc`:

```bash
#!/usr/bin/env bash

options=(
    style=round
    width=3.0
    radius=auto
    position=auto
    hidpi=on
    active_only=on
    order=below
    adaptive_color=off
    active_color=0xcc0088ff
    inactive_color=0x00000000
)

borders "${options[@]}"
```

This example draws a thin blue accent around the focused window. These are
example settings; they are not all built-in defaults.

### Common options

| Option | Built-in default | Behavior |
| --- | --- | --- |
| `style` | `round` | `round`, `square`, `uniform`, or `none`; `none` hides borders without stopping the process. |
| `width` | `4.0` | Positive stroke width in points. Visible width also depends on placement and window stacking. |
| `radius` | `auto` | System corner radius, or an explicit nonnegative number in points. |
| `position` | `auto` | `inside`, `center`, `outside`, or automatic adjustment at display edges. |
| `order` | `below` | Place the border below or above its target window. Inside placement and automatic edge insets can force it above. |
| `hidpi` | `off` | Render at Retina resolution when `on`. |
| `active_only` | `off` | Keep a border only for the focused window when `on`. |
| `adaptive_color` | `off` | `off`, `active`, or `focus`; see below. |
| `active_color` | `0xffe1e3e4` | Focused border color in `0xAARRGGBB` format. |
| `inactive_color` | `0x00000000` | Unfocused border color; has no visible effect with `active_only=on`. |

Use `blacklist="App One,App Two"` to exclude applications, or
`whitelist="App One,App Two"` to restrict borders to the named applications.
Colors also support per-edge values, gradients, and glow. See the
[full option reference](docs/borders.1.scd) for syntax and additional settings.

### Hide small windows

Set global minimum window dimensions in macOS points (independent of Retina
scaling), for example in the `options` array in `bordersrc`:

```bash
min_width=600
min_height=400
```

A window is excluded when **either** width or height is below its threshold.
Equality is allowed. Both options default to `0`, which disables that axis.
Use `borders min_width=600 min_height=400` to update a running instance.
These options cannot be combined with `apply-to`.

When a small window receives focus, Borders retains the previous eligible
window of the same process, or selects its frontmost visible document window.
If none qualifies, the focus border is hidden. Resizing across a threshold
updates visibility even with `active_only=on`. These are size rules, so small
terminals and documents are excluded too; they do not identify popup types.

### Corner radius and custom windows

With `radius=auto`, a positive system radius takes priority, including values
such as `11.5`. If the system returns zero or no usable radius, the fallback is:

| macOS version | Fallback radius |
| --- | --- |
| 27 and newer | 16 points |
| Earlier versions | 9 points |

The 16-point value is based on ordinary windows observed on macOS 27. It is
not a universal measurement of application-drawn corners. Some apps, such as
Wispr Flow, can draw visibly rounded content while reporting a system radius
of zero. The fallback may improve their borders, but cannot recover their
actual visible contour.

Override the radius to tune these windows:

```bash
borders radius=20.5
borders radius=auto
```

A numeric radius overrides system detection for `round` and `uniform` borders;
`radius=0` gives square corners. `style=square` remains square regardless of
radius. With automatic radius selection, `uniform` retains its fixed 9-point
stroke-path radius. Nonuniform corner geometry and unavailable continuous-path
support retain circular-arc rendering.

For a single tracked window, replace `12345` with its window ID:

```bash
borders apply-to=12345 radius=20.5
```

This is a runtime window override, not a persistent application rule. It can
be lost when a border is recreated, including when `active_only=on` removes
an unfocused window's border.

### Runtime changes

While Borders is running, subsequent commands update it immediately:

```bash
borders width=5.0 active_color=0xffe5484d
borders style=none
borders style=round
```

To keep changes across restarts, put them in `bordersrc`. You can apply an
updated configuration by running the script with Bash, for example
`bash ~/.config/borders/bordersrc`.

## Adaptive colors

Adaptive colors are optional and require macOS screen-recording permission.
Fixed colors do not need that permission.

| Mode | Focused border behavior |
| --- | --- |
| `off` | Use `active_color`. |
| `active` | Independently choose black or white for each edge based on nearby pixels. |
| `focus` | Choose one color for the whole border: red (`0xffe5484d`) on bright content, or pale gray (`0xffe1e3e4`) on dark content. |

For example:

```bash
borders adaptive_color=focus active_color=0xffe5484d width=5.0
```

Sampling runs after focus, window creation or unhide, resize, and Space
recovery events. Scrolling and video playback alone do not trigger sampling.
In `focus` mode, color changes require confirmation to limit flicker, and
failed samples retain an established color. Before a usable sample, or after
permission is lost, the border uses the configured `active_color` fallback.
Inactive borders continue to use `inactive_color`.

For the managed service, install the app first, then use:

```bash
make screen-capture-status
make request-screen-capture
# Grant access in System Settings, then:
make service-restart
```

These permission commands launch the installed, stably signed app through
LaunchServices. The service does not request screen-recording permission at
login. `adaptive_color` is process-wide and cannot be combined with `apply-to`.
Accessibility permission is separate: it is optional for core rendering and
can enable Accessibility-based focus resolution through `ax_focus`.

## Update and manage the service

Run these commands from the source checkout:

| Command | Action |
| --- | --- |
| `make update-service` | Rebuild this checkout, install the new version, and restart. |
| `make service-status` | Show the installed version and service status. |
| `make service-stop` | Stop and disable the LaunchAgent, including across logins. |
| `make service-start` | Enable and start the installed service. |
| `make service-restart` | Restart the installed service. |
| `make rollback-service` | Swap compatible `current` and `previous` versions and restart. |
| `make uninstall-service` | Remove the LaunchAgent and managed command/man-page links. |

Rollback requires a compatible previous managed version. A first install or
migration may have no rollback target until the next successful update.
Uninstalling preserves versioned files under `~/.local/opt/jankyborders`.

The installer leaves Homebrew's Cellar untouched. To return to an existing
upstream Homebrew installation:

```bash
make uninstall-service
hash -r
brew services start borders
```

## Troubleshooting

| Symptom | Check |
| --- | --- |
| New options are rejected or changes seem absent | Run `command -v borders`; confirm it points to the intended build. Use `make service-status` for a managed installation. |
| Service repeatedly restarts | Check for another startup entry in Homebrew, yabai, or AeroSpace. Run one primary instance. |
| Only one app has mismatched corners | Its visible content may differ from the reported window shape. Try a runtime `radius` override or exclude it with `blacklist`. |
| Border is blurry on a Retina display | Set `hidpi=on`. |
| Adaptive colors stay at the fallback | Check screen-recording permission for the installed app, then restart it. Sampling is event-driven. |

## Development and reference

```bash
make                   # Release build: bin/borders
make warnings          # Strict compiler checks and service-script syntax check
make test              # Regression tests
make test-sanitize     # AddressSanitizer and UndefinedBehaviorSanitizer
make test-thread       # ThreadSanitizer tests
```

The test suite covers parsing, configuration lookup and execution, window
policy, geometry, focus/Space recovery, and animation/adaptive-color lifecycle.
Passing tests does not establish visual compatibility with every app.

- [Full option reference](docs/borders.1.scd), or `man ./docs/borders.1` from the checkout.
- [Upstream audit](docs/upstream-audit.md): historical decisions and verification evidence; later commits supersede some entries.
- [Fork issues](https://github.com/TYZRPVX/JankyBorders/issues).
- [Upstream project](https://github.com/FelixKratz/JankyBorders).
- [License](LICENSE): GPL-3.0.
