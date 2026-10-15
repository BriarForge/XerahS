# Owned canvas mutation preparation and rotation previews

This is partial Linux native candidate evidence for EC-007/008/009/010/011/012/
014/015/017, ES-002/005/006/007/008/019 and EA-017/018/019/020. Every full
requirement, supported target and human/release gate remains Required. Shared
contracts, schemas, vectors, expected results, dispositions and approvals are
unchanged. This is an implementation checkpoint, not release acceptance.

## Production behavior

Quarter turns, half turn, flips, pixel/canvas resize, Auto Crop, Flatten, Clear
Annotations and Clear Image now prepare owned source/document/operation snapshots
on workers. A common native progress boundary shares history restoration's
cancellation and lifetime handling. Workers contain values and cancellation/
progress state; they never capture a window, dialog, session or borrowed history
pointer. Cancel, Escape, child destruction or forced owner teardown releases the
nested wait without joining unfinished work. Worker exceptions produce redacted
failure outcomes and a plain-language retry message; late completion cannot
publish a result or write files.

The main editor freezes document, working source, history state, exact selection
and canvas generation before busy notifications and before numeric configuration.
Surviving controls recover through the existing weak-owner output guard. Reentry
and ordinary Close are refused while configuration, preparation or publication
is busy. A new QtCore canvas-commit overload admits only the frozen state and
selection before validating and committing the complete result. Generation is
checked at the native boundary. Crop also uses this guarded Core admission.
Stale state/selection/replacement or invalid preparation keeps the current canvas
and Redo; no-op and cancellation add no history.

Successful publication updates the source cache, working image and prepared
private display before native refresh. Source decoding, PNG encoding and private
view conversion stay off the GUI thread on these mutation paths. Working source
pixels, alpha and profile remain authoritative. Untagged working source stays
untagged; the private view applies the existing canvas's sRGB assumption. Display
P3 working pixels/profile remain intact while the private view converts to sRGB.
The optional display member is native-only and never enters document JSON,
companions, public conformance output or preferences.

Custom rotation now owns immutable preview work rather than a future capturing
its dialog. Debounced angle/interpolation/expansion/background changes cancel
obsolete work; only the latest complete preview can apply. Old previews cannot
be accepted after settings change. Cancel and destruction do not wait for the
worker. Actual composite and private source views are prepared on that worker;
accepted rotation enters the same frozen publication/history path once.

Resize/Auto Crop configuration, their colour pickers, custom rotation, error
presentation and Clear confirmation use owned native dialogs. Cleanup hides and
detaches a dialog before deleting it, so a destruction observer may delete the
parent without recursively deleting that same child. Numeric controls retain
existing rules/IDs, aspect locks, anchors, fill, alpha and tolerance semantics.
Clear keeps Save/Discard/Cancel choices and default Cancel. Save then Clear uses
the ordinary explicit save outside the confirmation's busy scope, rechecks the
original state/selection/generation and refuses Clear after a change or failed
save. No geometry, raster, unknown-object or output-format policy is introduced.

QtCore keeps product geometry/history validation. Native preparation/progress and
codec/view work stay in app libraries with dynamically linked Qt. The shared
preparation helper removes duplicated native wait/lifetime code without changing
the existing history worker's behavior or public/persisted compatibility.

## Candidate verification

Four Core rows cover accepted canvas admission, stale state, stale selection and
invalid source. Refusal preserves exact document/selection/dirty state/Redo;
acceptance retains one-step Undo/Redo and reachable fields/assets.

Twelve actual native command rows cover both quarter turns, half turn, both
flips, pixel resize, canvas resize, Auto Crop, Flatten, Clear Annotations, Clear
Image and custom rotation. Independent pixel-coordinate formulas or declared
interior/alpha expectations verify source output. Alpha/Display P3 profile,
fields/IDs/assets/selection, viewport dimensions, one-step history, no implicit
file writes, actual PNG/.xann reopening, empty-companion behavior and saved
checkpoints are checked. A separate untagged-source row checks that only the
private display gets sRGB interpretation.

Twenty-two rows cover standard preparation and custom preview with Cancel,
Escape, child destruction, owner destruction, worker failure, stale state,
stale selection, replacement generation, busy-admission destruction, cleanup
observer destruction and final-status destruction. Deliberately blocked work
runs off the GUI thread; cancellation and forced teardown return under 500 ms
while the private gate remains blocked. Reentrant Undo/Redo/flip/resize does not
mutate the document. Eight rows destroy child or owner during resize, canvas
resize, Auto Crop and Clear configuration, preserving original files/history.
The existing history/capture/source/native suite also exercises the common
helper and guarded Crop integration.

A ready synthetic 17-degree rotation frame was inspected: angle, interpolation,
expansion, transparent fill, source/annotation/alpha-checker composite, dimensions,
progress and OK/Cancel are readable. Preparing-operation progress was also
inspected. These are offscreen candidates, not physical keyboard/AT/contrast/
performance/target or human approvals. Verification sequence/totals and the
previous exact CI are indexed in [CANVAS-WORKERS-EVIDENCE.json](CANVAS-WORKERS-EVIDENCE.json).

## Remaining work

Image comparison and New/Open preparation still have their earlier dialog/worker
lifetime paths. Next migrate ImageComparisonDialog loading and previews to owned
immutable workers and nonblocking cancellation/owner recovery, preserving its
alignment/divider controls, validated file snapshots and editor-history isolation.
Capture history has its earlier asynchronous coordinator; full target-identity,
shutdown and resource qualification remains Required.

All remaining annotation tools/fields, effects, inserted images/background,
general affine canvas transforms, output formats, capture topology, recording/
audio, uploads/credentials, workflows/settings/catalogues, application/history/
automation/compatibility, packaging/migration/updates/rollback, supported Linux
matrix, resource/performance and human privacy/security/accessibility/parity/
release gates remain Required. Recorded Ellipse, clipboard-assets, output-format,
legacy-companion and shortcut decisions remain pending. No production-readiness
percentage is established.
