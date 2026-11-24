# winget

Manifests for [winget](https://learn.microsoft.com/windows/package-manager/) (`winget install gSalzmann.Juice`).
Juice is a portable package: winget downloads `Juice.exe` from the GitHub release and adds the `juice`
command.

## Publishing a version

1. Publish the GitHub release `vX.Y.Z`. The *build* workflow attaches `Juice.exe` and `Juice.exe.sha256`.
2. Copy the previous folder to `manifests/g/gSalzmann/Juice/X.Y.Z`, update `PackageVersion`, `InstallerUrl`,
   `ReleaseDate` and `ReleaseNotesUrl`, and set `InstallerSha256` from `Juice.exe.sha256`.
3. `winget validate --manifest <folder>` and test with `winget install --manifest <folder>`.
4. Open a pull request with the folder in [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs)
   (same path under `manifests/`).