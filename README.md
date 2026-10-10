# AmnesiaTDD.FPSFix

A high-frame-rate fix for the Windows version of *Amnesia: The Dark Descent*.
It lets the game render above 60 FPS while keeping physics, scripts, AI, saves,
and other gameplay systems on the engine's original 60 Hz update loop.

## Features

- Smooth camera movement, raw mouse look, and menu/inventory cursors
- Interpolated first-person hands, held items, and lantern motion
- Smooth dynamic props, doors, drawers, cloth, enemies, and ragdolls
- Interpolated particles and frame-rate-correct image-trail effects
- Automatic compatibility checks that fail safely before modifying the game
- Per-feature configuration and lightweight diagnostic logging

Release checks passed at 60, 120, 144, and 240 FPS, with an extended playthrough
at approximately 350 FPS. The simulation rate is never increased, so high
frame rates do not speed up the game.

## Requirements

- A 32-bit Windows copy of *Amnesia: The Dark Descent*
- A compatible x86 ASI loader

The validated release target is the GOG Windows build:

| | Value |
| --- | --- |
| GOG build ID | `53742270235201755` |
| `Amnesia.exe` SHA-256 | `E75EE41394B2D20B0F96B74713C40F6C38BAC88E98D0E23383036F22EA1688D0` |

Other storefronts and revisions may work. On startup, the plugin verifies all
required engine signatures and imports instead of relying on the executable's
hash or install path. If the mapping is incompatible or ambiguous, it installs
no hooks and records the reason in the log.

## Installation

Download the latest archive from
[Releases](https://github.com/slopkhayzo/Amnesia-TDD-FPSFix/releases/latest).

### Bundle with Ultimate ASI Loader

1. Open the archive ending in `with-Ultimate-ASI-Loader.zip`.
2. Copy its contents next to `Amnesia.exe`.
3. Start the game normally.

Do not overwrite an existing `wininet.dll`. If one is already present, another
mod may own it; use the plugin-only package with that mod's compatible x86 ASI
loader instead.

### Plugin-only package

1. Install a compatible x86 ASI loader for the game.
2. Copy `AmnesiaTDD.FPSFix.asi` and `AmnesiaTDD.FPSFix.ini` together into a
   directory scanned by that loader.
3. Start the game normally.

The plugin is loader-agnostic and always looks for its INI beside the ASI.

## Usage

The supplied configuration enables all validated fixes and is suitable for
normal play. Keep VSync enabled or set an external frame-rate cap: the render
unlock can otherwise run as fast as the machine allows, and the internal
`PresentationLimiter`/`TargetFPS` option is not implemented in 1.0.0.

Settings are documented in
[`config/AmnesiaTDD.FPSFix.ini`](config/AmnesiaTDD.FPSFix.ini). Most users
should leave them unchanged.

Diagnostics are written to `AmnesiaTDD.FPSFix.log` beside the plugin. If that
directory is not writable, the log is placed in:

```text
%LOCALAPPDATA%\AmnesiaTDD.FPSFix\AmnesiaTDD.FPSFix.log
```

When reporting a problem, include that log and note the game storefront,
executable version, ASI loader, display refresh rate, and frame cap/VSync
settings. The optional `MotionTrace` capture is intentionally disabled by
default because it produces a much heavier per-frame diagnostic.

## Uninstallation

Remove `AmnesiaTDD.FPSFix.asi` and `AmnesiaTDD.FPSFix.ini`.

If you installed the bundled `wininet.dll`, remove it only if no other ASI mod
uses that loader. Logs can be removed separately.

## Building from source

Requirements:

- Visual Studio with the MSVC x86 C++ toolchain
- CMake 3.24 or newer
- Ninja (for the commands below)

From an x86 Visual Studio developer environment:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

The plugin and default INI are written to `build/bin`. To create the
plugin-only release archive:

```powershell
cmake --build build --target package_plugin_only
```

If the pinned Ultimate ASI Loader research binary is available, the convenience
bundle can also be built with `package_ultimate_asi_loader`. Packages are
written under `dist`.

## How it works

HPL2 already runs gameplay on a fixed 60 Hz clock separately from rendering.
The plugin removes the engine's 60 FPS presentation gate, captures completed
simulation states, and temporarily interpolates supported render state between
those snapshots. Authoritative game state is restored immediately after each
scene render.

For the binary-analysis record, architecture, validation history, and design
decisions, see the
[high-frame-rate research plan](research/docs/amnesia-tdd-high-frame-rate-plan.md).

## License

AmnesiaTDD.FPSFix is released under the [MIT License](LICENSE). It statically
links [MinHook](third_party/minhook/LICENSE.txt), which is also distributed
under the 2-clause BSD license.
