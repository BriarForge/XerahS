# Native implementation readiness

Contract version `0.3.1` (approval `APPROVAL-2026-10-08-LINUX-HYPRLAND-TOOLKIT`) is ready
for native coding to start on Windows, macOS, Linux, Android, and iOS. This page
states what each platform team starts with, in what order, and which gates stay
open without blocking the start.

## Status by target

| Target | Framework decision | Status |
|---|---|---|
| Windows | D-WIN-001: WinUI 3 with Win32/WinRT, C#, Windows 11 23H2+ | Ready to start |
| macOS | D-MAC-001: SwiftUI with AppKit, Swift, macOS 14+ | Ready to start |
| Linux `linux-qt` (reference edition) | D-LIN-001: Qt 6, C++17, Wayland first | Ready to start |
| Linux `linux-gnome` (Ubuntu, Fedora Workstation) | D-LIN-002: GTK 4 with libadwaita, proposed | Ready once the product owner confirms the toolkit |
| Linux `linux-kde` (Fedora KDE, Kubuntu) | D-LIN-002: Qt 6 with KDE Frameworks 6, proposed | Ready once the product owner confirms the toolkit |
| Linux `linux-hyprland` (Omarchy, Arch with Hyprland) | D-LIN-002: Qt 6 with LayerShellQt, confirmed 2026-10-08 | Ready to start as a candidate edition |
| Android | D-AND-001: Kotlin, Jetpack Compose, Android 10+ | Ready to start within `mobile-scope.json` |
| iOS and iPadOS | D-IOS-001: Swift, SwiftUI with UIKit, iOS 17+ | Ready to start within `mobile-scope.json`; release blocked by the MOB-018 licensing review |

## Start order

1. **Wave 1 qualification**, on every ready target:
   `FILENAME-GENERATION-001`, `POST-CAPTURE-ACTIONS-001`,
   `REGION-CAPTURE-001`, and `EDITOR-SESSION-001`, plus `MOBILE-PLATFORM-001`
   on Android and iOS. Each has normative text,
   scenarios, and deterministic vectors that decide the cross-platform
   behavior an implementation would otherwise have to guess.
2. **ImageEditor expansion**: `EDITOR-ANNOTATIONS-001`, `EDITOR-EFFECTS-001`,
   `EDITOR-CANVAS-001`, `EDITOR-OUTPUT-ACTIONS-001`, `EDITOR-SETTINGS-001`, and
   `EDITOR-UTILITIES-001`.
3. **Foundation and catalogues**: `CORE-PLATFORM-001`, `SETTINGS-CATALOG-001`,
   `WORKFLOW-CATALOG-001`, `INTERFACE-CATALOG-001`, and
   `COMPATIBILITY-CATALOG-001`, delivered by the waves in BXIP001 section 21.

## Headless test seams each implementation provides

Every implementation exposes these operations to its conformance adapter under
[`VECTORS.md`](VECTORS.md), using production code with injected inputs:

| Capability | Operations |
|---|---|
| `FILENAME-GENERATION-001` | `expand`, `preview` |
| `POST-CAPTURE-ACTIONS-001` | `run-pipeline`, `map-legacy-flags` |
| `REGION-CAPTURE-001` | `map-selection`, `keyboard-adjust`, `composite-coverage`, `state-events`, `last-region` |
| `EDITOR-SESSION-001` | `history`, `render`, `parse-document`, `round-trip`, `sidecar-path`, `save-sidecar` |
| `EDITOR-CANVAS-001` | `apply-operations` |
| `EDITOR-SETTINGS-001` | `load-settings`, `recent-files-add`, `settings-round-trip` |
| `MOBILE-PLATFORM-001` (Android, iOS) | `share-intake`, plus `expand` and `run-pipeline` with a `platform` input |

## Open gates that do not block starting

BXIP001 section 20.8 allows implementation before census closure. These gates
block activation or release, not coding:

- baseline census closure and product-owner signature;
- the shared conformance runner and per-platform adapters (built alongside the
  first platform code);
- golden-image fixtures and `conformance/image-editor/tolerances.yaml`
  (D-GOLD-001) for editor visual evidence beyond the ES-025 pixel vectors;
- runtime observation of the baseline `.xann` sidecar name (ES-030);
- the MOB-018 licensing review before any iOS release, and per-row mobile
  dispositions for `WORKFLOW-CATALOG-001` before it activates on mobile;
- named Windows, macOS, Linux, Android, iOS, and conformance owners (D-OWN-001);
- security, privacy, accessibility, licensing, and compatibility reviews;
- conformance evidence and parity review for every requirement on every
  supported target, including each supported Linux edition.

## Rule for ambiguity found during coding

When an implementation finds behavior the contract does not decide, it stops
guessing: record a clarification under ROOT-ESCALATE-001, add the deciding
text and vector through the product owner, and implement from the updated
contract on every target.
