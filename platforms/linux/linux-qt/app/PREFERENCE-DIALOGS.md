# Owned native preference dialogs

Partial candidate evidence for ESET-003/004/006/007/010/011/016 and ES-008/019.
The full contract, supported targets and human/release gates remain Required.
Shared settings validation, schemas, vectors, defaults, shortcut policy,
requirement labels/dispositions and approvals are unchanged.

## Production behavior

Drawing Defaults, Appearance and Toolbar now own their dialogs and callbacks.
Parent deletion during the modal loop cannot delete a stack object. Native
appearance controllers are dialog children. Cleanup hides and detaches each
dialog before deletion, so a destruction observer may safely delete the parent.
Callers check weak parent ownership after the modal loop and cleanup.

The native-only `PreferenceDialog` boundary serializes picker and Apply admission.
Colour pickers own their alpha-capable native dialog and use weak completion
checks. Cancel/Escape, direct picker deletion, parent/configuration deletion and
configuration rejection while a picker is open cannot publish a late colour.
Apply and Reset are refused while a picker owns the draft. Native desktop picker
helpers remain enabled in production; test widgets use private Qt settings.

Apply copies the draft and frozen revision before disabling controls. Reentrant
Apply is refused through persistence, synchronous live publication, failure
reporting and control recovery. The existing native host and QtCore store still
perform field/hotkey validation, revision checks, private atomic persistence and
unknown-field preservation. Only successful persistence finishes the dialog;
direct Accepted completion cannot bypass Apply. Failed or stale writes retain
the draft and a visible diagnostic. A locked write can be explicitly retried.
Once an explicitly requested write has committed, later owner deletion preserves
that committed preference; it cannot repeat or roll back the user's write.

Toolbar drafts copy available command names/IDs rather than retaining a borrowed
registry across the nested loop. Weak guards surround native reset/reorder/error
boundaries. Live toolbar rebuilding snapshots placements, uses weak action
references and refuses recursive traversal. A newer preference arriving during
rebuild schedules a fresh traversal, so the final toolbar reflects the latest
persisted state. Fixed menus, protected shortcuts, unavailable entries and text/
modal/busy precedence retain their existing policies.

Editor configuration uses the existing output guard plus a weak mutation guard.
Editing, output, replacement and recursive configuration remain unavailable until
cleanup completes. Busy-entry, modal, publication and cleanup owner deletion do
not restore state through dead references. Successful unchanged Drawing Defaults
Apply still restores the global creation style and clears a previous preference
failure, even when there is no changed-defaults event. Existing annotations,
source, selection, document history and persistence checkpoints remain intact.

## Native evidence

The dedicated private-bus `native-preference-dialogs` target exercises the real
editor/configuration sources with temporary files and private QSettings. Its
59 behavior cases cover:

- Twelve Drawing/Appearance/Toolbar parent deletion, direct dialog deletion,
  cleanup-time parent deletion and bypassed direct Accepted outcomes, with no
  pre-Apply persistence or revision change.
- Eighteen stroke/fill/accent picker cases: accepted alpha, Escape, direct child
  deletion, parent deletion, configuration rejection and cleanup-time parent
  deletion. Programmatic Apply and Reset while picking are refused. Cancel keeps
  the draft and preferences; accepted colours persist canonical alpha.
- Twelve serial Apply/publication cases: reentry, parent/dialog deletion after
  the committed write and locked-write failure followed by retry. Real files
  reopen with the accepted preferences; exactly one write revision/publication
  occurs for the original Apply.
- Fifteen actual-editor busy-entry, modal-parent, cleanup and publication owner
  boundaries plus accepted configuration. Controls recover only for surviving
  windows, while source/document state remains exact and creation defaults apply.
- A newer toolbar preference during native ActionAdded wins after the guarded
  queued refresh. A real failed toolbar default save followed by unchanged
  Drawing Defaults Apply recovers the intended global style without history.

Existing preference/appearance/toolbar tests retain their prior validation,
unknown/unavailable fields, cancellation/reset, multi-window publication, restart,
stale/read-only/locked refusal, hotkey and palette assertions. The native drawing
preference target now uses a private bus because its appearance controller reads
the same native desktop source as the other dialogs.

The first focused run passed all 51 initial cases and all four affected targets.
The initial complete local suite passed 47 targets. Review then added picker-
parent rejection, editor publication teardown, queued newest-toolbar refresh and
unchanged-default recovery cases. A mixed-type `auto` declaration was corrected
at compilation. No expected output or deadline was relaxed. Final builds and
verification totals are indexed in
[PREFERENCE-DIALOGS-EVIDENCE.json](PREFERENCE-DIALOGS-EVIDENCE.json); the checkpoint
report verifies the exact pushed Ubuntu 24.04/Qt 6.4.2 CI revision.

Synthetic QA inspects all three configuration forms and the alpha-capable colour
picker. Drawing Defaults now shares live resolved theme/contrast across native
states. This does not accept physical helpers, screen readers, high-contrast
hardware/theme combinations or the full supported matrix.

## Remaining work

Next own Recent Images configuration and its stale/failure message dialogs,
freeze revision/draft admission, and replace remaining raw editor preference
guards. Preserve opt-in private paths, atomic removal/clear, incoming history,
native selection and successful image output.

All application/capture topology/recording/audio/destinations/uploads/credentials/
plugins/workflows/settings/tools/effects/canvas/output/utilities/history/automation/
compatibility/packaging/migration/updates/rollback, resource/performance and
supported Linux matrix work remain in scope. Pending Ellipse, clipboard-assets,
output-format, legacy-companion and shortcut decisions and human security/privacy/
accessibility/parity/release gates remain unresolved. No completion percentage or
production-readiness acceptance is established.
