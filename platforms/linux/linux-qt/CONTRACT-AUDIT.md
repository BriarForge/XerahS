# Linux contract scope audit

Audit date: 12 October 2026; evidence updated 14 October 2026. Starting implementation: `82ef55e` on `main`.
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
packages. Current trace files label 81 as implemented and 67 as partial; 175 lack
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
| `EDITOR-ANNOTATIONS-001` | Explicit native Select/Rectangle/Crop modes, UUID-based keyboard object selection, numeric Rectangle creation with frozen defaults/state/selection, eight pointer resize handles and absolute numeric/color/alpha properties, frozen group pointer movement and native group rotation drafts, gesture cancellation and finite atomic geometry/history; Rectangle creation/selection/style/rotation/order and protected direct edits; native rectangle duplication/object Cut/Copy/Paste with owned MIME, bounded validation, fresh IDs and exact history | No complete EA trace. All 20 stable tools, curves/freehand/text/balloons/steps/image/emoji/cursor, redaction/magnifier/spotlight/eraser/cut-out, complete multi-tool/asset/shared clipboard and duplication, complete persistence and accessible tool/option/handle controls remain Required. |
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
   commits against stale document and selection under EA-003/017/018. Next guard
   native stroke/fill color dialogs against intervening state/selection and owner
   destruction, preserving selected/default style semantics, while resolving Ellipse schema/rasterization clarification
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
