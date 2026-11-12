# Native editor host callbacks (EO-014)

The Linux editor supplies built-in Save, Save As and Close hosts. It does not
require an injected replacement for a native operation that already exists.
Copy, Print, Pin, Upload and task completion use the explicit providers below.
Empty required providers disable the action or show an unavailable explanation;
optional save fixture hooks select the existing native implementation.

Configure editor providers on the GUI thread. Each output freezes value-owned
source/document/port snapshots before dispatch. For Save/Save As, the output
attempt starts after destination acceptance: save hooks are copied then, so
configuration while choosing a destination applies to that upcoming attempt.
Replacing a provider after dispatch affects later attempts. Reference arguments are borrowed only until the call returns.
A host that retains pixels, bytes or metadata must retain value-owned copies.
Worker captures must own their lifetime independently of the editor and must
not access GUI objects. Independent jobs must not share borrowed devices or
mutable host state. No callback implicitly owns the editable session.

| Operation | Ownership, thread and lifetime | Cancellation, error and reentrancy |
|---|---|---|
| Copy | `EditorCopyPorts` prepares an owned image/PNG on a worker and borrows that payload for GUI-thread `publish`. A host retains image/byte values, never references to the call. | Succeeded acknowledges the clipboard offer. Cancel/destruction before publication cannot copy. Failed/Cancelled promise no effect; publication exceptions or malformed acknowledgements mean unknown delivery and cannot authorize automatic retry. Reentrant output stays busy. Missing `publish` disables Copy. |
| Save | `EditorSaveHooks` borrows a worker-owned image/device for encoding and invokes `beforePublish` separately at each artifact boundary. Empty hooks use native encoding/publication. `EditorSaveControl` observers execute synchronously on the caller's thread, which is a worker through `runEditorSave`. | An encoder must produce decodable content in the selected format; its boolean alone is not a publication acknowledgement. Cancel/exception before raster publication keeps the old destination. After raster publication, sidecar failure/cancellation retains the raster, receipt and exact partial facts. Completion-observer exceptions cannot erase acknowledgements or escape into Qt action dispatch; a fixed warning reports failed observation without exposing payloads. GUI output reentry stays busy; owner destruction abandons delivery without joining or borrowing GUI/session state. |
| Save As | The GUI-thread native `EditorSaveDialog` returns an explicit destination/format. The same owned Save path handles encoding and paired publication after the chooser returns. | Chooser cancellation starts no output. Overwrite/format validation and partial outcomes use the Save contract. Owner deletion cannot adopt a late result. The output-busy guard spans chooser, rendering and publication; reentrant Save/Save As and closing cannot open another chooser or start another output. There is no missing injected chooser: the native host is installed by the editor. |
| Print | GUI-thread `configure` borrows an owned rendered snapshot. A selected `shared_ptr<QPrinter>` transfers exclusive use to the worker-only `submit`; neither callback may retain or use the borrowed printer concurrently. | Configuration success requires a valid printer/scale; submission success acknowledges the job. Before submission, Cancel/failure/destruction submits nothing. During submission cancellation is best effort; exceptions/abandonment or uncertain delivery cannot authorize retry. Reentrant output stays busy. Missing configure/submit disables Print or returns print-unavailable. |
| Pin | `EditorPinPorts` prepares owned pixels on a worker; GUI-thread `publish` borrows the snapshot. Successful publication transfers ownership of an independent pin to the service. | Cancel/destruction before publication creates no pin. Failed/Cancelled promise no effect; throwing/malformed publication means unknown delivery. Reentry stays busy. Missing publisher disables Pin. An acknowledged native pin survives editor destruction. |
| Upload | Worker preparation owns the PNG; GUI-thread `confirm` returns an owned destination frozen by explicit consent; worker-only `submit` borrows the frozen snapshot/destination and cooperative control. Captures outlive abandoned work. | No remote effect occurs before confirmation. Successful submission requires validated HTTP(S) result URLs; malformed/missing success facts mean unknown delivery. Cancel/exception/abandonment after submission preserves actual/unknown delivery facts and cannot trigger automatic retry. Reentrant output stays busy. Missing confirmation/submission disables Upload or explains its unavailability. |
| Task completion | `CaptureAnnotationDialog::taskFinished` is GUI-thread, at most once. The host connects before presentation and takes value copies of `document()`/`renderedResult()` synchronously during Continue; Cancel has no result. Host destruction disconnects its receiver. | State transitions fence reentrant Continue/Cancel/Close. Upstream cancellation abandons delivery. A missing receiver is explained without output. Throwing observers are contained and never retried; uncertainty after auto-copy stays distinct from a ready Continue. |
| Close | Qt calls `EditorWindow::closeEvent` synchronously on the GUI thread with a borrowed event. The editor owns confirmation lifetime through guarded heap objects; deleting the editor also retires the confirmation. | Pending output/interaction/close work refuses reentry. Save must acknowledge both artifacts before dirty close succeeds; failed/cancelled Save keeps the editor open. Discard is explicit. Cancellation or owner deletion during confirmation cannot save, access the deleted editor, or double-delete a dialog. The built-in Qt close handler is always present. |

These declarations correspond to `EditorCopy.h`, `EditorSave.h`,
`EditorSaveDialog.h`, `EditorPrint.h`, `EditorPin.h`, `EditorUpload.h`,
`CaptureAnnotationDialog.h`, and the guarded actions/close path in
`EditorWindow.cpp`. They describe existing provider contracts, not new authority
to upload, overwrite, read credentials, retry an uncertain effect or discard data.

## Executed evidence

`EditorCopyTest`, `EditorSaveTest`, `EditorSaveDialogTest`, `EditorPrintTest`,
`EditorPinTest`, `EditorUploadTest`, `EditorTaskTest`, `EditorExitTest`, and
`EditorWindowTest` drive the real native implementations and injected host
boundaries. They cover frozen snapshots, GUI/worker placement, missing providers,
exceptions, cancellation, owner destruction, reentrant output, acknowledgement,
partial paired publication, task completion and close/save/discard/cancel.

`destroyingEditorDuringCloseConfirmationDoesNotSaveOrCrash` reproduced a dirty
close abort (`double free or corruption`) before the ownership fix. Dirty and
clean owner destruction now return without saving or leaving a confirmation.
`hostExceptionPreservesTheArtifactPublicationBoundary` verifies exceptions
before each artifact, retained raster receipts after companion failure, and
repair without rewriting the acknowledged raster. The real Save As action test
reproduced two nested choosers before the transaction-wide busy guard; the Save
completion test reproduced an escaping host exception before observer containment.

EO-014 governs declared host semantics and missing-provider behavior. The wider
all-platform/failure qualification matrix remains EO-016, and physical native
service/accessibility qualification remains in its own requirements. This
checkpoint does not claim those other requirements or release acceptance.
