# Native command palette

Date: 12 November 2026. CORE-007 subset for the Linux Qt edition.

The palette searches commands that are already registered and runs the same `QAction` the tray, command window and menus share. It does not register commands, choose a shortcut, or perform the operation itself. Closing the palette is not a successful command.

## API

```text
xerahs::app::CommandPalette(CommandRegistry &registry, QWidget *parent = nullptr)
```

`CommandPalette` is a `QDialog`. `accept()` and `reject()` are the normal dialog slots.

| Object name | Type | Accessible name |
|---|---|---|
| `commandPalette` | the dialog | Command palette |
| `commandPaletteSearch` | `QLineEdit` | Search commands |
| `commandPaletteCommands` | `QListWidget` | Commands |
| `runCommand` | `QPushButton` | Run command |
| `commandPaletteStatus` | `QLabel`, `Qt::PlainText` | Command status |

Each list row stores:

| Role | Value |
|---|---|
| `CommandPalette::CommandIdRole` (`Qt::UserRole`) | stable id |
| `CommandPalette::CommandLabelRole` | mnemonic-free label |
| `CommandPalette::CommandShortcutRole` | primary shortcut as `QKeySequence::NativeText`, or empty |

Visible row text is the label, then two spaces and the shortcut when the action has one. Mnemonics are removed (`&` marks the shortcut character, `&&` is a literal `&`).

## What it lists

On construction, and again on every non-spontaneous show, the palette reads all six `CommandSurface` values through the existing public API: `ids`, `action` and `spec`. The surfaces, in tie-break order, are Tray, Window, FileMenu, CaptureMenu, ToolsMenu, PreferencesMenu.

A stable id is listed once, even when it is on several surfaces. The row keeps that surface's `QAction`. There is no second copy of the operation.

Order is `CommandGroup`, then registration order where a surface list proves it. Each surface's `ids()` list is a subsequence of registration order inside a group. Those subsequences are merged. When two commands in the same group never appear together on one surface, the command from the earlier surface above comes first. `CommandRegistry` does not publish a global sequence, so this merge is the whole ordering rule.

Commands whose `QAction::isVisible()` is false are omitted. Disabled actions stay in the list. Qt's `QAction::isEnabled()` is also false while an action is hidden; the palette does not call `setEnabled` to do that. Each non-spontaneous show clears the query.

Search compares the trimmed query with the live action text, the registered label and the stable id, case-insensitively. A shortcut is not part of the search.

## Availability

The Run button is enabled only for the selected action that is currently enabled and visible. Enter, double-click and Run all use `accept()`.

`accept()` on an empty selection, a disabled action, or a hidden action leaves the palette open and does not invoke anything. The status label says why:

| State | Status text |
|---|---|
| Registry destroyed | `Commands are no longer available.` |
| Nothing registered | `No commands are registered.` |
| Filter misses | `No commands match "%1".` |
| Every action hidden | `No commands are available.` |
| Rows exist but none selected | `Select a command.` |
| Selected action disabled or hidden | `"%1" is unavailable.` plus the action tooltip when it has one |
| Selected action can run, and has a status tip | that status tip |
| Selected action can run, tooltip only | that tooltip |
| Selected action can run, no tip | `Press Enter to run "%1".` |
| Escape, close, or `reject()` | `No command was started.` |
| Deferred run finds the action or registry gone | `The command is no longer available.` |
| Deferred run finds the action disabled or hidden | `"%1" is unavailable.` |
| Palette still modal at dispatch | `The command was not started.` |

`%1` is the mnemonic-free action text, or the stable id when that text is empty. The palette never enables an action and never writes a success message. The shared action reports its own result.

Action text, enabled, visible and shortcut changes update the open list. A destroyed action drops its row. A destroyed registry clears the list. Adopted actions are not reparented.

## Running a command

`accept()` hides the palette while it is still modal, then sets it non-modal. Qt removes a window from the modal stack only when it is hidden with modality still set. The shared `QAction::trigger()` is queued after that and runs on the next event-loop turn.

At that turn the palette checks again:

- the dialog is hidden, non-modal, and is not `QApplication::activeModalWidget()`
- the original parent widget, when there was one, still exists
- the registry still exists and `action(id)` is still this `QAction`
- the action still exists, is enabled, and is visible

Any failed check skips `trigger()`. `reject()`, destruction of the palette, or a newer show cancels a queued run. `QDialog::Accepted` means the palette closed in order to try the command. It does not mean the command ran.

Keep the palette alive until that queued turn. Do not set `Qt::WA_DeleteOnClose`, and do not delete it from `finished()`. Destroying it first cancels the run.

Up and Down in the search field move the selection. Enter runs. Escape rejects. The Run button is not the dialog's default button, so a disabled command cannot be activated by the dialog's Enter handling.

## Shell integration

`installApplication` registers `command-palette` in the tray, File menu and main-window grid. The shared action uses Ctrl+Shift+P as a window shortcut attached by `ApplicationWindow`. It opens one reusable palette owned by the main window and refuses to open over another modal dialog. Closing the palette preserves its lifetime through deferred dispatch; destroying the owner cancels that dispatch.

The registry holds adopted actions weakly and omits destroyed actions from surface lists, so reopening a palette cannot revive a stale action. Acceptance and rejection observers may destroy the palette safely. Qt closes the modal loop with completion signals blocked, then the palette delivers accepted/rejected and finished with lifetime checks between them; this avoids Qt 6.4 emitting finished on an already destroyed dialog.

Persisted `CaptureCommandPaletteEnabled` and the reference global hotkey remain separate settings work. This local shortcut does not register a global hotkey.

## Verification

An isolated Qt 6 harness compiled `CommandPalette.cpp` and `CommandRegistry.cpp` with `g++ -Wall -Wextra -Wpedantic` and ran under `QT_QPA_PLATFORM=offscreen`. It checked deduplicated group order, case-insensitive label and id search, disabled and hidden refusal, Escape, `exec()` releasing modality before `trigger()`, a disable or hide that happens while the dialog is closing, destruction of the palette, action or registry, live text, shortcut, enabled and visible updates, and keyboard selection. The shared build directory was not used.

Parent integration verification: the registered Ctrl+Shift+P window shortcut opens the real shell palette, dispatches the existing last-region command after modality releases, and reports its actual stale-geometry refusal. Both host Qt and the Qt 6.4.2 baseline pass the palette, registry and shell checks, including immediate observer deletion.
