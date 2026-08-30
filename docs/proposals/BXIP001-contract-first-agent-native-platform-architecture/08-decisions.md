# 14. Architecture Decisions

These decisions bind the greenfield pilot. Each may be superseded only by a later approved proposal, or by a time-bound waiver under [D-REL-001](#d-rel-001-release-policy-when-a-platform-cannot-implement-a-capability). They are judgments for approval, not observations from a completed pilot.

## D-OWN-001 Product Contract ownership and approval authority

**Decision.** Separate authorship from approval.

| Role | Authority | Current holder |
|---|---|---|
| Product owner (human) | Approves user-visible contract behavior, Unavailable/Degraded dispositions, waivers, root constitution changes, and releases | Michael D |
| Contract steward | Owns `product-contract/` quality, identifiers, versioning, and patch-level clarifications that do not change behavior | Designated agent or human reporting to the product owner |
| Platform owner (Windows, macOS, Linux) | Owns native realization, platform tests, packaging, and signing for that OS | One named owner per platform; a person MAY hold more than one platform |
| Conformance owner | Owns the shared runner, fixtures, tolerances, and independent verification | Must not be the sole platform owner of a platform under test |
| Governance owner | Owns the `AGENTS.md` hierarchy, rule-ID registry, and instruction linter | Product owner unless delegated in writing |

Agents MAY author contracts, implementations, and tests. Agents MUST NOT be the sole approver of a MUST/MUST NOT behavior change, a waiver, or a release.

## D-CON-001 Contract schema, versioning, and tooling

**Decision.** The pilot contract format is the directory layout in [§4.1](03-product-contract.md#41-proposed-repository-structure).

- Each capability is a directory named `<AREA>-<SLUG>-<NNN>/` containing `SPEC.md`, `SCENARIOS.feature`, and any of `STATE_MACHINE.md`, `*.schema.json`, `test-vectors.json`, `fixtures/`, and `platforms/*.md` that the capability requires.
- `product-contract/manifest.yaml` lists every capability, its SemVer, and its requirement IDs. The manifest schema SHALL be published as JSON Schema.
- Normative prose uses RFC 2119 keywords. Requirement IDs are stable (`PCC-001`, `FN-004`) and never reused for a different meaning.
- Versioning follows [§7.4](05-conformance.md#74-compatibility). The contract version is the product version the native apps claim.
- Tooling: a `tools/contract-linter` validates manifests, IDs, parent/child `AGENTS.md` links, and required files. The conformance runner in `conformance/runner` is a pinned Python 3.12-or-later CLI for the pilot; platform adapters are thin executables invoked by that runner.
- English remains the primary human interface. Machine-readable artifacts are mandatory wherever [§4.4](03-product-contract.md#44-exact-behavior-requires-exact-evidence) says they SHOULD exist; for the four pilot slices they SHALL exist.

## D-WIN-001 Windows framework and OS baseline

**Decision.** Windows App SDK with WinUI 3 for settings, history, and editor chrome. Win32 and WinRT for region-capture overlay, DXGI Desktop Duplication, global hotkeys, notify-icon tray, startup, and file-type association. Language: C# with CsWin32 or equivalent projections. Packaging for the pilot: unpackaged desktop first (ShareX-class capture and hotkeys are simpler unpackaged). MSIX is a later packaging proposal.

**OS baseline:** Windows 11 version 23H2 and later. Windows 10 is out of scope for greenfield native apps because it is past mainstream support as of this proposal; Avalonia XerahS remains the Windows 10 vehicle.

**Rationale.** WinUI 3 is the current Windows desktop UI stack and gives native Windows 11 accessibility and controls. Capture overlays, DXGI, and tray behavior still live in Win32/WinRT, so a hybrid is required rather than a pure WinUI app. WPF is in maintenance. MAUI and Avalonia would reintroduce a cross-platform UI layer, which this proposal exists to leave. C# is the language agents and the current Windows platform layer already use well; C++ is not required for native Windows behavior.

## D-MAC-001 macOS SwiftUI/AppKit boundary and OS baseline

**Decision.** SwiftUI for settings, history, onboarding, and editor chrome that SwiftUI can host. AppKit for the capture overlay, `NSStatusItem`, panel-style utility windows, and any ScreenCaptureKit preview surface SwiftUI cannot host with acceptable latency. Notifications: `UNUserNotificationCenter`. Login item: `SMAppService`. Hotkeys: Carbon `RegisterEventHotKey` as the primary path so Accessibility is not a prerequisite; it remains the supported hotkey API on macOS 14+ despite the broader Carbon deprecation. Capture: ScreenCaptureKit, with `SCScreenshotManager` on macOS 14+. Language: Swift.

**OS baseline:** macOS 14 Sonoma and later. macOS 12.3 remains the historical ScreenCaptureKit floor for Avalonia XerahS; it is not a greenfield SwiftUI baseline.

**Rationale.** SwiftUI is production-ready on 14+ and matches the agent-native, declarative implementation style. AppKit remains mandatory for overlay windows and menu-bar-only operation. Raising the floor from 12.3 avoids spending the pilot on SwiftUI backports.

## D-LIN-001 Linux toolkit, desktop, display-server, portal, distro, and packaging

**Decision.**

- **Toolkit:** Qt 6, dynamically linked (LGPL), GPL v3 application. Qt Widgets for overlay, tray, and global input. Qt Quick MAY be used for settings and history chrome. GTK4/libadwaita is rejected for the pilot because ShareX-class overlay and power-user density fights the GNOME HIG, while Flameshot, Spectacle, and Ksnip already prove this product shape on Qt.
- **Desktops:** GNOME 46+ and KDE Plasma 6 are first-class. wlroots compositors (Sway, Hyprland) are best-effort through portals.
- **Display server:** Wayland first. X11 is a documented fallback, not the design center.
- **Portals:** xdg-desktop-portal is required for Screenshot, ScreenCast, GlobalShortcuts, Notification, FileChooser, and Inhibit. Direct protocols are fallbacks when a portal is absent, and they MUST be diagnosed in the UI rather than failing silently.
- **Distros:** Ubuntu 24.04 LTS is the documentation and developer target. Fedora current Workstation (SELinux enforcing, Flatpak-first) is the acceptance gate, matching XIP0082. Arch is the tertiary smoke target. NixOS is out of scope.
- **Packaging:** `.tar.gz`, `.deb`, and `.rpm` are first-class. AUR packaging continues. Flatpak is the intended store path but is not a pilot publication gate. AppImage is deferred, matching XIP0079.
- **Filesystem:** XDG Base Directory Specification, matching XIP0075. No home-directory litter.
- **Language:** C++17.

## D-REPO-001 Monorepo

**Decision.** One repository: [BriarForge/XerahS](https://github.com/BriarForge/XerahS). The Product Contract, three platform trees, conformance corpus, and governance tooling share one revision so a behavior change can update spec, implementations, fixtures, and traceability atomically.

CI SHALL use path filters so a Windows-only change does not require a macOS or Linux full build, and vice versa. Sparse checkout is allowed. Splitting into per-platform repositories is forbidden until a later proposal shows that monorepo operational cost exceeds the coordination cost of multi-repo contract changes.

This proposal (BXIP001) is the canonical architecture document and lives in this repository. [XIP0086](https://github.com/ShareX/XerahS/blob/develop/docs/proposals/xip/XIP0086-contract-first-agent-native-platform-architecture.md) in the ShareX Team tree is a pointer to this directory.

## D-SHARE-001 Shared binaries

**Decision.** The pilot ships no shared product runtime binary and no common UI or capture library.

Shared artifacts are limited to:

- the Product Contract, schemas, fixtures, and golden images
- the conformance runner and its adapters
- the contract linter and governance tooling

OS secret stores (DPAPI, Keychain, libsecret) are used in-process on each platform; there is no shared crypto DLL.

A later proposal MAY add a shared library only for (a) the headless rendering kernel if [D-KERN-001](#d-kern-001-shared-headless-rendering-kernel-threshold) is met, or (b) a tiny language-neutral filename/token expander if independent implementations diverge on the FILENAME-GENERATION-001 vectors after two remediation cycles. Shared code remains an implementation choice subordinate to the contract.

## D-PLUG-001 Cross-language plugin, automation, and configuration

**Decision.** Greenfield applications SHALL NOT load in-process .NET plugins.

- **Automation:** CLI and MCP remain the language-neutral automation surface (XIP0063, XIP0064). Their contracts are part of the Product Contract, with stable subcommands, JSON output, and exit codes.
- **Configuration, history, and annotation documents:** versioned JSON Schema, UTF-8, language-neutral. Custom uploader HTTP templates stay first-class because they are already data, not code.
- **Plugins:** out-of-process, capability-declared, stdio JSON-RPC, settings as JSON Schema. This proposal locks that direction. A follow-up proposal specifies the handshake, sandbox, secret-passing, and packaging. Until that proposal lands, built-in uploaders and custom HTTP uploaders are the only destinations the native apps MUST implement.
- **Migration:** Avalonia/ShareX plugin settings that can be expressed as custom HTTP uploaders MUST import. Arbitrary in-process .NET plugin code is compatibility-best-effort through the Avalonia app, not through the native apps.

## D-REL-001 Release policy when a platform cannot implement a capability

**Decision.** A platform may claim a contract version only when every required requirement for that version has an accepted disposition on that platform.

- Dispositions are the [§7.3](05-conformance.md#73-platform-capability-matrix) categories: Required, Equivalent, Degraded, Unavailable, Not applicable.
- Unavailable and Degraded require product-owner approval before the contract version is tagged.
- "Not implemented" is not a disposition.
- Time-bound waivers: named owner, user-visible limitation, expiry no longer than 90 days, and a remediation plan. Expired waivers fail the release gate.
- Staggered *builds* are allowed. Staggered *claimed contract versions* are not. If Windows is ready for contract 1.2.0 and Linux is not, Windows stays on 1.1.x, or 1.2.0 is tagged only after Linux has a disposition.
- Security patches MAY ship per-platform immediately without waiting for contract parity.
- Windows-first silent landing of user-visible features is forbidden as the default.

## D-REV-001 Human review boundaries

**Decision.** Human review is required for:

1. Any contract change that alters user-visible behavior, persistence, security, privacy, permissions, or compatibility.
2. Unavailable, Degraded, and waiver requests.
3. Root `AGENTS.md` constitution changes.
4. Security-sensitive implementation: capture permission flows, credential storage, uploader secrets, plugin hosts, and network trust.
5. Signing, notarization, and release publication.

Human review is not required before landing:

- patch-level contract clarifications that do not change behavior
- additional tests and fixtures
- platform-idiomatic layout or control changes that preserve contracted semantics
- non-behavior refactors inside one platform tree

The same agent SHOULD NOT be the sole author, implementer, verifier, and approver of a material contract change. The product owner is the only authority that can accept Unavailable or an expired-waiver extension.

## D-AGT-001 AGENTS.md hierarchy

**Decision.**

- **Ownership:** governance owner, with product-owner approval for the root constitution.
- **Rule-ID namespaces:** `ROOT-*`, `CONTRACT-*`, `PLATFORM-*`, `WIN-*`, `MAC-*`, `LIN-*`, `CONF-*`, `EDITOR-*`, `TOOL-*`. IDs are unique repository-wide and never reused.
- **Maximum depth:** four levels (root, first-level scope, platform, feature module such as `platforms/macos/image-editor`). Deeper files require a governance-owner exception recorded in `product-contract/decisions/`.
- **Child files** add constraints only; they reference parent IDs and MUST NOT copy parent rule text.
- **CI:** the contract linter is blocking on broken parent/child links, missing or duplicate IDs, child override of protected root rules, directories that declare a scope but are absent from the parent index, and contract or platform changes that lack traceability updates. CI SHALL publish an effective-instructions report for every changed path.

## D-GOLD-001 Golden-image tolerances

**Decision.** Conformance distinguishes exact and perceptual comparisons. The conformance owner maintains `conformance/image-editor/tolerances.yaml`. Missing tolerances fail closed.

| Class | Comparison | Tolerance |
|---|---|---|
| Synthetic vector annotations, no text, integer coordinates, sRGB PNG, CPU rasterizer | Byte-identical PNG after canonical encoding, or per-pixel exact RGB | Zero |
| Lossless round-trip of an input PNG with no effects | Byte-identical | Zero |
| Lossy encode (JPEG/WebP) of a specified quality | SSIM against the contract encoder vector | SSIM >= 0.990 |
| Text rendered with the bundled conformance typeface | Per-pixel in sRGB after rasterizing at contract DPI | Max CIEDE2000 1.0; no system fonts |
| Image effects on CPU | Per-pixel or SSIM as declared per effect | Default max 1 LSB per 8-bit channel, or SSIM >= 0.995 |
| GPU-accelerated path | Same vectors as CPU | Must match the CPU result within the effect's published tolerance |
| Color-managed fixtures | Convert to linear sRGB using the embedded profile, then compare | Fixtures are tagged sRGB unless a test explicitly uses another profile |

Rules:

- Editor-export goldens use a bundled OFL typeface (Noto Sans or equivalent) checked into `conformance/image-editor/fonts/`. Native UI may use system fonts; export goldens MUST NOT.
- Deterministic conformance runs with the GPU rasterizer disabled. GPU is a performance path that must still pass the same vectors within tolerance.
- Resampling algorithm is named in the contract (default: Catmull-Rom). The CPU reference is authoritative.
- Font hinting, subpixel positioning, and ClearType/Core Text/FreeType differences are why export goldens use bundled fonts and a declared DPI. If two platforms still diverge on bundled-font text after one remediation cycle, the conformance owner MAY widen that case to CIEDE2000 2.0 without changing exact classes.

## D-KERN-001 Shared headless rendering kernel threshold

**Decision.** A follow-up proposal proposing a shared headless rendering kernel is justified only when at least one of the following is true:

1. Two or more independent native renderers fail the same required effect vector after two documented remediation cycles.
2. Pixel drift on a required effect exceeds [D-GOLD-001](#d-gold-001-golden-image-tolerances) tolerance on two or more platforms, and the cause is algorithmic rather than a single-platform bug.
3. Security review finds duplicated unsafe image-parsing or decoder code with divergent patch status.
4. Over a measured six-week window, the cost of keeping three effect implementations exceeds twice the estimated cost of one kernel plus three thin adapters.
5. A compatibility requirement needs bit-exact export across platforms (for example, reopen `.xann` and export an identical PNG on every OS).

Convenience, one platform falling behind, or a preference for a common DLL is not sufficient. Any kernel remains bound by [§8.6](06-architecture-boundaries.md#86-shared-rendering-kernel-exception).

## D-ID-001 Pilot application identity

**Decision.** Recorded in [§8.7](06-architecture-boundaries.md#87-coexistence-with-the-avalonia-application). Greenfield apps use `com.xerahs.native` (or the platform equivalent) and the display name "XerahS Native" until a later proposal authorizes identity collapse.

# 15. Residual follow-ups

These are intentionally not decided here and do not block approval of this proposal:

- Plugin handshake, sandbox, secret-passing, and package format (follow-up proposal after [D-PLUG-001](#d-plug-001-cross-language-plugin-automation-and-configuration)).
- MSIX, Apple notarized DMG/App Store, and Flathub publication of the *native* apps.
- Shared rendering kernel (only if [D-KERN-001](#d-kern-001-shared-headless-rendering-kernel-threshold) is met).
- Avalonia retirement or identity collapse (only after Phase 4 evidence).
- Windows 10 or macOS 13 support for native apps.
- Exact Windows App SDK and Qt 6 minor versions, which are pinned in platform `AGENTS.md` at implementation time.
