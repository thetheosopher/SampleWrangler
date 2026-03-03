# SampleWrangler Archive Checklist

Use this checklist before creating a shareable source archive.

## Keep in Archive

- Source code and project config:
  - `Source/`
  - `Tests/`
  - `CMakeLists.txt`
  - `CMakePresets.json`
  - `.gitignore`
  - `.gitmodules`
  - `AGENTS.md`
  - `LICENSE.txt`
- Build scripts and support files:
  - `cmake/`
  - `docs/`
  - `third_party/`
- Required assets and SDKs used by the project:
  - `Assets/`
  - `images/`
  - `JUCE/`
  - `REXSDK_Win_1.9.2/`
- Optional release artifacts (if you want to distribute installers with the archive):
  - `SampleWrangler-*-win64.msi`
  - `SampleWrangler-*-win64.zip`

## Exclude From Archive

- Temporary/generated build outputs:
  - `build/`
  - `packages/`
  - `_CPack_Packages/`
  - `Testing/Temporary/`
- IDE/local state folders (if present):
  - `.vs/`
  - `.vscode/` (optional: keep only if you intentionally share workspace settings)

## Quick PowerShell Cleanup (safe defaults)

```powershell
$paths = @('build','packages','_CPack_Packages','Testing/Temporary')
foreach ($p in $paths) { if (Test-Path $p) { Remove-Item -Recurse -Force $p } }
```

## Optional Validation Before Archiving

```powershell
$dirs = @('build','packages','_CPack_Packages','Testing/Temporary')
$dirs | ForEach-Object { "$_ => " + (Test-Path $_) }
Get-ChildItem -File -Name
```

Expected: all temp directories show `False`; installer files may remain if you choose to include them.
