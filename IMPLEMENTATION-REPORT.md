# Recovery and interaction changes

## Lyrics

The old service returned cooldown JSON without scheduling another lookup, and
Popups killed its result-reading timer after that reply. The UI rendered a fixed
countdown estimate. The worker now waits once on a cancellable steady-clock
deadline, publishes loading, and retries the same generation. Server 429/503
deadlines are global to the provider; delays are at least 1/2/4 seconds, and at
most three automatic retries follow the initial lookup. Every lookup retains
the two-search budget and 300 ms request spacing. Genuine not-found results
stay cached and do not retry. Configuration/malformed errors do not retry.

Results notify the popup by a posted window message instead of polling. The
controller stays alive; GSMTC/playback controls are untouched. Closing,
replacement, and shutdown invalidate the generation and interrupt the deadline.
Exhaustion offers Retry; even manual retries respect a remaining server cooldown.

## Calendar

The primary bug was ordering: render measured the popup before tick populated
the clock/date header. A browser probe of baseline commit
`3da2172fcda36c14d7923dd7be1372671672ea69` emitted `popup:size:320 235 1`,
then produced 271 DIP of content after the header grew to 36 DIP. Applying the
requested native viewport left the last row at y=261 below its 235-DIP boundary.
There was no subsequent measurement. Presentation also acknowledged two frames
without checking whether native bounds had arrived, and native DIP conversion
rounded to nearest physical pixels. Font/initial viewport changes were likewise
unobserved.

The calendar now fills its header before the first measurement and remeasures
on ResizeObserver, viewport resize, and font readiness,
uses intrinsic scroll height plus two DIP of contour clearance, and deduplicates
size messages per revision. The initial acknowledgement carries actual viewport
dimensions; native code rejects stale bounds and waits for a matching resize
acknowledgement. DIP dimensions round upward; WebView continues receiving the
actual native client rectangle. Existing work-area clamping remains in use;
constrained calendar content can scroll. Five/six-week layouts use 35 DIP row
pitch, a compact header, English month/day names, locale-derived week starts,
Windows time patterns, quiet today underline and a separate selected ring.

`diagnostics/interaction-tests.cjs` reproduces opening from a 200x36 tooltip
viewport, consumes measurement messages, checks the final row, switches between
five/six weeks, tests constrained height and reopen, and saves calendar-five-*
and calendar-six-* PNGs at four DPI settings. These are headless browser captures;
native Composition blur/monitor transitions still require desktop inspection.

## Reordering and menu

Pointer capture and a six-DIP threshold distinguish clicking from dragging.
CSS order displaces neighbors without detaching the captured node; DOM order
commits on release. Cancellation restores the original sequence. Native capture
loss, Escape, blur, pointer cancellation, and inventory replacement cancel safely.
Successful drags suppress activation. Magnification is suppressed while dragging.
Alt+Shift+ArrowLeft/Right provides keyboard reordering. Native code validates all
submitted current HWNDs and stores only AUMID/executable identities in HKCU under
Software/GlassSplit/AppOrder (REG_MULTI_SZ), bounded to 256 entries and 64 KiB.
Unknown apps append in inventory order; saved closed apps are retained within
the bound. Reset deletes that value. Native tests use a separate temporary key.

The custom English menu reuses the single composition popup, GPU backdrop and
native outline. Opening and dismissal use restrained fades; the closing surface
stops accepting pointer input immediately and honors reduced-motion settings.
It replaces TrackPopupMenu, supports arrows/Home/End/Enter,
Escape/outside clicks and an expandable Visibility section with Right/Left
navigation. Autostart is inspected again before changes; visibility still uses
the existing persisted policy. Popup revision cancellation prevents old content
acknowledgements from presenting a replaced surface.

## Tray feasibility: blocked

The read-only `diagnostics/tray-feasibility.ps1` probe on Windows 11 build 26300
found Shell_TrayWnd present but already hidden. Its UI Automation subtree
contained **zero descendants**, zero SystemTray elements, and zero invocation
providers. The probe did not change Explorer visibility or invoke applications.

[Microsoft's notification-area explanation](https://devblogs.microsoft.com/oldnewthing/20250929-00/?p=111637/)
states there is no supported notification-icon object model and hidden overflow
icons cannot be enumerated through UI Automation without showing their flyout.
[Shell_NotifyIcon](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyicona)
adds/updates/deletes icons registered by a caller; it does not enumerate others
or disclose their callback identities or image handles.

Consequently there is no demonstrated reliable data source for complete icons,
tooltips, live updates, and both application-owned click actions while Explorer
is hidden. No tray implementation, placeholder icons, process substitutes,
injection, memory scraping, shell patches or scanning loop was added. Supported
tray mirroring capabilities in this release: **none**. The practical limited
alternative is to retain/reveal Explorer's notification area and use its real
icons and overflow UI; a UIA experiment could address only visible providers
and their supported actions. That alternative is not implemented here and is
not claimed as complete mirroring. The visible-taskbar variant was not tested
because testing must not manipulate the real taskbar.

## Verification and remaining desktop checks

Run Release x64 with /W4 /WX, diagnostics/run-ci-tests.ps1, npm test,
diagnostics/verify-ui.cjs, and diagnostics/package-portable.ps1. Native and browser
regressions cover recovery, cancellation, matching/search budgets, isolated order
persistence, calendar geometry, gesture handling, menu and popup exclusivity.
Lyrics timing assertions use an explicitly advanced browser clock, so a slow
hosted runner cannot advance playback between the first-line assertion and its
presentation acknowledgement. Calendar first-opening checks wait for fonts and
actual layout settlement and log geometry if a fit check fails.
Mock media/audio tests do not demonstrate actual tray functionality.

Before using the release as a desktop replacement, manually inspect first native
calendar presentation and rapid month/open/close interactions on mixed-DPI and
small monitors; Composition contour/blur; drag crossing native window boundaries
and native capture loss; menu placement across monitors, focus and outside-click
dismissal; real offline/429 recovery with ongoing media; and restored app order
after application and Dock restarts. No production Dock was launched, real
playback changed, taskbar visibility changed, or startup setting written by tests.

## Local verification results (2026-10-21)

- Release x64 rebuild, /W4 /WX: passed, zero warnings and errors. Pinned
  dependencies were already restored; local NuGet CLI was unavailable, so MSBuild
  was invoked directly through diagnostics/vs-env.cmd. CI retains its normal restore.
- All seven native suites and performance sampler fixtures: passed.
- Full npm browser suite: passed (calendar, interaction, Lyrics, magnification,
  media readiness, mixer polish, performance, popups, refinements).
- Targeted reruns after review: Lyrics recovery/deadlines/notifications passed;
  interaction (including native capture cancellation), deployed-UI checks, and ZIP verification passed.
- Read-only Windows tray probe: taskbar hidden, zero UIA descendants, unsupported.
- Calendar screenshots inspected: diagnostics/calendar-five-* and calendar-six-*
  at 1, 1.25, 1.5 and 2 device scale. Native backdrop is outside headless proof.

## Changed files

- `Dock.vcxproj`
- `IMPLEMENTATION-REPORT.md`
- `README.md`
- `diagnostics/app-order-fixtures.h`
- `diagnostics/interaction-tests.cjs`
- `diagnostics/lyrics-recovery-fixtures.h`
- `diagnostics/lyrics-tests.cpp`
- `diagnostics/lyrics-ui-tests.cjs`
- `diagnostics/native-tests.cpp`
- `diagnostics/refinement-tests.cjs`
- `diagnostics/tray-feasibility.ps1`
- `src/app/Application.cpp`
- `src/app/Application.h`
- `src/app/ApplicationMenu.cpp`
- `src/app/ApplicationVisibility.cpp`
- `src/app/PopupDismiss.cpp`
- `src/app/PopupLyrics.cpp`
- `src/app/PopupMenu.cpp`
- `src/app/Popups.cpp`
- `src/app/Popups.h`
- `src/media/LyricsService.cpp`
- `src/media/LyricsService.h`
- `src/web/WebViewHost.cpp`
- `src/web/WebViewHost.h`
- `src/windows/AppOrder.h`
- `ui/app.js`
- `ui/calendar.css`
- `ui/calendar.js`
- `ui/dock.js`
- `ui/index.html`
- `ui/lyrics.js`
- `ui/magnification.js`
- `ui/menu.css`
- `ui/menu.js`
- `ui/popup.js`
- `ui/reorder.js`
- `ui/theme.css`
