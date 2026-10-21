# Glass Split

Glass Split is a Windows taskbar replacement written in C++ with a WebView2
interface. It uses three independent glass islands for system controls,
applications, and media. The project is still under development.

## Features

- Centered applications dock with hover magnification and window activation.
- GSMTC Now Playing controls and synchronized lyrics from LRCLIB.
- Application volume mixer and calendar.
- Always Visible, Auto Hide, and Smart Hide modes.
- Optional Windows autostart from the context menu.
- GPU glass blur, a watchdog, and taskbar restoration on exit or failure.

## Requirements

- Windows 11 is the intended desktop environment. The build targets the Windows
  10 SDK family; other Windows versions have not been verified.
- Microsoft Edge WebView2 Evergreen Runtime.
- For building: Visual Studio 2026 or Build Tools with Desktop development with
  C++, MSVC v145, and a Windows 10/11 SDK. The project selects SDK version 10.0,
  using an installed compatible SDK.
- NuGet CLI on PATH for the build script.

The native dependencies are pinned in `packages.config`: WebView2 SDK
1.0.4258.31 and Windows Implementation Library 1.0.260126.7.

## Building

From a command prompt in the project directory:

```bat
build.cmd
```

The script finds Visual Studio, restores NuGet packages, and rebuilds Release
x64 with warning level 4 and warnings treated as errors. Output goes to
`x64\Release`. You can also open `Dock.slnx` in Visual Studio after restoring
`packages.config` into the `packages` directory.

Source builds need the matching `ui` assets alongside `Dock.exe`. The project
copies them into an adjacent `ui` directory after building. Keep that directory
with the executable when moving a build.

## Running and exiting

Run `x64\Release\Dock.exe`. The program hides the Windows taskbar while active.
Only one instance runs per session.

Right-click the system island for visibility settings, the optional
`Start with Windows` setting, and the exit item (`Закрыть Dock`). Exit through
that menu to restore the taskbar. A watchdog also attempts restoration if the
main process fails. Autostart is opt-in and follows the executable's location.

## Tests

Native regression runners are in `diagnostics`. Run them from the project root:

```bat
diagnostics\run-native-tests.cmd
diagnostics\run-media-tests.cmd
diagnostics\run-lyrics-tests.cmd
diagnostics\run-popup-audio-tests.cmd
diagnostics\run-performance-tests.cmd
diagnostics\run-autostart-tests.cmd
diagnostics\run-visibility-tests.cmd
```

Browser tests require Node.js, npm, and Microsoft Edge:

```bat
npm ci
npm test
```

These suites exercise source fixtures and headless UI pages without launching
the production dock. Connectivity probes and benchmark scripts are separate
manual tools; benchmarks can launch a diagnostics instance.

## Notes

Media controls depend on the player publishing GSMTC metadata. Some players
report the uploader as the artist. Lyrics search first uses the inferred artist
and title, then falls back to the normalized title alone when needed. Matching
uses title, duration, and available artist evidence, and leaves ambiguous
recordings unselected. Each lookup makes at most two requests.

Lyrics availability depends on LRCLIB coverage and network availability. Rate
limits and service outages are reported separately from missing lyrics. Cached
results and server cooldowns can delay another lookup. There is no background
lyrics polling.

The interface includes Russian menu labels. Desktop behavior can vary with
Windows versions, multiple monitors, DPI settings, and media applications.
This repository contains source and tests, not a packaged binary distribution.
