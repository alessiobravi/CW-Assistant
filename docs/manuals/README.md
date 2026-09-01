# CW Assistant manuals

These manuals describe the software from an operator's point of view. They are
updated with every user-visible change. Features marked **planned** are not yet
available in a downloadable build.

- [Operator guide](operator-guide.md): first launch, profiles, safe startup,
  frequency/split concepts, and current limitations.
- [Configuration reference](configuration-reference.md): every currently
  exposed station-profile setting, with practical examples.
- [Online builds and installation](online-builds-and-installation.md): GitHub
  builds, the Windows MSI, macOS Sonoma 14+ bundles, and Debian/Ubuntu packages.
- [CAT4OM setup](cat4om-setup.md): native WebSocket connection, radio ID,
  read-only testing, ownership, security, and troubleshooting.

Engineering architecture, requirements, decisions, conformance policy, and the
delivery backlog remain one level above this folder. The implemented
full-passband baseline and the planned phase-aware filtering, weak-signal
multiple-pass behavior, and same-frequency pileup separation are described in
the [high-accuracy decoder strategy](../decoder-strategy.md).
