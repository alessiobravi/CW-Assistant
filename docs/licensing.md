# Licensing policy

CW Buddy is distributed under `GPL-3.0-or-later`. Source files are covered
by that license unless a file clearly states a different compatible license.
The repository's `LICENSE` file holds the GNU General Public License version 3
text. Every package presents that same file as its installer license and
installs it as the package copyright record, with this policy file beside it.

Contributions must be legally redistributable under `GPL-3.0-or-later` and must
not copy proprietary source, artwork, documentation, interface assets, or other
protected expression. Functional interoperability research must result in an
independent implementation and neutral project terminology.

Before adding or updating a dependency, record its source, version, license,
linking form, redistribution requirements, and platform packaging impact. A
dependency must be compatible with the project license and distributable on all
platforms where it is bundled. Optional vendor runtimes and drivers that cannot
be redistributed stay external, are detected at runtime, and receive operator
installation guidance.

Qt 6.5 or newer is the desktop application's only required third-party
dependency; the core library links nothing beyond the C++ standard library. The
application uses the Qt Core, Gui, Multimedia, Network, Qml, Quick, Quick
Controls, Quick Dialogs, Serial Port and WebSockets modules. Hosted builds
deploy the corresponding Qt shared libraries and install the license texts the
Qt SDK ships beside the application documentation whenever that SDK provides
them. The Qt edition a release is built against is recorded with the rest of
the dependency inventory, because it decides how that build may be
redistributed.

Hosted desktop packages may include the pinned ONNX Runtime CPU shared library
under its MIT license to execute an operator-supplied local character model.
The runtime is pinned by both version and source commit, and the build refuses
to configure against a distribution that does not carry its license,
third-party notice and privacy notice; it installs all three beside the
runtime. CW Buddy explicitly disables runtime telemetry and does not bundle,
download, or redistribute a model or model metadata.

Hosted desktop distributions provide the receive-only SoapySDR runtime under
the Boost Software License 1.0, the SoapyRTLSDR module under the MIT license,
and, on Windows, the SoapySDRPlay3 bridge under the MIT license,
and the redistributable librtlsdr/libusb runtime closure under their applicable
licenses. The Windows closure also carries the pthreads runtime those
components load, `pthreadVC3.dll`, with its copyright record. The Linux
portable closure also includes libudev and libcap with the distribution
copyright records for both. Portable packages bundle those components and
their license records; the Linux `.deb` declares the distribution module
dependency, whose package manager supplies its runtime and notices. Package
construction records exact versions or pinned source commits where components
are bundled. SDRplay's API,
service, and device driver remain external vendor prerequisites: CW Buddy
never redistributes them. The Windows CI builder downloads a checksum-pinned
official API installer only as an ephemeral link-time dependency; it is absent
from the release payload. A compatible externally installed SoapySDRPlay3
module may still be discovered without displacing bundled module paths.

The optional managed callsign cache downloads `MASTER.SCP` at runtime directly
from the [Super Check Partial Database](https://www.supercheckpartial.com/),
maintained by W9KKN. No explicit redistribution grant is published for that
dataset, so CW Buddy does not bundle or republish it. The integration uses
the provider's documented updater interface and keeps the downloaded data in
the user's application-data directory.

Release builds will generate a software bill of materials and third-party
notices. CI will add automated license-policy checks after dependency manifests
are introduced; a human review remains required for new licenses and exceptions.
