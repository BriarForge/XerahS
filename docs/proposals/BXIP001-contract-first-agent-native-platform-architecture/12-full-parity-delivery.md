# 21. Full-Parity Delivery Program

## 21.1 Required outcome

Following BXIP001 SHALL produce installable native Windows, macOS, and Linux XerahS applications in this repository that provide the complete contracted desktop functionality of the pinned KovaForge baseline. The four original pilot capabilities are the first qualification tranche; they are not the end state and no second architecture approval is required to continue into full implementation.

The Avalonia application remains available as a reference and fallback until the full-parity release gate passes. BriarForge implementation does not modify the KovaForge checkout.

## 21.2 Capability domains

The capability ledger SHALL cover at least these domains. The examples are discovery seeds, not a closed list.

| Domain | Baseline outcomes that must be inventoried and delivered |
|---|---|
| Application shell | Launch, single-instance behavior, tray, menus, lifecycle, startup, notifications, theme, localization, onboarding, updates |
| Configuration and security | Application settings, workflow settings, uploader settings, defaults, validation, backups, import, migration, secret storage |
| Capture | Fullscreen, monitor, active monitor, window, active window, region, transparent region, custom region, last region, scrolling capture, auto capture, mixed DPI, HDR, cursor/window/control detection |
| Workflow engine | Independent workflows, hotkeys, task settings, job queue, after-capture tasks, quick actions, after-upload tasks, cancellation, retry, recovery |
| Recording | Region/window/custom/last-region recording, video and GIF, audio, encoders, pause/stop/abort, partial recovery, diagnostics |
| ImageEditor | Native editor session, annotations, selection, history, effects, serialization, `.xann`, export, capture-overlay annotations |
| VideoEditor and media | Native video-editing workflow, trim/cut/encode operations, FFmpeg/native media paths, thumbnails, image/media utilities |
| Upload and destinations | Image/file/text upload, URL shortening, sharing, custom uploaders, OAuth, multiple instances, first-party destination coverage, plugin extensibility outcome |
| History and indexing | Task results, SQLite/data migration, thumbnails, search/filter, media explorer, cleanup, reopen/re-edit |
| Automation and integration | CLI, JSON output, exit codes, MCP, assistant, Send To, watch folder, shell/file associations, headless execution |
| Diagnostics and recovery | Logging, support bundles, permission diagnostics, degraded-mode reporting, crash/startup recovery, data corruption handling |
| Distribution quality | Native packaging, signing/notarization, installation, side-by-side import, updates, uninstall safety, accessibility and performance gates |

Known first-party destination projects in the baseline include Amazon S3, Auto, Bitly, Dropbox, FTP/SFTP, GitHub Gist, Imgur, Immich, Nextcloud, Paste2, Pastebin, and XBackBone. The census, rather than this sentence, is authoritative if the baseline contains more.

## 21.3 Delivery waves

Work proceeds in dependency-aware waves. Domains within a wave MAY run in parallel when agents own non-overlapping paths. A wave may overlap the next only when its contracts and interfaces are stable enough that downstream work is not guessing.

### Wave 0 - Baseline freeze and governance

- Commit the artifacts from [section 20](11-reference-baseline.md).
- Make the contract linter, instruction resolver, provenance checks, and parity dashboard blocking.
- Establish platform build/test skeletons and contract adapter protocols.
- Assign product, contract, platform, conformance, security, and release owners.

### Wave 1 - Qualification tranche

Implement `FILENAME-GENERATION-001`, `POST-CAPTURE-ACTIONS-001`, `REGION-CAPTURE-001`, and `EDITOR-SESSION-001` on all three platforms. Use the results to tune process and tooling, not to decide whether full parity remains the target.

Architecture changes discovered here require an approved BXIP amendment; ordinary implementation continues automatically once qualification gates pass.

### Wave 2 - Product foundation and compatibility

- Application shell, lifecycle, tray, notifications, startup, theme, localization, and accessibility foundation
- Language-neutral configuration, history, task-result, workflow, annotation, and uploader schemas
- KovaForge configuration/import migration with rollback and dry-run reporting
- Native secret stores and credential migration
- Logging, diagnostics, crash recovery, and stable error taxonomy

### Wave 3 - Capture, workflows, and recording

- All capture and region-selection modes from the ledger
- Hotkeys and independent workflow editing
- Complete after-capture, quick-action, and after-upload pipelines
- Recording, GIF, audio, encoder selection, recovery, and diagnostics
- Permission and multi-monitor/DPI/HDR conformance on each OS

### Wave 4 - Editors and media

- Complete native ImageEditor surface and capture-overlay annotation integration
- All contracted annotations, effects, history, project formats, and exports
- Native VideoEditor/media tools and compatibility behavior under [D-VID-001](08-decisions.md#d-vid-001-native-videoeditor-and-media-tools)
- Deterministic image/media fixtures and measured performance gates

### Wave 5 - Destinations and extensibility

- All first-party destinations and destination settings from the ledger
- Custom uploader import and execution
- OAuth and platform secret-store flows
- Multiple destination instances, fallback selection, URL shorteners, and sharing
- Language-neutral out-of-process plugin protocol, SDK, packaging, permissions, diagnostics, and migration guidance

### Wave 6 - History, automation, and power-user integration

- History, indexing, thumbnails, search, media exploration, cleanup, and re-editing
- CLI command and exit-code parity
- MCP and assistant capability parity
- Watch-folder daemon, Send To, shell/file associations, and headless behavior
- Backup, restore, portable/export behavior where present in the ledger

### Wave 7 - UX completeness and native distribution

- Every baseline settings surface and user-visible command
- Onboarding, diagnostics, permission education, keyboard navigation, screen-reader coverage, localization
- Signed/notarized installable artifacts for the supported platform baselines
- Update, side-by-side migration, rollback, and uninstall verification
- Performance, memory, battery, startup, capture-latency, and long-running recording acceptance

### Wave 8 - Full-parity release candidate

- Freeze the baseline ledger and contract version for the release candidate.
- Run all shared conformance, platform integration, migration, accessibility, security, packaging, and manual journey suites.
- Resolve every unclassified, unimplemented, expired-waiver, and release-blocking degraded row.
- Publish a signed parity attestation containing the baseline commit, contract version, platform builds, ledger hashes, deviations, and evidence links.

## 21.4 Per-capability implementation packet

Every implementation task generated from the ledger SHALL include:

1. Baseline ledger ID and source evidence.
2. Approved Product Contract and requirement IDs.
3. Data and configuration compatibility impact.
4. Windows, macOS, and Linux implementation ownership.
5. Shared vectors and native integration scenarios.
6. Accessibility, security, privacy, permission, and performance requirements.
7. Failure, cancellation, recovery, and diagnostic behavior.
8. Traceability-manifest updates.
9. Manual journey evidence where automation cannot prove the outcome.
10. Product-owner decisions for any deviation or intentional correction.

An agent cannot close a packet merely because code compiles or the happy path works.

## 21.5 Full-parity release gate

The first release claiming KovaForge-baseline parity SHALL satisfy all of the following:

- The baseline census gate in section 20.8 is closed.
- One hundred percent of in-scope ledger rows are contracted.
- One hundred percent of Required and Equivalent rows are `release-verified` on every applicable platform.
- There are zero `Not implemented`, empty, unknown, or unreviewed platform dispositions.
- There are zero expired waivers and zero release-blocking Degraded or Unavailable dispositions.
- Every intentional retirement and defect correction has human approval, migration notes, and tests.
- Configuration, workflows, uploader settings, history, annotation documents, and supported media migration pass from real sanitized KovaForge fixtures.
- Every first-party destination has contract tests and at least one approved integration-evidence path.
- GUI, CLI, MCP, assistant, daemon, and plugin surfaces claim the same compatible contract version.
- Native accessibility, security, privacy, permissions, recovery, and performance gates pass.
- Install, side-by-side import, upgrade, rollback, and uninstall are verified on each supported OS.
- No production build depends on the local KovaForge checkout or the legacy ImageEditor/VideoEditor submodules.

A product-owner-approved intrinsic platform limitation may remain only when the KovaForge baseline itself cannot provide an equivalent outcome on that platform or the operating system makes it impossible. The parity attestation must state it prominently.

## 21.6 Continuous parity after the first release

After parity is reached:

- Every BriarForge feature starts with a contract change and all-platform disposition.
- Every approved KovaForge baseline advance uses section 20.7.
- CI compares ledger, contract, implementation, and evidence hashes.
- The parity dashboard reports domain completion and regressions; percentages never replace the list of unresolved IDs.
- A released capability cannot regress to an earlier lifecycle state without failing the branch and release gates.

Full parity is therefore a maintained invariant, not a one-time migration milestone.
