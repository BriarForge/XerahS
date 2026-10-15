# Saved workflow global shortcuts — native candidate evidence

Date: 16 October 2026. CORE-007/024 and WFCAT-003/013/014 native subset.

## User behavior

Choose **Enable workflow shortcuts…** in the XerahS window, Capture menu or tray.
The desktop's GlobalShortcuts portal receives the supported saved region workflow
UUIDs and names, and lets the user choose bindings. XerahS assigns no preferred
keys. Unsupported workflows are not offered. The command window shows the actual
returned bindings, including an empty selection and later desktop binding changes.
Names are the application's known saved names; trigger descriptions are plain text.

**Configure workflow shortcuts…** closes the old session and starts a new desktop
setup dialog. **Disable workflow shortcuts** also cancels pending setup. Neither
command edits the saved workflows, compositor files or desktop remembered choices.
This checkpoint's enablement lasts for the current XerahS process: enable again
after restart. There is no implicit startup permission prompt or background grab.
Saving or reloading workflows disables this session so obsolete labels/bindings
cannot silently acquire new meaning; enable again to review the current list.
Disabling shortcuts leaves an already running capture and its outputs intact.

Each accepted press reaches the same workflow-ID dispatcher and CaptureController
as Run selected and CLI. Saved definitions are validated and frozen at admission.
Busy capture, modal work and pending desktop shortcut setup refuse conflicting
capture requests. There is no deferred capture queue. Held-key repeats are ignored
until release, including a press refused while busy. Unsupported/deleted/changed
storage cannot bypass the normal resolver. Registration is not capture permission;
the actual capture retains its portal consent and cancellation path.

Unavailable portals, permission refusal, cancellation, timeout, invalid results,
owner replacement and session closure show actionable fixed messages. Manual Run
selected and CLI remain available when no shortcut service exists. Reconnection
requires explicit Enable; a replacement service inherits no trusted activation.
The native desktop-file identity is `com.xerahs.app`.

## Implementation and authority

QtCore `WorkflowShortcuts` validates the existing profile format, projects only
supported UUID/name pairs, validates accepted subsets and arbitrates press/release.
The native adapter uses CreateSession and one BindShortcuts call per session from
the [upstream portal API](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html).
It does not depend on the version-2-only ConfigureShortcuts method. Request tokens
are unpredictable; replies are correlated with their handles, including early
Response signals. Only the resolved unique portal owner can supply events;
session paths, offered/accepted UUIDs, duplicate IDs and bounded trigger text are
checked. Bindings changes preserve held keys that remain bound. Empty accepted
subsets grant no activation. Request/session cleanup covers cancellation before
method reply, pending consent, failures, disable and destruction. Calls have a
5 s timeout and user response a 120 s timeout. The private native fixture injects
shorter deadlines; production deadlines are not relaxed for CI.

The native actions are shared by the tray, window buttons and Capture menu.
Their keyboard fixtures use actual controls and visible results. All testing uses
an isolated non-activating D-Bus, temporary stores and synthetic pixels. No live
capture, real shortcut registration, Avalonia configuration, clipboard publication
or compositor configuration was used for verification.

## Qualification and next work

`WorkflowShortcutsTest.cpp` covers Core eligibility and subset/held-key policy,
early/normal replies, actual serialized binding fields, wrong owner/session/ID,
binding changes, invalid/duplicate responses, unavailable/denied/cancelled service,
timeout, late cancellation cleanup, service replacement and Closed. Native actions
cover keyboard enable, busy refusal, invalidation, reconfigure and disable. A real
CaptureController case proves a portal shortcut saves the configured synthetic PNG
and that busy presses and disabled shortcuts do not start another acquisition.
The full native suite, tooling and exact pushed Ubuntu/Qt 6.4.2 CI are recorded
with this checkpoint in the audit and continuation report. Published shared
CORE/WFCAT vectors are still absent; no shared expected result was invented.

This session-only subset does not accept the full shortcut catalogue, persisted
opt-in/autostart, backend-specific fallback, activation-token/window-parent focus,
locked-screen behavior, physical key/desktop/accessibility matrix or human privacy,
security, parity and release gates. All remain Required. Complete workflow families,
recording/audio, uploads and shared CLI/MCP schemas remain unfinished.
[Admitted executions](WORKFLOW-EXECUTIONS.md) now record shortcut origin and link
region invocation/early outcomes to actual pipeline results. [Headless discovery](WORKFLOW-DISCOVERY.md) now exposes saved IDs/default/support.
Next add execution metadata retention controls, atomic filename counters and
the remaining native capture families.
