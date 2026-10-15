# Saved capture workflows — native candidate evidence

Date: 15 October 2026. This implements a usable region-profile subset of
WORKFLOW-CATALOG-001, not acceptance of its 436 workflow/action ledger rows.

## User behavior

Open **Capture workflows…** from the command window or tray. **New** creates a
named region profile. Select actions, optionally enable **Ask which actions to
run after each capture**, choose **Make default**, then **Save changes**.
**Run selected** runs that saved profile; **Capture region**, tray activation and
the existing `--capture-region` command resolve the saved default. Restart keeps
the profiles, selected actions and default. Closing without Save keeps the last
saved configuration. Delete requires confirmation and only persists on Save.

The selectable actions are `annotation`, `save`, `save-as`, `image-clipboard`,
`file-clipboard`, `path-clipboard` and `reveal`. An empty selection is allowed.
Existing PCA ordering, file prerequisites, chooser cancellation, independent
failures, annotation, worker output and explicit retry remain the execution path.
Per-run chooser changes do not rewrite profiles. Save uses the profile
folder and filename pattern with a deterministic preview; blank folder inherits
the standard capture folder. Save As retains its explicit new-PNG picker.
[Output-setting evidence](WORKFLOW-OUTPUTS.md) records validation and versioning.

The controller freezes the selected definition before requesting the screenshot.
Changes during acquisition affect future captures. Retained-action retry uses
the original profile snapshot, pixels and output settings. A busy capture cannot
be replaced by Run selected. Unsupported profiles are refused before acquisition.

## Storage and compatibility boundary

The private native file is
`${XDG_CONFIG_HOME:-~/.config}/com.xerahs.app/linux-qt/capture-workflows.json`.
It uses format `com.xerahs.app/linux-qt/capture-workflows`, version 2 (reads
version 1 and upgrades only on explicit Save), UUID profile
IDs and the existing PCA action IDs. It is not the shared workflow interchange
format and does not read or modify Avalonia settings. Missing storage supplies
the initial **Save and copy image** profile without writing until explicit Save.

Qt Core production logic validates at most 64 profiles, 1–128-character names,
128 action IDs per profile, default membership, UUID uniqueness, JSON types and
the 1 MiB storage limit. Compatible unknown fields/actions/capture types survive
round-trip. Unknown actions are visible as unavailable; unsupported definitions
cannot run. Corrupt/newer/unsafe files are preserved read-only. Revision and
exact-byte checks reject stale writes/runs until Reload. The existing private
atomic storage port supplies locking, permissions and compare-and-replace.

Results and private restart journals include only known profile fields under
`extensions.com.xerahs.app.capture_workflow`; unknown extensions are excluded.
Names and action choices remain private metadata, visible through explicit result
review. Configured empty/clipboard-only runs also receive terminal journal records.
No credentials or unknown extension payloads are copied into the report.

## Verification

Full CTest: **49/49 locally, 70.18 s**; **49/49 cached Qt 6.4.2, 67.04 s**.
All **114 existing published vectors** pass; none were changed. They cover six
other capability packages. No WORKFLOW vectors are published; CMake reports that
absence explicitly and the workflow trace keeps every requirement Required.

`CaptureWorkflowsTest.cpp` verifies validation, unknown preservation, private
atomic save/restart, stale/locked/corrupt refusal and report/journal metadata.
`NativeCaptureWorkflowsTest.cpp` drives actual controls for save/clipboard/ask/
empty profiles, restart/default/Run, cancelled/failed/stale persistence, confirmed
deletion and retained retry after profile changes. Real PNG pixels and terminal
journal files are checked; clipboard publication is injected. Private-portal
controller cases verify both default/explicit snapshots and pre-acquisition
refusal. The complete portal target passes in 8.03 s. A synthetic workflow-manager
frame was inspected. All 32 tooling tests pass. Exact pushed Ubuntu CI is
reported with the checkpoint; previous baseline CI was
[38731722098](https://github.com/BriarForge/XerahS/actions/runs/38731722098).

## Remaining scope and next feature

Per-profile save folders, filename patterns and preview now use existing Core
naming and private atomic persistence. [Explicit-ID CLI activation](WORKFLOW-CLI.md)
now shares the dispatcher and controller with Run selected. [Session global shortcuts](WORKFLOW-SHORTCUTS.md) now provide explicit desktop
binding and shared admission. Next implement workflow discovery and execution/result
linkage, followed by atomic counter reservation. Full catalogue families, recording/audio, uploads/credentials, shared
interchange/migration, full execution identity/status, resource/concurrency
policies and target/human qualification remain Required. Existing Ellipse,
clipboard-assets, export-format, legacy-companion and shortcut decisions remain
pending. This evidence grants no security/privacy/accessibility/parity/release
approval and proves no production-readiness percentage.
