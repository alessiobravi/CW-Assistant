# Online builds and installation

## Build entirely on GitHub

Every push and pull request starts the desktop workflow on GitHub-hosted
runners. It installs the pinned Qt modules, configures with CMake, builds the
desktop application and core tests, runs the tests, stages runtime dependencies,
and uploads one artifact for each platform:

- Windows 11 x64
- Ubuntu Linux x64
- macOS Sonoma 14 or newer, Apple silicon
- macOS Sonoma 14 or newer, Intel x64

For the simplest download, open the repository's root
[`binaries/`](../../binaries/README.md) index. It points to stable asset names in
the **Continuous development builds** prerelease and includes a machine-readable
manifest and SHA-256 checksum link. The prerelease is updated only after the
complete platform matrix passes. Its `continuous` Git tag is force-moved to
that fully verified commit only after the release assets and checksums have
also been published; a failed run leaves the prior known-good downloads and
marker untouched.

For private-repository automation that can push through Git SSH but cannot read
the Actions API, each matrix leg publishes a temporary annotated
`ci-status/<platform>-<commit>` tag containing the outcome of Qt installation,
configure, build, test, staging, archive, upload, and package steps. A release
failure publishes the same kind of marker. Successful release publication
removes those diagnostic tags and advances `continuous`.
When Qt installation fails, the status annotation also carries a bounded tail
of the installer log so authorized Git-only automation can diagnose the cause.
Qt SDK downloads are retried once from a clean uncached directory with the
runner's external 7-Zip binary when the hosted Python extractor fails.

The Windows leg temporarily installs the SDK downloader from immutable upstream
commit `8c3695d4a4e1ceabf6a74dc6c79681656dc6b74b`. That commit contains the Qt
6.11 Windows repository-layout correction missing from the current downloader
release. This is a build-tool pin only: the application still bundles the
explicit Qt 6.11.2 runtime, and MSI application upgrades continue to use CW
Assistant's stable upgrade identity and monotonically increasing package
revision. Replace the source pin with a released downloader only after a hosted
Windows build proves that the release contains the same correction.

The same files remain available as short-lived workflow artifacts: open the
repository's **Actions** page, select a successful **Cross-platform Desktop CI**
run, and scroll to **Artifacts**. Workflow artifacts expire after 14 days;
continuous-release assets remain available until superseded. Both are unsigned
and intended for testing until the signing workflow is complete.

The push workflow is the superset verification path: it builds the desktop and
core tests on every supported architecture. The lighter core-only workflow is
retained for pull requests and manual diagnostics instead of duplicating every
push run.

Before packaging, each platform also exercises the empty spectrum/waterfall
render path directly and loads the complete QML desktop shell in an offscreen
software-rendering smoke test. These checks guard first launch when no receiver
data exists. The staged package must then launch and exit cleanly with each
runner's native graphics path before it is archived or uploaded. Clean-machine
testing on representative graphics hardware remains a release gate. The native
smoke run injects a deterministic spectrum row so it covers texture creation as
well as the empty first frame.

## Windows 11 x64

The Windows download is an MSI installer, not a compressed archive:

```text
cw-assistant-windows11-x64.msi
```

Verify its SHA-256 checksum, double-click it, and follow Windows Installer to
select the installation folder. It installs the self-contained Qt application,
creates Start-menu and desktop shortcuts, registers the application in
**Settings → Apps → Installed apps**, and provides normal uninstall/repair
behavior.

Every hosted package has a monotonically increasing numeric package revision
and a stable Windows Installer upgrade identity. Running a newer MSI performs a
major upgrade of the existing installation; station profiles remain in the
user’s application settings and are not removed with program files. Downgrades
are rejected by Windows Installer.

The installer presents the canonical GPL-3.0-or-later text. The build copies
that UTF-8 source into the `.txt` input format required by CPack's WiX generator;
the repository license remains the single source of truth.

Development installers are currently unsigned, so Windows may identify the
publisher as unknown. Verify `SHA256SUMS` before continuing. Automatic in-app
download/install is intentionally not enabled until the MSI, update manifest,
and channel metadata are cryptographically signed. For now, update by
downloading and running the newer MSI from the same continuous release page.

## macOS Sonoma and newer

Both macOS archives contain a self-contained `.app` bundle compiled with
deployment target 14.0. Choose the Apple silicon archive for M-series Macs and
the Intel x64 archive for supported Intel Macs. Extract the archive and move
`cw-assistant-desktop.app` to `/Applications` if desired.

The hosted matrix inspects the staged executable's Mach-O build metadata and
rejects an artifact unless its minimum macOS version is exactly 14.0. Builds are
currently unsigned and not notarized; signing and notarization remain release
gates.

## Debian and Ubuntu

Only the Linux matrix job runs the Debian packaging stage. It produces a
`cw-assistant_<version>-<revision>_amd64.deb`
package. Download it from the `cw-assistant-debian-ubuntu-x64` workflow
artifact, extract the artifact archive if necessary, then install with APT so
dependency errors are reported clearly:

```sh
sudo apt install ./cw-assistant_0.1.0-1_amd64.deb
```

The stable continuous-release filename is
`cw-assistant-debian-ubuntu-x64.deb`, so a release download can instead be
installed with:

```sh
sudo apt install ./cw-assistant-debian-ubuntu-x64.deb
```

Launch from the desktop application menu or run:

```sh
cw-assistant-desktop
```

The package includes the application, deployed Qt/QML runtime components,
desktop entry, scalable icon, license, and user manuals. The package is built on
Ubuntu 24.04, so runtime validation on supported Debian and Ubuntu releases is a
release gate. A signed APT repository is planned; until it exists, installing a
downloaded `.deb` is not the same as subscribing to an APT repository.

Because the hosted package deploys its pinned Qt runtime, available Qt SDK
license texts are installed under
`/usr/share/doc/cw-assistant/third-party/qt6/`. The project dependency policy is
installed as `/usr/share/doc/cw-assistant/licensing.md`.

## Local developer build (optional)

Local compilation remains useful for contributors but is not required merely
to obtain a test build:

```sh
cmake -S . -B build -DCWA_BUILD_DESKTOP=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```
