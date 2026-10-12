# Linux contract scope audit

Audit date: 12 October 2026. Starting implementation: `82ef55e` on `main`.
Source of truth: Product Contract 0.3.1, its capability specifications,
scenarios, published vectors, parity ledgers and [readiness gates](../../../product-contract/READINESS.md).
This is an implementation audit and work ordering record, not an acceptance,
waiver, release approval or reduced completion condition.

The full native Linux application is **unfinished**. The baseline's 1,592
linked discovery rows establish contract routing, not implemented behavior.
Six capability packages have Linux requirement trace files and a production
conformance adapter; their 114 published vectors cover only those seams.
Neither that vector count nor a green Ubuntu CI run proves the full contract.

## Capability scope

All desktop capabilities remain in scope. Evidence below identifies the
current implementation surface; remaining work includes the capability's
entire specification and linked ledger rows, not just the examples listed.

| Capability | Current evidence | Requirements still needing implementation or qualification |
|---|---|---|
| `CORE-PLATFORM-001` | Native tray, instance socket, region controller, editor and desktop-file identity in `app/` and `packaging/` | No complete CORE requirement trace. Main application/onboarding, authenticated activation and failure recovery, localization, notification authority/privacy, updates, profiles/migration, credential services, global shortcuts, general capture, recording/audio, media, uploads, plugins, history, automation/watch folders/assistant, diagnostics and distribution remain Required. |
| `FILENAME-GENERATION-001` | `src/naming/`, adapter and 51 vectors | Complete native preference/input integration, filename preview and destination workflow qualification remain Required. Core expansion evidence alone does not accept all native usage paths. |
| `POST-CAPTURE-ACTIONS-001` | `src/actions/`, adapter and 17 vectors; `CaptureActions` implements the current save/clipboard path | Full native action catalogue, configurable action selection, annotation/effects/output handoffs, uploads, bounded retry, durable results/restart recovery, privacy and consent qualification remain Required. |
| `REGION-CAPTURE-001` | `src/capture/`, 21 vectors; `PortalScreenshot`, `CaptureController`, `RegionOverlay` | Native portal lifecycle/cancellation/failure evidence is missing at the audit baseline. The controller assumes a uniform desktop scale; per-monitor physical source mapping, rotation/colour/availability, mixed displays, cursor settings, HDR conversion, interactive handles/snapping/magnifier, persisted last-region invocation, physical accessibility and performance remain Required. |
| `EDITOR-SESSION-001` | Document/sidecar, source binding, rectangle history/rendering, native editor, 19 vectors | Full contracted annotation/effect/canvas/output integration, complete persistence compatibility, native parity, visual goldens and human reviews remain Required. |
| `EDITOR-ANNOTATIONS-001` | Rectangle creation/selection/style/rotation/order in the session and canvas | No complete EA trace. All 20 stable tools, curves/freehand/text/balloons/steps/image/emoji/cursor, redaction/magnifier/spotlight/eraser/cut-out, object clipboard/duplicate, complete persistence and accessible tool controls remain Required. |
| `EDITOR-EFFECTS-001` | General canvas pixel preparation/history exists | No complete EE trace or effect catalogue implementation. All 232 selected stable effect IDs, parameter semantics, preview/Apply, resource/colour/alpha/randomness rules, presets/favorites, cancellation and visual evidence remain Required. |
| `EDITOR-CANVAS-001` | Native New/Open, zoom/pan, crop/auto-crop, resize/rotate/flip, represented-annotation flatten/clear and comparison; three vectors | Inserted images, background composition, general affine annotation transforms, complete flattening and native format/colour/resource/interaction qualification remain Required. |
| `EDITOR-OUTPUT-ACTIONS-001` | Current PNG/sidecar Save and Save As implementation | No complete EO trace. Full immutable output service, JPEG/WebP quality/alpha/metadata choices, native image copy, uploads, print, pinning, result publication and destructive/exit consent qualification remain Required. |
| `EDITOR-SETTINGS-001` | Private atomic store, recents, creation-only border/fill/thickness, appearance, window state and toolbar controls; three vectors plus native tests | Complete field ledger/default/migration/downstream coverage, fonts/other tool defaults, numeric/effect settings, recent/favorite effects, convenience/automation preferences, independent whole-file recovery, shared semantics and physical/human reviews remain Required. |
| `EDITOR-UTILITIES-001` | Canvas comparison support covers only a related image operation | No complete EU trace. Contracted utility tools, deterministic local processing, resource policy, progress/cancellation, accessibility and output integration remain Required. |
| `SETTINGS-CATALOG-001` | Editor preference subset and inactive unknown preservation | No complete SETCAT trace. All 922 settings rows, profiles/scopes, shared schema, secure credential references, import/export/reset preview, full validation/migration and approval remain Required. |
| `WORKFLOW-CATALOG-001` | Fixed save/clipboard capture actions and isolated pipeline model | No complete WFCAT trace. All 436 workflow/action rows, versioned definitions, consistent entry points, execution identity/results, concurrency, cancellation, retry and privacy/permission integration remain Required. |
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

1. Complete native screenshot request lifecycle under RC-001/002/008/016/018
   and CORE-004/005: cancel pending native interaction, distinguish user cancel
   from denial, bind responses to the requested service owner/handle, terminate
   transport failure once, redact errors and bound native image decoding.
   Verify production calls on an isolated bus and temporary image fixtures.
2. Remove the controller's uniform-scale source assumption through a specified
   physical-source backend and verify per-display mapping/topology/colour, then
   complete last-region storage, cursor/settings and overlay interactions.
3. Continue configurable native workflows and the complete post-capture action
   catalogue alongside recording, uploads/credentials/history/integrations and
   the remaining editor tools/effects/canvas/output/settings/utility features.
4. Maintain requirement-level traces and meaningful failure/consent evidence;
   qualify the complete supported target matrix and prepare all human gates.

Each checkpoint preserves the full scope above. A final completion audit must
prove every applicable requirement and ledger row against production behavior,
meaningful tests and accepted review/release evidence.
