# Builds and releases

The Windows workflow uses the Visual Studio 2026 runner and requires MSVC v145.
It restores the pinned NuGet packages, builds Release x64 with /W4 /WX, runs
seven native suites and the synthetic performance sampler, then runs the
headless Edge suites. It never starts the production Dock executable.

Pull requests receive read-only permissions and build artifacts. Only the
publication job on pushes to main or version tags receives contents: write.

## Development builds

A successful main build updates the single Development build prerelease and
its GlassSplit-Windows-x64.zip asset. Publication is serialized, and a build
whose commit is no longer the main head is skipped. Only the disposable
`dev-latest` tag can be force-updated. The main branch and version tags are
never moved by the workflow. GITHUB_TOKEN tag updates do not trigger another
workflow run.

## Versioned releases

Choose an unused version after reviewing the changes, then run:

```sh
git tag v1.2.3
git push origin v1.2.3
```

Replace v1.2.3 with the intended version. Tags must use vMAJOR.MINOR.PATCH with
numeric components. The workflow builds the tagged source, tests it, and
publishes the portable ZIP with GitHub-generated notes from the actual changes.
An existing versioned release is never overwritten; a rerun after successful
publication fails rather than replacing its assets. No personal token is needed.

## Portable package

`diagnostics/package-portable.ps1` inspects the built executable and loader
imports, includes the required app-local v145 CRT DLLs, and copies the tracked
UI assets from the build output after comparing their hashes with the source.
The ZIP has one GlassSplit directory containing Dock.exe, WebView2Loader.dll,
the required CRT DLLs, ui/, and version.json with the full source commit SHA.
Every archive file is checked against the staging inventory and SHA-256 hash.
WebView2 Evergreen Runtime remains a prerequisite on the destination computer.

For a local package after building:

```powershell
./diagnostics/package-portable.ps1
```

Build and package output is ignored by Git. Keep diagnostics and browser profiles
out of the distribution. The workflow artifact is available even if publication
fails, so release failures can be investigated separately from build failures.
