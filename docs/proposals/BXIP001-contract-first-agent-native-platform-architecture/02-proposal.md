# 3. Proposal

## 3.1 Establish three sources of truth

XerahS SHALL distinguish three forms of truth:

| Layer | Source of truth | Responsibility |
|---|---|---|
| Product truth | Product Contract | Defines user-visible behavior and product invariants |
| Platform truth | Native implementation | Defines how an operating system realizes the contract |
| Conformance truth | Tests and evidence | Demonstrates that an implementation satisfies the contract |

No platform implementation SHALL become the product specification merely by being first or most complete.

## 3.2 Replace the conceptual common DLL with a Product Contract

The Product Contract is not a runtime binary. It is a versioned repository artifact that humans and agents use at development time.

Plain English is its primary interface because it communicates intent, context, and expected user experience across implementation languages. English SHALL be made precise with normative terms:

- **MUST** and **MUST NOT** define required behavior.
- **SHOULD** and **SHOULD NOT** define preferred behavior that may have a documented exception.
- **MAY** defines optional behavior.

The contract SHALL include machine-readable artifacts wherever exact output or state is important. Prose alone is not sufficient for serialization, naming, migration, security, ordering, concurrency, image processing, or other behavior that can be expressed deterministically.

## 3.3 Implement the contract through native platform applications

The greenfield repository will contain three independently buildable platform applications:

| Platform | Native direction | Notes |
|---|---|---|
| Windows | WinUI 3 with Windows App SDK for application chrome; Win32 and WinRT for capture overlay, DXGI, hotkeys, tray, and startup | Language: C#. OS baseline: Windows 11 23H2 and later. [D-WIN-001](08-decisions.md#d-win-001-windows-framework-and-os-baseline) |
| macOS | SwiftUI for chrome; AppKit for overlay, status item, and capture surfaces SwiftUI cannot host | Language: Swift. OS baseline: macOS 14 Sonoma and later. [D-MAC-001](08-decisions.md#d-mac-001-macos-swiftuiappkit-boundary-and-os-baseline) |
| Linux | Qt 6 (Widgets for overlay, tray, and input; Qt Quick optional for settings chrome) plus xdg-desktop-portal | Language: C++17. Wayland first, X11 fallback. [D-LIN-001](08-decisions.md#d-lin-001-linux-toolkit-desktop-display-server-portal-distro-and-packaging) |

The Product Contract SHALL remain independent of these choices. Replacing a platform framework must not require redefining product behavior. The table records pilot-binding defaults from [architecture decisions](08-decisions.md); a later proposal may change a framework after measured evidence, without rewriting contracted product outcomes.

## 3.4 Permit native adaptation without permitting silent divergence

Parity means equivalent product outcomes, not necessarily identical pixels or interaction mechanics.

Each implementation:

- MUST satisfy shared behavioral invariants.
- MUST use native accessibility, navigation, permission, and lifecycle conventions where they differ.
- MAY use different interaction mechanics when required by platform conventions.
- MUST document any material behavioral deviation.
- MUST NOT silently omit a contracted capability.

For example, a settings view may use different native controls and layout on each platform while preserving the same setting meanings, defaults, validation, persistence, and downstream effects.

## 3.5 Integrate ImageEditor into each native platform solution

ImageEditor is a defining XerahS product capability and SHALL be implemented as an internal, first-class feature module in each native platform solution. The greenfield applications SHALL NOT take a production dependency on the existing Avalonia `ShareX.ImageEditor` repository as a Git submodule, linked library, embedded UI, or required runtime process.

The shared ImageEditor asset will be its Product Contract and conformance corpus, not a common UI binary:

```text
ImageEditor Product Contract
        |
        +-- Windows native ImageEditor feature
        +-- macOS native ImageEditor feature
        +-- Linux native ImageEditor feature
        `-- Cross-platform conformance corpus
```

Each native ImageEditor SHALL live in the same repository as its native XerahS application so a behavior change can update the contract, all affected implementations, fixtures, and traceability evidence atomically.

The existing [KovaForge/ShareX.ImageEditor](https://github.com/KovaForge/ShareX.ImageEditor) repository will continue to serve ShareX and the existing Avalonia XerahS. For the greenfield project it is a legacy reference implementation and source of compatibility evidence, not a component of the target runtime architecture.
