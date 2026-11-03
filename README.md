# XerahS (native)

Greenfield, contract-first native XerahS for Windows, macOS, and Linux desktops and Android and iOS mobile devices.

This repository is **not a fork** of [ShareX/XerahS](https://github.com/ShareX/XerahS). It does not inherit that tree's history, Avalonia UI, or project layout. ShareX/XerahS remains the production Avalonia application and a behavioral reference. This repository implements the same product from a versioned Product Contract, natively on each operating system.

| Layer | Lives here |
|---|---|
| Product truth | `product-contract/` |
| Platform truth | `platforms/windows`, `platforms/macos`, `platforms/linux`, `platforms/android`, `platforms/ios` |
| Conformance truth | `conformance/` |

Application identity: `com.xerahs.app` / "XerahS". The native application replaces Avalonia XerahS after full-parity acceptance, with explicit settings migration and recovery safeguards.

## Progress

Recorded implementation coverage as of 2026-11-04, from the
[platform progress reports](platforms/PROGRESS.md):

| OS | Implementation coverage | Current status |
|---|---:|---|
| [Linux](platforms/linux/PROGRESS.md) | **53.4%** — 173 / 324 desktop requirements | Native Qt application in development; 125 requirements partial and 26 not started. |
| [Windows](platforms/windows/PROGRESS.md) | **0.0%** — 0 / 324 desktop requirements | Native application not started; requirement implementation unassessed. |
| [macOS](platforms/macos/PROGRESS.md) | **0.0%** — 0 / 324 desktop requirements | Native application not started; requirement implementation unassessed. |
| [Android](platforms/android/README.md) | Not yet assessed | Native application not started; mobile scope tracked separately. |
| [iOS / iPadOS](platforms/ios/README.md) | Not yet assessed | Native application not started; mobile scope tracked separately; distribution licensing review required. |

Coverage counts only requirements explicitly marked implemented. Partial and
unassessed requirements receive no credit. These unweighted percentages measure
recorded contract coverage, **not production readiness or remaining effort**.
Release still requires supported-target qualification, conformance, parity, and
human security, privacy and accessibility reviews. Mobile requirements are
outside the desktop denominator; see [MOBILE-PLATFORM-001](product-contract/capabilities/MOBILE-PLATFORM-001/SPEC.md).

The Product Contract is version 0.3.1 with lifecycle `draft`. Its
[backlog status](product-contract/backlog/STATUS.md) tracks contract work;
platform reports track native implementation. See
[native implementation readiness](product-contract/READINESS.md) for sequencing
and acceptance gates. Update this snapshot when regenerating platform progress.

## Architecture

Start at [BXIP001](docs/proposals/BXIP001-contract-first-agent-native-platform-architecture/README.md). ShareX Team tracks the same proposal as XIP0086.

Pilot-binding defaults (see BXIP001 decisions):

- Windows: WinUI 3 + Win32/WinRT, C#, Windows 11 23H2+
- macOS: SwiftUI + AppKit, Swift, macOS 14+
- Linux: Qt 6, C++17, Wayland first (reference edition); distro-native editions such as Omarchy, Ubuntu, and Fedora under D-LIN-002
- Android: Kotlin + Jetpack Compose, Android 10+ (D-AND-001)
- iOS and iPadOS: Swift + SwiftUI, iOS 17+ (D-IOS-001); scope in `MOBILE-PLATFORM-001`
- No shared product runtime binary in the pilot
- No in-process .NET plugins

## Layout

```text
product-contract/   Product Contract (what XerahS does)
platforms/          Native applications (how each OS does it)
conformance/        Shared scenarios, vectors, and evidence
tools/              Contract linter and governance tooling
docs/proposals/     BXIP001 and later proposals
```

## License

GNU GPL v3. See [LICENSE](LICENSE). Copyright ShareX Team.

## Verification

Governance and contract checks use only the Python standard library:

```sh
python tools/contract-linter/lint.py
python -m unittest discover tools/contract-linter/tests -v
python tools/contract-backlog/tracker.py --check
python -m unittest discover tools/contract-backlog/tests -v
python tools/contract-linter/lint.py --changed-since origin/main   # effective instructions
```
