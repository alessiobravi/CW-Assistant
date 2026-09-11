# CW Buddy manuals

These manuals describe the software from an operator's point of view. They are
updated with every user-visible change. Features marked **planned** are not yet
available in a downloadable build.

- [Operator guide](operator-guide.md): first launch, profiles, safe startup,
  frequency/split concepts, and current limitations.
- [Configuration reference](configuration-reference.md): every currently
  exposed station-profile setting, with practical examples.
- [Online builds and installation](online-builds-and-installation.md): GitHub
  builds, the Windows MSI, macOS Sonoma 14+ bundles, Debian/Ubuntu packages,
  and the portable Linux archive.
- [CAT4OM setup](cat4om-setup.md): native WebSocket connection, radio ID,
  read-only testing, ownership, security, and troubleshooting.

Engineering architecture, requirements, decisions, and conformance policy
remain one level above this folder; the delivery backlog is at the repository
root. The implemented full-passband baseline and the planned phase-aware
filtering, weak-signal multiple-pass behavior, and same-frequency pileup
separation are described in the
[high-accuracy decoder strategy](../decoder-strategy.md).
