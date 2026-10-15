# Linux contract scope audit

Audit date: 12 October 2026; evidence updated 15 October 2026. Starting implementation: `82ef55e` on `main`.
Source of truth: Product Contract 0.3.1, its capability specifications,
scenarios, published vectors, parity ledgers and [readiness gates](../../../product-contract/READINESS.md).
This is an implementation audit and work ordering record, not an acceptance,
waiver, release approval or reduced completion condition.

The full native Linux application is **unfinished**. The baseline's 1,592
linked discovery rows establish contract routing, not implemented behavior.
Seven capability packages now have Linux requirement trace files. Six have
published vectors and a production conformance adapter; their 114 vectors
cover only those seams. CORE has native/domain evidence and no published
vector file yet.
Neither that vector count nor a green Ubuntu CI run proves the full contract.

The numbered desktop-spec inventory contains 324 requirements across 15
packages. Current trace files label 80 as implemented and 68 as partial; 175 lack
implemented/partial labels in those seven traces, and CORE-029 records the
declared mobile scope outside Linux desktop. These are unweighted trace labels,
not a production-readiness score. Additional partial native candidate maps are
indexed separately until complete capability traces/vectors exist. In particular,
some implemented rows cover core seams only,
and individual catalogue requirements expand into hundreds of ledger items.
No full native production-readiness percentage or release acceptance is proven.

## Capability scope

All desktop capabilities remain in scope. Evidence below identifies the
current implementation surface; remaining work includes the capability's
entire specification and linked ledger rows, not just the examples listed.

| Capability | Current evidence | Requirements still needing implementation or qualification |
|---|---|---|
| `CORE-PLATFORM-001` | All 37 CORE rows traced; native lifetime lock, authenticated bounded activation, real-process failure/recovery and actual-application forwarding tests; native command window sharing tray actions | Complete main application/onboarding, shared profile/activation qualification, localization, notification authority/privacy, updates, migration, credential services, global shortcuts, general capture, recording/audio, media, uploads, plugins, history, automation/watch folders/assistant, diagnostics and distribution remain Required. Native candidate evidence does not accept human reviews or full CORE scope. |
| `FILENAME-GENERATION-001` | `src/naming/`, adapter and 51 vectors | Complete native preference/input integration, filename preview and destination workflow qualification remain Required. Core expansion evidence alone does not accept all native usage paths. |
| `POST-CAPTURE-ACTIONS-001` | `src/actions/`, adapter and 17 vectors; `CapturePipeline` runs native PNG publication and GUI clipboard dispatch through the Core engine, with cancellation, activity, actual artifact metadata and in-memory receipts with shared edit/discard/review controls; native write-ahead records and multi-record restart history preserve acknowledged results and distinguish interrupted unknown actions; explicit native linked retry reuses retained pixels/settings and preserves previous successes; the application and adapter share the canonical result serializer with namespaced task/timing/file/retry facts; a cancellable per-run chooser selects outputs/empty with effective-plan recovery; native file/path clipboard and asynchronous ShowItems reveal consume a live file binding; native Save As retains both file aliases; native rectangle annotation returns edited pixels before output and retains its document for explicit compatible editor persistence, with actual canvas/recovery/retry/process-kill evidence | Full save variants, native action catalogue, configured workflow selection/persistence, annotation/effects/output handoffs, uploads, complete catalogue/prerequisite retry, full durable media/artifact recovery, configured retention/pagination, crash cleanup authority, shared persisted compatibility, privacy and consent qualification remain Required. |
| `REGION-CAPTURE-001` | `src/capture/`, 21 vectors; `PortalScreenshot`, `CaptureController`, `RegionOverlay`; native portal lifecycle/failure tests, worker composition with accessible progress and responsive cancellation, independent pixel fixtures and stale-worker isolation tests | The controller assumes a uniform desktop scale; per-monitor physical source mapping, rotation/colour/availability, mixed displays, cursor settings, HDR conversion, interactive handles/snapping/magnifier, persisted last-region invocation, physical accessibility and performance remain Required. |
| `EDITOR-SESSION-001` | Document/sidecar, source binding, rectangle history/rendering, native editor, 19 vectors; owned save worker acknowledges its exported state and explicit companion repair preserves PNG/Redo | Full contracted annotation/effect/canvas/output integration, complete persistence compatibility, native parity, visual goldens and human reviews remain Required. |
| `EDITOR-ANNOTATIONS-001` | Explicit native Select/Rectangle/Crop modes, UUID-based keyboard object selection, numeric Rectangle creation with frozen defaults/state/selection, eight pointer resize handles and absolute numeric/color/alpha properties, frozen group pointer movement and native group rotation drafts, guarded stroke/fill choices with local capture defaults and saved editor defaults, gesture cancellation and finite atomic geometry/history; Rectangle creation/selection/style/rotation/order and protected direct edits; native rectangle duplication/object Cut/Copy/Paste with owned MIME, bounded validation, fresh IDs and exact history | No complete EA trace. All 20 stable tools, curves/freehand/text/balloons/steps/image/emoji/cursor, redaction/magnifier/spotlight/eraser/cut-out, complete multi-tool/asset/shared clipboard and duplication, complete persistence and accessible tool/option/handle controls remain Required. |
| `EDITOR-EFFECTS-001` | General canvas pixel preparation/history exists | No complete EE trace or effect catalogue implementation. All 232 selected stable effect IDs, parameter semantics, preview/Apply, resource/colour/alpha/randomness rules, presets/favorites, cancellation and visual evidence remain Required. |
| `EDITOR-CANVAS-001` | Native New/Open, zoom/pan, crop/auto-crop, resize/rotate/flip, represented-annotation flatten/clear and comparison; three vectors | Inserted images, background composition, general affine annotation transforms, complete flattening and native format/colour/resource/interaction qualification remain Required. |
| `EDITOR-OUTPUT-ACTIONS-001` | PNG Save/Save As freeze an owned snapshot on a cancellable worker with guarded atomic publication, true partial receipt and explicit companion-only repair; Save As adopts its path only after full success. Native Copy Image offers one immutable sRGB/alpha image plus PNG with GUI acknowledgement, explicit unknown delivery and confirmed known-failure replay; native Print adds explicit page/margins/scale/orientation/colour preview, native printer confirmation and worker submission with truthful uncertainty, all preserving document/history/files | No complete EO trace or published EO vectors. Full immutable output catalogue/background integration, JPEG/WebP quality/alpha/metadata choices, permission/ACL/disk-full/shutdown/durability and physical accessibility qualification, complete clipboard/paste-target/color qualification, uploads, full print/printer qualification, pinning, result publication and destructive/exit consent qualification remain Required. |
| `EDITOR-SETTINGS-001` | Private atomic store, recents, creation-only border/fill/thickness, appearance, window state and toolbar controls; three vectors plus native tests | Complete field ledger/default/migration/downstream coverage, fonts/other tool defaults, numeric/effect settings, recent/favorite effects, convenience/automation preferences, independent whole-file recovery, shared semantics and physical/human reviews remain Required. |
| `EDITOR-UTILITIES-001` | Canvas comparison support covers only a related image operation | No complete EU trace. Contracted utility tools, deterministic local processing, resource policy, progress/cancellation, accessibility and output integration remain Required. |
| `SETTINGS-CATALOG-001` | Editor preference subset and inactive unknown preservation | No complete SETCAT trace. All 922 settings rows, profiles/scopes, shared schema, secure credential references, import/export/reset preview, full validation/migration and approval remain Required. |
| `WORKFLOW-CATALOG-001` | Default save/clipboard capture, per-run native action selection and isolated pipeline model | No complete WFCAT trace. All 436 workflow/action rows, versioned definitions, consistent entry points, execution identity/results, concurrency, cancellation, retry and privacy/permission integration remain Required. |
| `INTERFACE-CATALOG-001` | Small native tray/editor/CLI subset | No complete IFCAT trace. All linked GUI/CLI/MCP surfaces, semantic equivalence, availability, versioned progress/error/cancellation and keyboard/assistive-technology qualification remain Required. |
| `COMPATIBILITY-CATALOG-001` | `.xann` parsing/round-trip and current unknown editor preference preservation | No complete COMPCAT trace. Configuration/workflow/uploader/history/SXCU/plugin/effect/automation compatibility, isolated previewed imports, secret handling, resource validation, idempotent migration and rollback/goldens remain Required. |
| `MOBILE-PLATFORM-001` | Its explicit scope is Android/iOS | Mobile requirements are not Linux desktop requirements; CORE-029 and mobile-scope rules remain authoritative. This does not waive any desktop capability or its cross-platform acceptance gate. |

## Linux targets and release gates

The supported `linux-qt` profile names Ubuntu LTS, Fedora Workstation and Arch
Linux; GNOME, Plasma and wlroots environments; Wayland first and an X11
fallback. Ubuntu 24.04/Qt 6.4.2 CI and local Arch/Qt 6.11.2 tests provide useful
build/domain evidence. Complete distro/compositor/native integration matrices,
including permission rejection, unavailable mechanisms, screen locking,
scaling, display removal and accessibility, remain Required.

The candidate Hyprland edition has a confirmed toolkit; GNOME/KDE candidate
toolkits remain proposed. Candidate status does not permit narrowing the
supported reference edition or self-approving an edition promotion. No accepted
Degraded/Unavailable disposition is inferred from missing code.

Only the desktop-file packaging surface exists here. Verified `.tar.gz`,
`.deb`, `.rpm` and AUR distribution, dynamic Qt/GPL obligations, provenance,
clean installation, explicit replacement migration, updates, rollback/recovery
and uninstall remain Required. Live Avalonia configuration must stay untouched
until an explicitly authorized migration.

Readiness still requires census closure/product-owner signature, named owners,
shared visual goldens/tolerances, compatibility observations, conformance/parity
acceptance and human security, privacy, accessibility, licensing and release
reviews. These approvals remain human work; agents can prepare evidence and
reviewable candidate code. No requirement becomes accepted merely by being
marked implemented in a core trace file.

## Next production work

The 14 October annotation-transfer checkpoint extends editor Save, paired capture persistence,
annotation handoff, Save As/multiple-file results, journal/recovery, linked retry
and per-run selection with owned worker output, explicit editor companion repair,
non-mutating native clipboard output and native Print and annotation transfer with guarded known/unknown delivery.
It passed all 43 local CTest targets
(native tests and all 114 published vectors for the six implemented vector
packages), contract lint/backlog checks, their 25 unit tests and the seven
conformance-runner unit tests. The production-path real-process fixtures use
private buses and temporary files/stores. Exact pushed-revision Ubuntu
24.04/Qt 6.4.2 evidence is supplied by the repository's Product Contract CI;
neither these tests nor that CI accepts the outstanding native/human gates.
The [journal evidence](app/JOURNAL-RECOVERY.md) indexes verified behavior and
concrete retention, crash-temporary and shared recovery-format decisions.
The [retry evidence](app/RETRY-POLICY.md) records actual native eligibility,
append-only results, file provenance, cancellation and remaining retry scope.
The [Save As evidence](app/SAVE-AS.md) records actual new-PNG picking/publication,
multiple artifacts, Save-first precedence, retained-pixel retry and killed-process
recovery. Concrete replacement/format/paired-sidecar/shared-schema decisions
and the supported native/human gates remain Required.
The initial Save As baseline run exposed Qt 6.4 focused-field selection
behavior in a fixture. Corrected focus/selected-path/Save-control fixtures and
standard directory navigation retain the rejection/path assertions, with
isolated Qt 6.4.2 reproduction and native verification indexed in SAVE-AS.md.
The [annotation handoff evidence](app/ANNOTATION-HANDOFF.md) indexes actual
before-save canvas/rendering, frozen styles, retained editable documents,
explicit compatible editor persistence, output retry and killed-process recovery.
This is partial native rectangle coverage. The [paired-save evidence](app/PAIRED-SAVE.md)
adds explicit Save/Save As companions with true per-artifact failures, known-PNG
consumption, companion-only repair and known-PNG/unknown-companion restart
review. Full persistence/default/removal/format policy, tool/workflow/native/human
scope remain Required. The [editor-save evidence](app/EDITOR-SAVE.md) indexes
immutable worker snapshots, responsive cancellation, exported-state checkpoints,
true split outcomes, explicit companion repair, failed Save As path preservation,
future-major/old-destination preservation and owner/callback lifetime tests.
Affected fixtures also run against cached Qt 6.4.2. Human permission/ACL policy,
path-race/power-loss/process-death/full-shutdown and native disk-full qualification
remain Required; no durable editor retry authority is inferred from this live receipt.
The [editor-copy evidence](app/EDITOR-COPY.md) indexes actual image/PNG/alpha
offers, worker/GUI separation, frozen snapshots, Cancel/owner destruction,
unknown ownership/foreign replacement, explicit known-failure replay, Ctrl+C
text precedence and unchanged dirty/history/viewport/settings/files. The native
toolbar now has 48 current commands while retaining its default layout.
EO-004-LINUX-2026-10-14 remains pending; Copy is independent specified output
work, not a decision on those format defaults. Complete physical clipboard,
color/alpha, shutdown, accessibility and privacy/security qualification remain Required.

The [editor-print evidence](app/EDITOR-PRINT.md) and its machine-readable candidate
map index real preview controls, immutable worker preparation, private Qt PDF
output observed through Poppler, cancellation/owner lifetime, fresh-confirmation
known-failure replay and unchanged document/history/Redo/files/preferences/clipboard.
All printer/permission/queue/device/colour/shutdown/physical accessibility and
native target/human acceptance remain Required. The minimum Qt baseline is
unchanged; PrintSupport is dynamic and Poppler is test-only.

The [annotation transfer evidence](app/ANNOTATION-TRANSFER.md) and candidate
requirement map index actual native rectangle duplication/object clipboard, new
identities, viewport-visible offsets, atomic history/selection/checkpoint/Redo,
protected direct edits and owned source/file/asset/default preservation. Native
Cut requires acknowledged publication and keeps unknown outcomes unchanged.
EA-016-LINUX-2026-10-14 remains pending for shared MIME/canonical/version rules,
selected asset references/projection/collision handling, unknown/lock fields,
offset/resource policy and deciding vectors. Asset-bearing clipboard copies are
refused; local Duplicate preserves existing assets. Full tools, compatible
clipboard/assets, target/resource/privacy/security/a11y/human gates remain Required.

1. Continue full native capture/operation qualification under
   RC-001/002/008/016/018 and CORE-004/005. The pending Screenshot lifecycle is
   now cancellable, owner/handle fenced and bounded, with isolated production
   transport/controller evidence. Confirmed pixel composition now runs on a worker with responsive cancellation, bounded pixels and accessible progress; native PNG encoding/publication and save/clipboard pipeline dispatch now also run on a worker with reachable cancellation and retained receipts; shared recovery controls now edit retained pixels and explicitly discard the current receipt; native multi-record restart result review now uses private write-ahead journals with explicit unchanged-record forgetting; explicit native retry now reuses failed save/clipboard context with append-only lineage, actual file provenance and cancellation; full media/artifact recovery, catalogue/prerequisite retry, retention/pagination, crash cleanup and complete pipeline qualification remain unfinished. Core startup now also has lifetime ownership,
   authenticated activation/failure recovery and a real command window. Native
   candidate code still requires shared/profile/physical/human qualification.
2. Remove the controller's uniform-scale source assumption through a specified
   physical-source backend and verify per-display mapping/topology/colour, then
   complete last-region storage, cursor/settings and overlay interactions.
3. Continue configurable native workflows and the complete post-capture action
   catalogue alongside recording, uploads/credentials/history/integrations and
   the remaining editor tools/effects/canvas/output/settings/utility features.
   Native per-run action selection now uses the same engine, cancels before
   output mutation, preserves defaults and records the effective plan before
   output intent. The [selection evidence](app/ACTION-SELECTION.md) indexes
   actual native keyboard/I/O/recovery behavior and remaining qualification.
   Native saved-file clipboard/path/reveal now validates live publication identity,
   resolves original save prerequisites in explicit retry and leaves uncertain
   reveal delivery interrupted. The [file-output evidence](app/FILE-OUTPUTS.md)
   records actual native behavior, path-race limits and remaining human/native
   qualification. Native Save As now retains both files, checks cancellation at
   safe publication boundaries and preserves earlier success in retry/recovery.
   Native annotation now returns accepted rendered pixels before save and
   retains a re-editable source document for explicit compatible editor Save.
   Explicit sidecar intent and truthful companion-artifact outcomes now enter
   capture Save/Save As under ES-011/012/014/030. Explicit editor Save now uses
   a bounded immutable worker-output and companion-repair model under
   EO-001/003/005/006/014/015 and ES-011/012, including guarded replacement/
   supported-companion authority, cancellation and retained dirty-state evidence.
   Next implement explicit PNG/JPEG/WebP save options under EO-004/005/006.
   Resolve EO-004-LINUX-2026-10-14, recorded in EDITOR-SAVE.md for the product owner,
   covering draft format selection, defaults, alpha flattening, quality and profile
   policy, while continuing decided behavior and independent contract work.
   Native Copy Image now renders one owned snapshot and preserves document state
   through cancellation/failure/replay under EO-001/002/014/015. Frozen
   multi-object pointer movement and native rotation drafts now guard previews/
   commits against stale document and selection under EA-003/017/018. Native
   stroke/fill choices now guard state/selection/defaults/generation/preferences
   and owner lifetime, preserving alpha, selected/default semantics, history and
   truthful preference failure under EA-014/017/018/019/020 and ESET-006/007/015.
   Numeric Rectangle drafts now fence generation and use an owned nested alpha
   picker; matching-state replacement and owner destruction preserve committed
   state. Thickness now guards document/default admission and editor publication;
   capture controls keep defaults local with a visible numeric value. Four guarded
   ordering commands now expose native editor/capture controls and canvas-local
   candidate catalogue gestures, retaining group order and native text/list
   precedence. Guarded deletion now adds capture Delete and fences native state/
   selection/generation with exact group history, output and teardown evidence.
   Guarded keyboard transforms now retain steps/repeats, frozen admission,
   exact group history and actual output, with pending Close refusal. Next migrate
   the main editor Crop routes to the now-verified owned preview with frozen
   state/selection/generation and worker/close/owner guards,
   while resolving Ellipse schema/rasterization clarification
   EA-005-LINUX-2026-10-14 in ANNOTATION-TOOLS.md, full annotation/asset clipboard
   under EA-016/017 and native output
   service handoffs and full Print qualification under EO-008/014/015 while
   resolving the format policy. Continue all
   annotation tools, full workflow configuration, remaining save variants and
   media/artifact recovery alongside that work.
4. Maintain requirement-level traces and meaningful failure/consent evidence;
   qualify the complete supported target matrix and prepare all human gates.

Each checkpoint preserves the full scope above. A final completion audit must
prove every applicable requirement and ledger row against production behavior,
meaningful tests and accepted review/release evidence.

The [annotation tool evidence](app/ANNOTATION-TOOLS.md) and its candidate machine map index actual checked Select/Rectangle/Crop modes, accessible instructions, capture controls, cancellation, atomic geometry/rounded-no-op refusal and exact pre-command selection recovery. Existing small-screen placement assertions are retained. EA-005-LINUX-2026-10-14 records the missing shared Ellipse schema and coverage/stroke decision; no recognized Ellipse payload or golden pixels are invented. All EA requirements remain Required, and the trace-label counts and full scope above are unchanged.

The [rectangle edit evidence](app/RECTANGLE-EDIT.md) and candidate machine map index
eight native pointer handles, fixed opposite-point geometry at arbitrary rotation,
copied previews and one release commit, shared numeric/color/alpha properties,
exact undo/redo and creation-default/asset/source/unknown-field preservation.
Actual save/reopen, capture Apply/Cancel/publication and owner-lifetime fixtures
remain private. Initial viewport fitting now uses the actual layout. Full
border/corner/shadow/tool/resource/target/a11y/privacy/security/human qualification
remains Required; the full scope and trace-label counts above are unchanged.

The [keyboard Rectangle creation evidence](app/RECTANGLE-CREATION.md) and candidate
machine map index native numeric bounds, read-only frozen style, Tab/Alt+R/F9 and
capture Tab-Space input, one guarded Core history insertion and pointer style
snapshots. Stale/invalid/cancel/owner refusal, previous selection recovery and
actual temporary save/reopen/publication are covered. Full tools, field/schema,
resource/native/a11y/privacy/security/parity/human qualification remain Required;
no shared expected vectors, contract scope or trace-label counts changed.

The [native object selection evidence](app/ANNOTATION-OBJECTS.md) and candidate
machine map index shared single/range/toggle/all/clear keyboard selection, owned
UUIDs and plain labels, exact pointer/history/order/source synchronization and
stale/busy/lifetime refusal. Main/capture input and actual temporary output and
persistence preserve source, IDs, compatible unknown data and selection-only
history/checkpoint behavior. Full tool/field/resource/performance/native/a11y/
privacy/security/parity/human qualification remains Required; scope and trace-label
counts are unchanged.

The [frozen group move evidence](app/ANNOTATION-MOVE.md) and candidate machine map
index owned source-pixel previews, shared translation/finite validation, guarded
one-operation release, exact history/selection and cancellation/stale/protected/
no-op/overflow/lifetime refusal. Actual native keyboard selection, editor
PNG/.xann save/reopen and capture publication retain source, IDs, styles, assets
and compatible unknown data. Full tools/fields/resource/performance/target/a11y/
privacy/security/parity/human qualification remain Required; full scope and
trace-label counts above are unchanged.

The [guarded rotation evidence](app/ANNOTATION-ROTATION.md) and candidate machine
map index owned per-object angle previews, shared Core finite/normalized geometry,
one-operation history and frozen state/selection/generation commits. Main/capture
native input, preview/cancel/invalid/stale/no-op/protected/lifetime refusal and
actual temporary output/persistence preserve complete compatible documents,
source, fields, defaults and assets. Full tools/fields/resource/performance/target/
a11y/privacy/security/parity/human gates remain Required; scope and trace labels
remain unchanged.

The [guarded colour evidence](app/ANNOTATION-COLOR.md) and candidate machine map
index the shared owned alpha picker, frozen state/selection/defaults/generation/
preference revision, one-operation history and default-only/no-op behavior.
Actual editor save/reopen and canonical preference restart preserve compatible
documents; capture publication uses independent alpha samples and leaves editor
preferences untouched. Stale/protected/geometry/cancel/owner/dialog/publication
failure and locked preference persistence are exercised. Full tools/fields,
resource/performance/native matrix/a11y/privacy/security/parity/human/release gates
remain Required; scope and trace-label counts above are unchanged.

This colour checkpoint passed all 44 local CTest targets in 42.26 seconds,
including the unchanged 114 published vectors, all 32 tooling unit tests and
contract lint/backlog checks. The six affected CTest targets also passed against
cached Qt 6.4.2 in 18.24 seconds. The checkpoint report verifies Ubuntu 24.04 CI
on the exact pushed SHA. These are candidate implementation checks, not human
acceptance or full Linux completion.

The [numeric Rectangle draft evidence](app/RECTANGLE-DRAFTS.md) and candidate
machine map index document-generation fences for creation/properties, including
replacement with equal history state/UUID selection and replacement during a
pending notification. Owned nested alpha pickers change only properties drafts
and survive dialog/editor/canvas destruction. Stale/Cancel/refusal preserves
original/replacement state, defaults, checkpoints, Redo and files; fresh editor
save/reopen and actual original-capture publication recover correctly.
This checkpoint passed 44 local CTest targets in 42.20 seconds, 114 unchanged
published vectors and all 32 tooling unit tests; the six affected targets passed
against cached Qt 6.4.2 in 18.50 seconds. Contract lint/backlog/effective instruction
checks and exact pushed Ubuntu 24.04 CI are reported with the checkpoint. Full
scope, trace labels, native/resource/a11y/privacy/security/parity/human/release
requirements remain unchanged and Required. The subsequent thickness checkpoint
adds atomic document/default admission and native publication/lifetime guards.

The [thickness evidence](app/ANNOTATION-THICKNESS.md) and candidate machine map
index guarded complete document/default admission, frozen native state/selection/
defaults/generation/preferences, one-step history and no-op/default-only behavior.
Protected/unsupported/invalid/stale batches preserve defaults and committed state;
preference failures keep accepted edits undoable with honest diagnostics, and
publication observers may destroy the owner safely. Capture controls show a
numeric future-default value and preserve private editor preference bytes/revision.
Actual editor save/reopen/restart and capture PNG/receipt retain source, fields,
assets and unknown data. Verification passed 44 local CTest targets in 45.09
seconds, 114 unchanged vectors and 32 tooling tests, plus six affected Qt 6.4.2
targets in 21.28 seconds. Contract lint/backlog/effective instructions and exact
pushed Ubuntu CI are reported with the checkpoint. Full scope, trace labels and
native/resource/a11y/privacy/security/parity/human/release gates remain Required.
The subsequent [ordering checkpoint](app/ANNOTATION-ORDER.md) supplies those four
guarded native commands, canvas-local gestures, protected native text/list
precedence and real history/persistence/publication evidence. The catalogue
approval and physical/native/human gates remain Required.


The [ordering evidence](app/ANNOTATION-ORDER.md) and candidate machine map index
four guarded editor/capture controls, frozen state/selection/generation dispatch,
one-step exact history and independently declared single/group/opaque array
orders. Opaque payload, source, assets, fields and preferences survive actual
editor PNG/.xann reopen and capture publication with independent overlap pixels.
Empty/protected/no-op/stale/invalid/reentrant/owner refusal preserves committed
state and files. Canvas-local candidate catalogue gestures and bracket aliases
share admission; custom navigation yields to native list/text/modal/busy input.
The Qt Page-key canonicalization regression is covered on both Qt versions.
This checkpoint passed 44 local CTest targets in 44.38 seconds, 114 unchanged
published vectors and 32 tooling tests, with eight affected Qt 6.4.2 targets in
21.35 seconds. Lint/backlog/effective instructions and exact pushed Ubuntu CI
are reported with the checkpoint. The native registry has 52 commands with
fourteen visible defaults unchanged. Trace statuses/dispositions, full scope and
native/resource/a11y/privacy/security/parity/human/release gates remain unchanged
and Required; the shortcut catalogue still awaits product-owner approval.
The subsequent [deletion evidence](app/ANNOTATION-DELETE.md) supplies native
state/selection/generation guards, capture Delete and group/history/persistence/
teardown coverage; full scope and approvals remain Required.


The [deletion evidence](app/ANNOTATION-DELETE.md) and candidate machine map index
single/mixed/opaque/all deletion, whole-group protection, exact Undo/Redo and
reachable assets/defaults. Native admission freezes state/selection/generation
before busy/pending notifications. Empty/protected/stale/reentrant/owner refusal
keeps current state and files; editor menu/keys/custom hotkeys and capture native
Button/input/list/history/publication are exercised. Actual remaining PNG/.xann
reopen retains source/fields/assets; all-object explicit save follows ES-014
removing the stale companion while retaining raster and owned session/receipt.
Weak refresh guards contain action/model/title/status and capture history owner
teardown. The deliberate status callback exposed a QStatusBar traversal crash;
post-traversal notification fixes the tested path on Qt 6.11.2 and 6.4.2. Only
private fixtures were involved; system config and real user data remain untouched.
Verification passed 44 local CTest targets in 45.56 seconds, 114 unchanged vectors
and 32 tooling tests, plus nine affected Qt 6.4.2 targets in 23.49 seconds.
Lint/backlog/effective instructions and exact pushed Ubuntu CI are reported with
the checkpoint. Registry/default layout and requirement scope/dispositions remain
unchanged. ES-014 is now honestly labelled partial because the baseline-alias
archive/reconciliation policy is unresolved. Full scope, all native/resource/a11y/privacy/security/parity/human/
release gates and draft shortcut-catalogue approval remain Required.
The subsequent [keyboard evidence](app/ANNOTATION-KEYBOARD.md) adds guarded
move/resize/quarter-turn admission, preserved steps/repeats and exact native
group history/output. Native capture Crop now supplies owned preview/history/source-proof publication,
indexed in [CAPTURE-CROP.md](app/CAPTURE-CROP.md). Main-editor Crop now shares that owned preview; see [EDITOR-CROP.md](app/EDITOR-CROP.md).
Main-editor owned history is now indexed in [EDITOR-HISTORY.md](app/EDITOR-HISTORY.md);
next migrate the remaining canvas worker paths.

The new [ES-014/030 clarification](app/EMPTY-SIDECAR-CLARIFICATION.md) records
one deleted annotation reappearing from an intact baseline alias after a formerly
successful default-companion removal. Candidate Core/native persistence now
reports sidecar-save-failed for known or late aliases, preserving companions,
raster and owned zero-annotation state with dirty/retry evidence. Archive or
reconciliation with confirmation/rollback requires the pending product-owner
policy and full privacy/security/compatibility review; no policy is inferred from
continuation. Other contracted work continues independently.


The [keyboard transform evidence](app/ANNOTATION-KEYBOARD.md) and candidate map
index actual one/ten-pixel move/resize, both quarter turns, held-key repeat,
whole-group finite/protection admission, exact history/default/source/assets,
stale selection/state/generation and pending/rejection/completion owner failure.
Real editor/capture PNG/.xann reopen and independently sampled pixels retain
accepted edits; capture Cancel/destruction keeps the original working image.
The new clean-window pending Close regression now refuses active canvas
interaction. Text/list/modal and gated native-save focus/busy/recovery are
verified. Full local verification covers all 44 targets: the full run passed 43
and, after a redundant-selection fixture correction, the complete editor-window
retest passed in 14.70 seconds. The unchanged 114 vectors and 32 tooling tests
pass; seven affected Qt 6.4.2 targets are verified, with the final editor-window
retest in 13.92 seconds. The candidate map records the sequences and fixture/Close
findings honestly. Contract lint/backlog/effective instructions and exact pushed
Ubuntu CI are reported with the checkpoint. Prior deletion commit 9d47c34 has
verified successful CI run 38654736774: 44 Ubuntu 24.04/Qt 6.4.2 tests in 46.01
seconds, plus lint success.

Requirement IDs/statuses/dispositions/vector references, 52 registered commands,
fourteen visible defaults and full scope remain unchanged. All native/resource/
a11y/privacy/security/compatibility/parity/human/release gates and the recorded
Ellipse/clipboard-assets/output-format/legacy-companion/shortcut decisions remain
Required or pending as recorded. Native capture Crop now supplies that owned
preview/history/source handoff; main-editor Crop now shares that preview; owned source-changing history is now indexed in [EDITOR-HISTORY.md](app/EDITOR-HISTORY.md).
The next step is migration of remaining canvas worker paths.


[Native capture Crop](app/CAPTURE-CROP.md) now supplies focusable pointer/numeric
controls, owned retained-region preview and one guarded Core history commit.
Crop-region normalization factors the existing canvas rules; unknown geometry
refuses atomically. Private session lineage composes crops through exact history
without changing shared document JSON. The capture worker independently reproduces
that region from original source pixels and checks PNG/dimensions before accepting
changed working media. Source-changing history prepares source/private display
on a cancellable owned worker before admission. Actual objects/assets/alpha,
viewport/selection/history, no-op/Cancel, source-proof tampering, nested/global
cancel/owner recovery, real PNG/.xann publication and failed-save retry are verified.
Preview and final native capture frames were inspected using only synthetic data.

A private coordinator-destruction fixture exposed a child recovery callback after
the parent's canvas deletion (SIGSEGV, PID 4054432, 15 October 11:11:55 AWST).
Closing/disconnection guards fix the tested path on both Qt versions. Child-only
recovery and asynchronous-focus/default fixture corrections are recorded in the
candidate map; no core memory copy or system change was made. Full local
verification covers 44 targets: 43 passed in the full 53.54-second run and the
corrected complete capture target passed in 9.72 seconds. The 114 unchanged
vectors, 32 tooling tests and ten affected Qt 6.4.2 targets pass; its final capture
retest took 9.39 seconds. Contract lint/backlog/effective instructions and exact
pushed Ubuntu CI are reported with the checkpoint. The prior keyboard commit
1f07fb9 has verified successful CI 38671878807: 44 Ubuntu 24.04/Qt 6.4.2 tests
in 49.39 seconds plus lint success.

Trace IDs/statuses/dispositions/vector references, 80 implemented/68 partial core
labels, 52 registered editor commands and fourteen visible defaults remain
unchanged. Full tools/fields, effects, canvas/output, capture topology, recording/
audio, uploads, workflows/settings/catalogues, application/history/automation/
compatibility, distribution/migration/updates/rollback, supported targets, resource/
performance, privacy/security/a11y/parity/human/release gates and recorded product
decisions remain Required or pending. Main-editor Crop now shares that preview, as recorded below. Owned main-editor source history is now indexed in [EDITOR-HISTORY.md](app/EDITOR-HISTORY.md).
Next migrate remaining canvas operations to owned worker preparation.


[Main-editor Crop](app/EDITOR-CROP.md) now sends numeric bounds and reverse pointer
drags through the shared owned preview. Frozen document/source/state/exact
selection/generation precede busy notifications. One accepted crop publishes the
prepared source/private display, preserving fields/assets and exact surviving
selection/history/checkpoints; full-image no-op and Cancel preserve Redo. Native
keyboard controls/roles, Crop after inverse rotations, alpha/profile/pixels,
unknown/stale refusal, reentry, blocked-worker Cancel/child/owner destruction,
busy-admission/final-status owner deletion and actual PNG/.xann reopen are verified.
No source decode/conversion repeats on the GUI thread at crop admission. The
shared native editor-canvas module owns the dialog, preserving Core-only logic,
dynamic Qt and existing shared compatibility. The inspected synthetic preview
explicitly explains that Undo and embedded assets can retain earlier pixels.

All 44 local CTest targets passed in 55.12 seconds, including the unchanged 114
published vectors. The 15 focused native Crop cases (17 with setup/cleanup) pass;
32 tooling tests and ten affected Qt 6.4.2 targets (38.77 seconds) pass.
Instruction/lint/backlog verification and exact pushed Ubuntu CI are indexed in
the candidate evidence/checkpoint report.
The previous capture Crop commit 4f9e612 has verified successful CI 38676044414:
44 Ubuntu 24.04/Qt 6.4.2 tests in 48.28 seconds plus lint success. Focused fixture
corrections account for expected save-time binding changes and the actual JSON
dimension key; shared vectors and production behavior were not altered to fit.

Requirement IDs/statuses/dispositions/vector references, 80 implemented/68 partial
core labels, 52 registered editor commands and fourteen visible defaults remain
unchanged. All full application/capture/recording/audio/upload/workflow/settings/
editor/effects/history/automation/compatibility/packaging/migration/updates/rollback
scope, supported targets, resources/performance and human privacy/security/a11y/
parity/release gates remain Required. Recorded product decisions remain pending;
no production-readiness percentage is established. The following checkpoint implements owned main-editor source history. Next migrate
remaining canvas operations to owned preparation and frozen admission.


[Owned main-editor Undo/Redo](app/EDITOR-HISTORY.md) now copies its source-changing
history target before busy notifications and validates/decodes/normalizes source
and private display off the GUI thread. Guarded Core admission freezes current
state, exact selection and target history ID; native admission also freezes
canvas generation. A replaced Redo branch is refused even after an intervening
edit and Undo return to matching current state/selection. Core restores its exact
document, source/identity/fields/assets/selection and checkpoints; prepared source/
display publication avoids GUI-thread source decoding. Annotation-only Undo/Redo
uses guarded restoration without an image worker.

Native Ctrl+Z/Ctrl+Y, alpha/Display P3 source history, saved-before/Crop/Undo
checkpoints, actual PNG/.xann reopen, preference/file isolation, invalid snapshots,
reentry, stale state/selection/generation, worker failure and both directions of
Cancel/Escape/child/owner destruction are verified. Blocked preparation does not
hold cancellation or forced teardown. Progress cleanup detaches the child before
destruction observers can delete the parent; final-status/busy-owner deletion is
also covered. The inspected synthetic native progress exposes readable text and
Cancel with checked accessibility names/role. Physical/native/human acceptance
is still Required.

All 44 local CTest targets pass in 61.58 seconds, including the unchanged 114
published vectors; all eleven affected Qt 6.4.2 targets pass in 46.77 seconds.
Seven new Core and 28 new native rows are included, plus 32 passing tooling tests.
The first focused keyboard fixtures needed active-parent/canvas focus after Crop;
final complete editor-window targets pass on both Qt versions with no timeout or
shared expected-result change. Compilation also corrected QList UUID fixture
calls. Exact pushed Ubuntu CI and lint/backlog/effective instructions are recorded
in the candidate evidence/checkpoint report. Prior main-editor Crop commit
c4faa5c has verified successful CI 38679232412: 44 Ubuntu 24.04/Qt 6.4.2 tests in
50.77 seconds and lint success.

Requirement IDs/statuses/dispositions/vector references, 80 implemented/68 partial
core labels, 52 registered commands and fourteen visible defaults remain
unchanged. All full application/capture/recording/audio/uploads/workflows/settings/
editor/tools/effects/canvas/output/history/automation/compatibility/packaging/
migration/updates/rollback scope, supported targets, resources/performance and
human privacy/security/a11y/parity/release gates remain Required. Product decisions
remain pending; no production-readiness percentage or release acceptance is
established. Next migrate remaining main-editor canvas operations from stack-owned
future/progress paths to owned preparation and frozen state/selection/generation
admission, with nonblocking cancellation/owner teardown and unchanged geometry,
source/profile, exact one-step history and actual persistence.


[Owned canvas mutations](app/CANVAS-WORKERS.md) now prepare immutable source/
document/operation snapshots and private displays on workers, with progress and
nonblocking Cancel/Escape/child/owner recovery. The common native preparation
helper also serves existing history restoration. Frozen state/exact selection/
generation precede configuration and busy notifications; guarded Core commit
preserves atomic history, surviving selection and reachable fields/assets.
Publication sets source/cache/private display before refresh, avoiding GUI-thread
decode/conversion. Untagged working source stays untagged with the old private
sRGB assumption; Display P3 working source/profile is retained. Geometry, raster,
unknown-object and output policies remain unchanged.

Custom rotation now owns preview work without dialog captures or destructor joins.
Only the latest complete angle/interpolation/expansion/fill draft can apply. Numeric
resize/Auto Crop, colour pickers, clear confirmation and errors use owned dialogs;
cleanup detaches before destruction observers can delete the parent. Clear keeps
Save/Discard/Cancel and rechecks frozen admission before/after explicit Save.
Existing incomplete-save recovery and all other native history/capture cases pass.

Four new Core and 43 native cases cover twelve actual commands with independent
pixels/profile/history/PNG/.xann expectations, untagged private-view interpretation,
stale/failure/reentry, blocked-worker cancellation/teardown and configuration owner
recovery. Ready rotation and operation-progress synthetic frames were inspected.
The full local run passed 44 targets in 62.94 seconds; after the view-tag/retry-text
review, the final affected editor-window retest passed in 26.10 seconds. Eleven
Qt 6.4.2 targets passed in 48.63 seconds and its final editor retest passed in
25.07 seconds. Unchanged successful targets were retained; the 30-second editor
timeout was not increased. The 114 unchanged vectors and 32 tooling tests pass.
Instruction/lint/backlog and exact pushed Ubuntu CI evidence are reported with
the checkpoint. Prior history commit 302066e has verified successful CI 38681983130:
44 Ubuntu 24.04/Qt 6.4.2 tests in 58.97 seconds, plus lint success.

Requirement IDs/statuses/dispositions/vector references, 80 implemented/68 partial
core labels, 52 registered commands and fourteen visible defaults remain unchanged.
The full application/capture/recording/audio/upload/workflow/settings/editor/tools/
effects/canvas/output/history/automation/compatibility/distribution/migration/
updates/rollback scope, target matrix, resources/performance and human privacy/
security/a11y/parity/release gates remain Required. Product decisions remain pending;
no completion percentage or release acceptance is established. Next migrate
ImageComparisonDialog loading/previews to owned immutable workers and nonblocking
cancel/owner recovery, retaining validated file snapshots, alignment/divider controls
and editor-history isolation; New/Open and capture-history ownership qualification
also remain Required.

[Owned comparison loading](app/COMPARISON-WORKERS.md) now freezes source/
annotation/path/job values on workers, retires cancelled work without joining and
isolates late success/exception from newer loads. Pair/union validation and complete
UI publication precede enabled controls. Native picker/comparison dialogs and the
main-editor busy boundary guard cancellation, child/owner destruction, cleanup
reentry and unchanged editor source/history/selection/viewport/files. Existing
native-size alignment, transparent padding, private sRGB interpretation and Core
geometry/raster policy remain unchanged.

Twenty-six new native cases cover blocked cancellation/teardown, immutable
rendering, redacted failure/retry, union limits, progress/enable reentry, owned
file pickers and main-editor lifetime. Existing actual divider keys/pointer,
independent pixel/layout, sidecar isolation, exact Undo/Redo and real PNG/.xann
save/reopen cases are retained/strengthened. The full local 44-target run passes
in 65.53 seconds (editor-window 26.62); six affected Qt 6.4.2 targets pass in
33.42 seconds (editor-window 25.38). All 114 unchanged published vectors and 32
tooling tests pass; no timeout or expected result changed. The synthetic ready
comparison frame was inspected. An initial offscreen GTK-helper fixture timeout
was resolved by selecting Qt owned picker controls in the fixture only, with
private QSettings; physical desktop helpers remain unqualified. A new union-test
compile error was corrected to independent width/height assertions. Final checks
and exact pushed native/lint CI are reported with the checkpoint. Prior canvas
commit 57a547a has verified successful CI 38700573514: 44 Ubuntu 24.04/Qt 6.4.2
targets in 63.36 seconds and lint success.

Requirement IDs/statuses/dispositions/vector references, 80 implemented/68 partial
core labels, 52 registered commands and fourteen visible defaults remain unchanged.
The full application/capture/recording/audio/upload/workflow/settings/editor/tools/
effects/canvas/output/history/automation/compatibility/distribution/migration/
updates/rollback scope, supported target matrix, resources/performance and human
privacy/security/a11y/parity/release gates remain Required. Product decisions remain
pending; no completion percentage or release acceptance is established. Next
migrate New Image/Open preparation and configuration to owned workers/dialogs and
nonblocking child/owner/cancel recovery, preserving dirty replacement, explicit
save/reload, source/profile/alpha and exact old-session history.

Final comparison review caught divider availability preceding geometry/status
publication. The follow-up moves enabling after complete publication and adds
native divider-enable observer assertions. Both complete affected targets pass
on the final source: 29.58 seconds locally (dialog 3.18, editor-window 26.40)
and 28.44 seconds on Qt 6.4.2 (dialog 3.06, editor-window 25.38). The other 42
successful local/four baseline targets remain unchanged; the earlier full-run
times are not claimed as one final full run. Scope, labels, vectors and all
remaining gates are unchanged.
