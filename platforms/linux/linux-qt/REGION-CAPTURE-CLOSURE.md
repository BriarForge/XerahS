# Linux region capture closure — 2026-11-04

EXTRACTED evidence: production call paths and executed tests below. Candidate
implementation classification does not accept native/physical/human qualification.

Qt topology is frozen before acquisition; each screen has a hashed stable ID,
its own DPR, orientation and physical mapping. Exact bitmap mismatch fails
`capture-topology-mismatch`. Real overlays and the worker compositor preserve
one frozen image and exact cross-display source pixels. **Capture last region**
now validates current IDs/bounds before screenshot admission and reaches the
normal output pipeline. Overlay keyboard instructions state physical units and
Shift steps; visible dashed focus and black/white boundaries work on light/dark
sources. The [source authority note](app/SOURCE-TOPOLOGY-REVIEW.md) records the
Aoife/Mike decision, reference layout provenance and portal limitations.

## Requirement audit

Promoted to implemented candidate: RC-004, RC-005, RC-010, RC-012, RC-014,
RC-017, RC-019, RC-022 and RC-027. Every row names its clause-specific tests in
[traceability](traceability/REGION-CAPTURE-001.json).

RC-003 stays partial: depth is recorded, but Qt does not supply the full
per-monitor primaries/transfer/HDR characteristics. RC-018 stays partial:
configured pixel retention/privacy policy and portal-owned file lifecycle need
a product disposition. RC-013/015 have portal limitation notes only; RC-020 is
untouched. CORE-014 keeps its partial status and records the approved topology
decision. No contract, expected result or waiver changed.

## Files changed

- `src/capture/ScreenTopology.{h,cpp}` (new), `RegionGeometry.h`: Qt Core topology builder, stable identity and metadata/diagnostic definitions.
- `app/ScreenTopology.{h,cpp}` (new), `CaptureController.{h,cpp}`, `SnapshotComposition.cpp`: native sampling, lifecycle/topology fencing, pre-acquisition validation and exact bitmap offsets.
- `app/RegionOverlay.cpp`, `app/features/CaptureFeature.cpp`, `app/ApplicationWindow.cpp`: accessible visible focus and the admitted shared last-region command.
- `src/workflows/CaptureWorkflows.cpp`, `WorkflowExecution.cpp`: saved last-region source and durable stable failure codes.
- `tests/RegionCaptureNativeTest.cpp` (new), `RegionCaptureTest.cpp`, `SnapshotCompositionTest.cpp`, `ShellCommandsTest.cpp`, `PortalScreenshotTest.cpp`: real production-path tests. The portal suite adds frozen confirmation after removal of the borrowed fixture file; its execution is sandbox-blocked.
- `CMakeLists.txt`, `README.md`, `IMPLEMENTATION.md`, `CONTRACT-AUDIT.md`, `app/SOURCE-TOPOLOGY-REVIEW.md`, this report; two traceability maps, four generated progress files and Linux Graphify outputs.

## Verification

Build: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j4` — passes.

Affected targets: `QT_QPA_PLATFORMTHEME= ctest --test-dir build -R '^(region-capture-native|region-capture|snapshot-composition|last-region-store|capture-workflows|conformance|shell-commands|workflow-execution)$' --output-on-failure`.

```text
100% tests passed out of 8
Total Test time (real) =   3.78 sec
```

Full suite: `QT_QPA_PLATFORMTHEME= ctest --test-dir build -j4 --output-on-failure`.

```text
71% tests passed, 47 tests failed out of 163
Total Test time (real) =  60.17 sec
```

116 targets pass. All 47 failed targets depend on sandbox-prohibited D-Bus,
Unix sockets or localhost listeners. Private buses report `Failed to bind
socket ... Operation not permitted`; native-instance/CLI fixtures cannot elect
an IPC primary, and HTTP fixtures cannot listen. The three editor-window
partitions were rerun alone and fail before test startup on the same D-Bus
socket restriction, rather than an editor assertion or load-related flake.
The orchestrator must rerun the full suite outside this sandbox.

Exact blocked CTest names:

```text
editor-print
editor-copy
editor-save
pipeline-journal
capture-history-window
pipeline-history-dialog
capture-pipeline
action-selection
capture-file-output
capture-save-as
capture-annotation
capture-history
capture-paired-save
native-instance
cli-contract
portal-screenshot
native-desktop-appearance
native-editor-preferences
native-window-discovery
annotation-object-navigator
editor-window-1
editor-window-2
editor-window-3
editor-pin
annotation-tool-matrix
editor-wallpaper
editor-upload
native-preference-dialogs
url-download
editor-url-open
editor-open
native-capture-workflows
workflow-shortcuts
secret-service-credential-store
upload-executor
capture-upload
capture-pin-print
portal-color-picker
file-upload
watch-folder-service
capture-conditions
effects-dialog
onboarding
scroll-capture
capture-recognition
capture-before-upload
capture-delete
```

`python tools/platform-progress/update.py`, its `--check`,
`python tools/contract-linter/lint.py` and `git-declan diff --check` pass.
`graphify update .` completed inside `platforms/linux`; extractor syntax/partial
coverage warnings remain navigation limitations, not conformance evidence.

## Handoff

No task commits could be created. `git-declan add` fails:

```text
fatal: Unable to create '/home/mike/Work/BriarForge/XerahS/.git/worktrees/codex-stores/index.lock': Read-only file system
```

The shared Git directory is outside the writable worktree, so staging is also
blocked. Changes remain unstaged on detached base
`e241fddeb3094dc523b65198e5807f4e0661e8cb`. No push, branch switch, stash or
history rewrite was attempted. Aoife must stage/commit the reviewed slice and
run the socket-dependent tests externally. Native physical multi-monitor,
Orca, compositor/permission/privacy/security review remain unverified.
