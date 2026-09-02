# Application packaging assets

The application mark combines a simplified straight Morse key with one dot and
one dash. It uses the desktop palette and avoids text so it remains language
neutral and recognizable in compact operating-system UI.

- `cw-assistant-icon.png`: 1024 px RGBA source master.
- `cw-assistant.png`: 512 px Linux icon and Qt runtime resource.
- `cw-assistant.ico`: Windows executable, shortcut, and installed-app icon.
- `cw-assistant.icns`: macOS application-bundle icon.
- `cw-assistant.rc`: Windows executable resource binding.
- `windows/windows-installer-patch.xml`: WiX v3 finish-page launch choice and
  bounded running-application handling for upgrades.
- `windows/VerifyWindowsInstaller.ps1`: CI inspection of the generated MSI
  tables for those launch and shutdown guarantees.

When the source mark changes, regenerate all platform files in the same commit,
inspect at 16, 32, and 64 px, and run the complete packaging matrix. Keep the
silhouette simple and avoid text, 3D treatments, or decorative radio elements
that impair small sizes.
