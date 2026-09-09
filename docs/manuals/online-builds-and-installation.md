# Online builds and installation

CW Buddy replaces earlier CW Assistant installations. The Windows installer
keeps the existing upgrade identity, the Debian package declares the former
package as replaced, and the application imports existing profiles and the
managed callsign database on first launch. The macOS bundle identifier remains
unchanged intentionally so the operating system recognizes the renamed app as
the same product.

## Build entirely on GitHub

Every push and pull request starts the desktop workflow on GitHub-hosted
runners. It installs the pinned Qt modules, including native multimedia device
discovery, configures with CMake, builds the
desktop application and core tests, runs the tests, stages runtime dependencies,
and uploads one artifact for each platform:

- Windows 11 x64
- Ubuntu Linux x64
- macOS Sonoma 14 or newer, Apple silicon
- macOS Sonoma 14 or newer, Intel x64

Hosted packages include the CPU-only ONNX Runtime needed by the optional local
character-refinement decoder. No decoding model or model metadata is bundled or
downloaded; the Decoder settings page accepts compatible operator-supplied
files stored on the local machine. The application explicitly disables runtime
telemetry. Packages include the runtime's MIT license, privacy notice, and
third-party notices.

For the simplest download, open the repository's root
[`binaries/`](../../binaries/README.md) index. It points to stable asset names in
the **Continuous development builds** prerelease and includes a machine-readable
manifest and SHA-256 checksum link. The prerelease is updated only after the
complete platform matrix passes. Its `continuous` Git tag is force-moved to
that fully verified commit only after the release assets and checksums have
also been published; a failed run leaves the prior known-good downloads and
marker untouched. When replacing an existing prerelease, binaries and
`SHA256SUMS` are uploaded first and `latest.json` last, leaving the previous
manifest reachable until the replacement set is complete. The in-app updater
also retries transient publication/network failures three times. Release asset
replacement itself uses bounded backoff because GitHub can briefly retain an
old filename after accepting its deletion; an exhausted retry sequence leaves
the verified tag unchanged and reports a publication failure.

For private-repository automation that can push through Git SSH but cannot read
the Actions API, each matrix leg publishes a temporary annotated
`ci-status/<platform>-<commit>` tag containing the outcome of Qt installation,
configure, build, test, staging, archive, upload, and package steps. A release
failure publishes the same kind of marker. Successful release publication
removes those diagnostic tags and advances `continuous`.
Each diagnostic tag push uses bounded retry so a transient Git transport error
after successful platform validation does not immediately invalidate that
matrix leg; exhausting the retries still fails the job and blocks publication.
When Qt installation or compilation fails, the status annotation also carries
bounded compiler/linker error lines plus a shorter trailing log excerpt, so
interleaved output from a later successful parallel target cannot hide the
actual failure from authorized Git-only automation.
Qt SDK downloads are retried once from a clean uncached directory with the
runner's external 7-Zip binary when the hosted Python extractor fails.

The Windows leg temporarily installs the SDK downloader from immutable upstream
commit `8c3695d4a4e1ceabf6a74dc6c79681656dc6b74b`. That commit contains the Qt
6.11 Windows repository-layout correction missing from the current downloader
release. This is a build-tool pin only: the application still bundles the
explicit Qt 6.11.2 runtime, and MSI application upgrades continue to use the
product's stable upgrade identity and monotonically increasing package
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
cw-buddy-windows11-x64.msi
```

Verify its SHA-256 checksum, double-click it, and follow Windows Installer to
select the installation folder. It installs the self-contained Qt application,
creates **Start → All → CW Buddy → CW Buddy** as a dedicated program
group plus a **CW Buddy** desktop shortcut, registers the application in
**Settings → Apps → Installed apps**, and provides normal uninstall/repair
behavior. The executable, shortcuts, and installed-app entry use the CW
Morse-key application icon. Its rounded corners use native transparency, so the
surrounding desktop or launcher color remains visible.

Every hosted package has a monotonically increasing numeric package revision
and a stable Windows Installer upgrade identity. Running a newer MSI performs a
major upgrade of the existing installation; station profiles remain in the
user’s application settings and are not removed with program files. Downgrades
are rejected by Windows Installer.

The successful finish page includes **Launch CW Buddy**. It is unchecked
for a clean install and selected by default for an interactive upgrade, so the
updated application normally reopens when setup finishes. You can clear the
option before selecting **Finish**. During an upgrade, Windows Installer asks a
running CW Buddy instance to close gracefully and waits up to 15 seconds
before terminating a stuck instance. The launch default applies to every
interactive upgrade because process closure occurs separately in Windows
Installer's elevated execute session; its result is not reused by the
finish-page session.
The hosted Windows job also inspects the generated MSI tables through
PowerShell 7-compatible access to the Windows Installer automation API and
retains a failed assertion in its CI status record. It verifies that process
closure runs only in the execute sequence, along with the bounded shutdown,
conditional launch default, and launch target, so these packaging regressions
block publication with a diagnosable result.

The installer presents the canonical GPL-3.0-or-later text. The build copies
that UTF-8 source into the `.txt` input format required by CPack's WiX generator;
the repository license remains the single source of truth.

The MSI also contains the receive-only SoapySDR runtime, RTL-SDR module,
librtlsdr, libusb, and required Windows runtime DLLs. No separate SDR program
is needed. The build loads the module and verifies its registered RTL-SDR
factory before constructing the MSI, then inspects the MSI file table for the
runtime and license records. The normal RTL-SDR Windows USB driver must still
be installed for the dongle itself.

Development installers are currently unsigned, so Windows may identify the
publisher as unknown. Verify `SHA256SUMS` before continuing. Automatic in-app
download/install is intentionally not enabled until the MSI, update manifest,
and channel metadata are cryptographically signed. For now, update by
downloading and running the newer MSI from the same continuous release page.

### Build version identity

Every artifact from one workflow uses the same `major.minor.revision` value.
For continuous builds, `revision` is the GitHub Actions run number. The value in
**Settings → About** therefore matches the Windows executable and MSI, macOS
bundle, Debian package, installed `share/doc/cw-buddy/VERSION` file, and the
`version` field injected into `latest.json`. A default local configuration uses
revision `0`.

On macOS, the self-contained application stores the same machine-readable
record at `CW Buddy.app/Contents/Resources/VERSION`; Windows and Linux use
`share/doc/cw-buddy/VERSION` inside the staged installation. The hosted
build checks these records and the native executable/bundle metadata before it
publishes any package.

## macOS Sonoma and newer

Both macOS archives contain a self-contained `.app` bundle with the native
application icon and microphone usage declaration, compiled with
deployment target 14.0. Choose the Apple silicon archive for M-series Macs and
the Intel x64 archive for supported Intel Macs. Extract the archive and move
`cw-buddy-desktop.app` to `/Applications` if desired.

The hosted matrix inspects the staged executable's Mach-O build metadata and
rejects an artifact unless its minimum macOS version is exactly 14.0. It also
checks the bundle identity, executable, icon, and version metadata and performs
a strict recursive verification of the final resource seal after every bundled
file has been installed.

Builds are currently unsigned and not notarized; signing and notarization
remain release gates. After verifying the archive against `SHA256SUMS`, launch
the extracted app once. If Gatekeeper blocks this known development build, open
**System Settings → Privacy & Security**, locate the blocked-app message, click
**Open Anyway**, then confirm **Open**. This exception is specific to that app;
do not disable Gatekeeper globally. A "damaged" error is not an expected
unsigned-build warning: download the current archive again and report the
release version if it persists.

The application bundle includes SoapySDR, SoapyRTLSDR, librtlsdr, and libusb.
Packaging treats the runtime-loaded RTL module as a Mach-O deployment root,
rejects unresolved Homebrew paths, verifies the final deep signature, and runs
the real backend/module smoke test without requiring attached hardware.

## Debian and Ubuntu

Only the Linux matrix job runs the Debian packaging stage. It produces a
`cw-buddy_<version>-<revision>_amd64.deb`
package. Download it from the `cw-buddy-debian-ubuntu-x64` workflow
artifact, extract the artifact archive if necessary, then install with APT so
dependency errors are reported clearly:

```sh
sudo apt install ./cw-buddy_0.1.245-1_amd64.deb
```

The stable continuous-release filename is
`cw-buddy-debian-ubuntu-x64.deb`, so a release download can instead be
installed with:

```sh
sudo apt install ./cw-buddy-debian-ubuntu-x64.deb
```

Launch from the desktop application menu or run:

```sh
cw-buddy-desktop
```

The package includes the application, deployed Qt/QML runtime components,
desktop entry, 512 px application icon, license, and user manuals. The package is built on
Ubuntu 24.04, so runtime validation on supported Debian and Ubuntu releases is a
release gate. A signed APT repository is planned; until it exists, installing a
downloaded `.deb` is not the same as subscribing to an APT repository.

APT resolves the package's RTL-SDR Soapy module dependency. The portable Linux
archive instead includes the SDR-specific SoapySDR/RTL-SDR ELF closure and
launches through a package-relative wrapper, so it does not depend on a
separately running SDR application. Keep the archive layout intact when moving
or extracting it; invoking its launcher through a symbolic link is supported.
The archive includes the USB-side libudev/libcap dependency closure and its
license records. It remains an x86-64 Linux build against the Ubuntu 24.04
system ABI, and it cannot bundle kernel drivers, device-access rules, or USB
permissions.
Both package forms execute a backend/module smoke test before publication; that
test proves the driver factory loads, not that a physical dongle is accessible.
If an SDR runtime assembly step fails, the release is withheld and the bounded
package-step diagnostic is retained with that platform's CI status record. The
hosted Linux dependency step retries transient repository downloads a bounded
number of times and uses only the runner's signed Ubuntu source definition;
unrelated preinstalled third-party repositories are excluded from this package
build. The release is still withheld if installation cannot finish.
The Windows dependency build pins both upstream revisions and their accepted CMake
policy compatibility floor. Configure-stage CMake failures use the same bounded
status diagnostic path, and the application configure resolves SoapySDR from
the verified platform-specific CMake directory in the prepared runtime root
rather than a build-machine package registry.
Before package staging, the Windows UI smoke resolves its linked SDR DLLs from
that same prepared root. The later staged smoke then proves the installer layout
is self-contained rather than inheriting this build-only path.

SDRplay is intentionally different: install a compatible SDRplay API/service
and SoapySDRPlay3 module from their respective providers. CW Buddy neither
downloads nor redistributes the proprietary vendor runtime. Its packaged RTL
module path is added without hiding compatible modules in the normal system or
operator-provided SoapySDR search paths.

Because the hosted package deploys its pinned Qt runtime, available Qt SDK
license texts are installed under
`/usr/share/doc/cw-buddy/third-party/qt6/`. The project dependency policy is
installed as `/usr/share/doc/cw-buddy/licensing.md`.

## Local developer build (optional)

Local compilation remains useful for contributors but is not required merely
to obtain a test build:

```sh
cmake -S . -B build -DCWA_BUILD_DESKTOP=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```
