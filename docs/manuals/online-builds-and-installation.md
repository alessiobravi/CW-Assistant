# Online builds and installation

## Build entirely on GitHub

Every push and pull request starts the desktop workflow on GitHub-hosted
runners. It installs the pinned Qt modules, configures with CMake, builds the
desktop application and core tests, runs the tests, stages runtime dependencies,
and uploads one artifact for each platform:

- Windows 11 x64
- Ubuntu Linux x64
- macOS ARM64
- macOS x64

Open the repository's **Actions** page, select a successful
**Cross-platform Desktop CI** run, and download the artifact for the required
platform. Development artifacts expire after 14 days. They are unsigned and
intended for testing until the release/signing workflow is complete.

## Debian and Ubuntu

Only the Linux matrix job runs the Debian packaging stage. It produces a
`cw-assistant_<version>-<revision>_amd64.deb`
package. Download it from the `cw-assistant-debian-ubuntu-x64` workflow
artifact, extract the artifact archive if necessary, then install with APT so
dependency errors are reported clearly:

```sh
sudo apt install ./cw-assistant_0.1.0-1_amd64.deb
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
