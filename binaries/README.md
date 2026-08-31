# Download CW Assistant binaries

Compiled files are hosted as assets of the
[continuous development release](https://github.com/alessiobravi/CW-Assistant/releases/tag/continuous).
The repository keeps this stable index without placing generated binary payloads
in Git history.

## Direct downloads

- [Windows 11 x64 installer](https://github.com/alessiobravi/CW-Assistant/releases/download/continuous/cw-assistant-windows11-x64.msi)
- [Debian/Ubuntu x64 package](https://github.com/alessiobravi/CW-Assistant/releases/download/continuous/cw-assistant-debian-ubuntu-x64.deb)
- [Linux x64 portable archive](https://github.com/alessiobravi/CW-Assistant/releases/download/continuous/cw-assistant-linux-x64.tar.gz)
- [macOS Sonoma 14+ Apple silicon](https://github.com/alessiobravi/CW-Assistant/releases/download/continuous/cw-assistant-macos-arm64.tar.gz)
- [macOS Sonoma 14+ Intel x64](https://github.com/alessiobravi/CW-Assistant/releases/download/continuous/cw-assistant-macos-x64.tar.gz)
- [SHA-256 checksums](https://github.com/alessiobravi/CW-Assistant/releases/download/continuous/SHA256SUMS)
- [Machine-readable download manifest](latest.json)

The Windows download is an upgrade-capable MSI; a Windows `.tar.gz` is not
published. These are automated, unsigned, pre-release builds. The continuous release is
updated only after all supported-platform builds and core tests pass. Because
the repository is private, GitHub authentication and repository read access are
required to download an asset.

## Verify a download

Linux or macOS:

```sh
sha256sum -c SHA256SUMS --ignore-missing
```

On macOS, use `shasum -a 256 <downloaded-file>` if `sha256sum` is unavailable.
On Windows PowerShell:

```powershell
Get-FileHash .\cw-assistant-windows11-x64.msi -Algorithm SHA256
```

Compare the reported value with the corresponding entry in `SHA256SUMS`.
