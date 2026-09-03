# Licensing policy

CW Assistant is distributed under `GPL-3.0-or-later`. Source files are covered
by that license unless a file clearly states a different compatible license.

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

Hosted desktop packages may include the pinned ONNX Runtime CPU shared library
under its MIT license to execute an operator-supplied local character model.
The build installs the matching license, third-party notice, and privacy notice
beside the runtime. CW Assistant explicitly disables runtime telemetry and does
not bundle, download, or redistribute a model or model metadata.

The optional managed callsign cache downloads `MASTER.SCP` at runtime directly
from the [Super Check Partial Database](https://www.supercheckpartial.com/),
maintained by W9KKN. No explicit redistribution grant is published for that
dataset, so CW Assistant does not bundle or republish it. The integration uses
the provider's documented updater interface and keeps the downloaded data in
the user's application-data directory.

Release builds will generate a software bill of materials and third-party
notices. CI will add automated license-policy checks after dependency manifests
are introduced; a human review remains required for new licenses and exceptions.
