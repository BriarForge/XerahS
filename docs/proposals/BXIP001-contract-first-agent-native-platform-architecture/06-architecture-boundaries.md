# 8. Target Architecture Boundaries

## 8.1 Product behavior versus platform mechanism

The contract defines outcomes rather than prescribing implementation unnecessarily.

Examples:

| Product contract | Native mechanism |
|---|---|
| Register a global capture shortcut | Windows hotkey APIs; macOS native hotkey APIs; Linux portal, compositor, or approved fallback |
| Request screen-recording permission | Windows capability behavior; macOS TCC flow; Linux desktop portal flow |
| Notify the user of upload completion | Windows notification; macOS UserNotifications; Linux desktop notification |
| Present application settings | Native controls and navigation on each platform |

## 8.2 Data and integration boundaries

Independent native implementations increase the importance of stable, language-neutral formats. The Product Contract SHALL define:

- Configuration and migration schemas
- History and task result formats
- CLI behavior and exit codes
- Automation and MCP contracts
- Plugin boundaries
- Uploader definitions and network behavior
- Diagnostic and telemetry semantics

Greenfield applications SHALL NOT load in-process .NET plugins. Decision [D-PLUG-001](08-decisions.md#d-plug-001-cross-language-plugin-automation-and-configuration) sets an out-of-process stdio JSON-RPC direction. The handshake, sandbox, secret-passing, and package format remain a follow-up proposal. Until that proposal lands, built-in uploaders and custom HTTP uploaders are the destination floor.

## 8.3 Reference implementations are informative, not normative

During greenfield development, the pinned KovaForge baseline is the primary discovery source and compatibility oracle. The ShareX Team tree, original ShareX, and related submodules remain valuable historical references. They SHALL NOT override an approved Product Contract or dictate the new repository structure. If code and contract disagree, the discrepancy must be classified explicitly rather than silently copying the code or silently omitting the behavior.

## 8.4 ImageEditor module boundary

Each native solution SHALL expose ImageEditor to its host application through a small, platform-idiomatic internal interface with equivalent contract semantics.

Conceptually, the editor input is:

| Input | Purpose |
|---|---|
| Source image | Image to annotate or transform |
| Editing mode | Standalone editing, capture annotation, or workflow task mode |
| Initial annotation document | Optional re-editable project state |
| Editor preferences | Contract-defined defaults and native presentation preferences |
| Host capabilities | Explicitly provided save, clipboard, upload-request, pin, and diagnostic capabilities |

The editor result is:

| Output | Purpose |
|---|---|
| Disposition | Confirmed, cancelled, or failed |
| Rendered image | Final image when confirmed |
| Annotation document | Re-editable, language-neutral project state |
| Requested continuation | Save, copy, upload, pin, or return-to-workflow intent where applicable |
| Diagnostics | Structured errors, warnings, and renderer information |

The ImageEditor feature owns:

- Native editor UI, input, selection, zoom, and accessibility.
- Annotation state, layer ordering, history, undo, and redo.
- Annotation rendering and image-effect execution.
- Editor-local preferences and project-document persistence.
- Import and export of the contract-defined annotation format.

The host XerahS application owns:

- Capture orchestration and source-image acquisition.
- File destinations and naming policy.
- Clipboard, upload, pin, history, automation, and post-capture workflows.
- Permission prompts and platform capabilities outside the editor.
- Interpretation and execution of requested continuation actions.

This boundary prevents ImageEditor from becoming a second application framework inside XerahS while keeping it independently testable within each native solution.

## 8.5 Existing ShareX.ImageEditor compatibility

The current editor's `.xann` version 1 format, annotation inventory, effect behavior, history semantics, and host callbacks SHALL be investigated as migration inputs. The Product Contract SHALL define which behaviors are retained, corrected, or intentionally discontinued.

The greenfield repository SHOULD import approved schemas, fixtures, and golden images as versioned test assets with appropriate license and provenance records. It SHOULD NOT require a live checkout of the legacy repository for normal builds, tests, or releases. If the legacy implementation is temporarily used as an oracle, it MUST run only in isolated development or compatibility tooling and MUST NOT be packaged with a native application.

## 8.6 Shared rendering-kernel exception

The initial architecture SHALL implement editor state, UI, and rendering within each native solution. A shared rendering submodule or common DLL SHALL NOT be introduced by default.

Image processing is nevertheless an exactness-sensitive domain. If qualification or implementation evidence shows that independently implementing complex effects causes unacceptable pixel drift, security risk, or maintenance cost, a follow-up proposal MAY propose a small headless rendering kernel with a stable language-neutral ABI.

Any approved kernel:

- MUST contain no UI, windows, dialogs, platform services, workflow orchestration, or product policy.
- MUST remain subordinate to the ImageEditor Product Contract.
- MUST be replaceable by a conforming native implementation.
- MUST use the same conformance vectors as all native renderers.
- MUST justify its repository and dependency model independently; approval is not implicit permission to restore the existing Avalonia submodule.

The evidence bar for proposing a kernel is [D-KERN-001](08-decisions.md#d-kern-001-shared-headless-rendering-kernel-threshold).

## 8.7 Coexistence with the Avalonia application

The greenfield applications SHALL NOT replace or reuse the production Avalonia application identity during development and parity validation.

- Avalonia XerahS keeps the existing product identity (`com.xerahs.app` and current installers).
- Greenfield applications SHALL use a distinct application identifier, window title suffix, and package name, for example `com.xerahs.native` and the display name "XerahS Native".
- Greenfield applications MUST be able to import contract-defined configuration, history, and annotation documents produced by Avalonia XerahS and ShareX, but MUST NOT write over the Avalonia application's live settings without an explicit user action.
- Side-by-side installation MUST be supported through the full-parity attestation and any later identity decision.
- Only a subsequent approved proposal may collapse the two identities or retire the Avalonia package.

See [D-ID-001](08-decisions.md#d-id-001-development-application-identity).

## 8.8 VideoEditor and media boundary

Each native solution SHALL expose VideoEditor and media tools through a platform-idiomatic internal feature boundary:

- The host owns file selection, workflow continuation, history, upload, naming, and permissions outside media processing.
- The feature owns native editing UI, timeline/selection state, preview, edit operations, encode/export requests, cancellation, progress, and recoverable diagnostics.
- Media operations use contract-defined time bases, frame rounding, audio behavior, codecs, quality settings, metadata handling, and failure semantics.
- FFmpeg or another approved third-party engine MAY execute media operations behind an adapter. The adapter and native UI remain separately testable.
- Native products MUST NOT require the legacy `ShareX.VideoEditor` checkout, React UI, or .NET backend at build or runtime.

The legacy submodule supplies fixture and compatibility evidence under recorded provenance. See [D-VID-001](08-decisions.md#d-vid-001-native-videoeditor-and-media-tools).

## 8.9 Reference isolation

The KovaForge checkout and its pinned submodules are read-only discovery inputs. BriarForge builds, tests, packages, and releases SHALL succeed when those checkouts are absent.

Imported fixtures, schemas, screenshots, and golden outputs MUST record source repository, commit, source path, license, transformation, and hash. Agents MAY study legacy algorithms but SHALL implement from the approved contract and native platform plan.
