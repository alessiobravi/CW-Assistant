# Application packaging assets

The application mark uses a blue/cyan receiver arc, a central Morse sequence,
and the CW Buddy wordmark on a dark rounded-square field. Its exterior is true
alpha transparency rather than black corner pixels. The repository-owned 1024
px master is the source for every platform asset and for the README image.

- `cw-buddy-icon.png`: 1024 px RGBA source master.
- `cw-buddy.png`: 512 px Linux icon and Qt runtime resource.
- `on-air-active.png`: transparent compact guarded-KEY activity mark used by
  the operating panel; the UI dims it while KEY is inactive.
- `cw-buddy.ico`: Windows executable, shortcut, and installed-app icon.
- `cw-buddy.icns`: macOS application-bundle icon.
- `cw-buddy.rc`: Windows executable resource binding.
- `windows/windows-installer-patch.xml`: WiX v3 finish-page launch choice and
  bounded running-application handling for upgrades.
- `windows/VerifyWindowsInstaller.ps1`: CI inspection of the generated MSI
  tables for those launch and shutdown guarantees.

When the source mark changes, regenerate all platform files in the same commit,
inspect at 16, 32, and 64 px, and run the complete packaging matrix.
