# Glass Split

Glass Split is my Windows taskbar replacement, written in C++ with a WebView2
interface. It splits the desktop controls into three glass islands: system
controls on the right, applications in the center, and media on the left.

## Features

- An applications dock with hover magnification and window switching.
- A volume mixer, clock, and calendar.
- Now Playing information and synchronized lyrics from LRCLIB.
- Always Visible, Auto Hide, and Smart Hide modes.
- Optional startup with Windows, available from the context menu.

## Download

Download `GlassSplit-Windows-x64.zip` from
[GitHub Releases](https://github.com/auxilialary/minimal-dock/releases).
The rolling **Development build** prerelease contains the latest successful
build from `main`.

Extract the ZIP and run `GlassSplit\Dock.exe`. Keep the DLLs and `ui` folder
beside the executable.

The [WebView2 Evergreen Runtime](https://developer.microsoft.com/en-us/microsoft-edge/webview2/)
must be installed. The portable ZIP includes the Microsoft C++ runtime DLLs.

## Build

Install Visual Studio 2026 or its Build Tools with Desktop development with C++,
MSVC v145, and a Windows 10/11 SDK. Install the NuGet CLI and put it on `PATH`.

From a command prompt in the repository:

```bat
build.cmd
```

The script locates Visual Studio, restores the pinned packages from
`packages.config` into `packages`, and builds Release x64 with `/W4 /WX`.
The output is in `x64\Release`, including the UI assets.

To build in Visual Studio instead, restore the dependencies first:

```bat
nuget restore packages.config -PackagesDirectory packages -NonInteractive
```

Then open `Dock.slnx` and select Release x64.

## Notes

Windows 11 is the intended desktop environment. The project is still in
development, and the menus include some Russian labels.

Lyrics depend on LRCLIB coverage and network access. Media information depends
on what the player exposes through Windows media sessions.

Glass Split hides the Windows taskbar while running. Right-click the system
island and choose `Закрыть Dock` to exit and restore it. A watchdog also attempts
to restore the taskbar if the main process fails.
