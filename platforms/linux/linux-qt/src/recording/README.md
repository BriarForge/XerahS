# Recording control and output policy

CORE-017 declaration, confirmed by Aoife under Mike's similar-apps rule on
2026-11-04: **Stop finalizes; Abort discards; encoder failure preserves a
recoverable partial file when one exists.** Abort follows ShareX/OBS-style
intentional discard semantics. It does not promise recovery of discarded media.

`Session::control` requires the explicit current session UUID. Stale controls and
invalid transitions return stable diagnostics. Repeated Pause, Resume, Stop and
Abort are harmless in their accepted states and do not repeat terminal results.

Stop enters Stopping, waits for encoder finalization, then `Session::finalize`
publishes the private temporary recording without replacing an existing output.
Only successful publication produces Completed and a completed output path.
Abort stops the encoder, calls `Output::abort`, deletes the private partial file,
and reports Cancelled with no completed or recovery output. Encoder or publication
failure calls `Session::fail`: `Output::retainFailure` preserves a nonempty
owner-only partial file, reports Failed plus cleanupRequired, and never claims a
completed recording. An empty partial is removed. Recovery may require container
repair; a new run and object destruction do not delete the retained file.

The request declares monitor/window target and optional region, frame rate,
cursor inclusion, MP4/WebM/GIF container, selected available codec, quality,
output folder/name and explicit audio device IDs. Pause retains one session and
output; Resume continues that session. Native transport and encoding live in
`app/RecordingController.cpp`; the Core state/output policy lives here.

Automated evidence: `RecordingTest::transitionMatrix`,
`targetedControlsAreIdempotentAndKeepThePublishedOutput`,
`encoderFailureRetainsPrivatePartialMediaButAbortDiscards`,
`outputFinalizationAbortAndCollision`; and
`RecordingWorkflowTest::recordingFailuresAndAbortHaveOneRedactedTerminal`.
Physical portal/encoder qualification is separate from this tested candidate.
