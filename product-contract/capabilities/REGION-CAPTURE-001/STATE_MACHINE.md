# Region-capture state machine

```text
created -> authorizing -> preparing -> idle -> selecting -> selected -> confirming -> completed
              |             |          |         |           |            |
              +-------------+----------+---------+-----------+------------+-> cancelled
              +-------------+----------+---------+-----------+------------+-> failed
```

- `idle`: overlay is ready and no selection exists.
- `selecting`: pointer or keyboard is actively defining bounds.
- `selected`: positive bounds exist and await explicit confirmation.
- `confirming`: bounds are frozen and pixels are being acquired/composited.
- `completed`, `cancelled`, and `failed` are terminal.

Transitions by event (RC-025):

| From | Event | To |
|---|---|---|
| `created` | start | `authorizing` |
| `authorizing` | `permission` authorized | `preparing` |
| `authorizing` | `permission` denied or restricted | `failed` |
| `preparing` | `ready` | `idle` |
| `idle` | `pointer-down` | `selecting` |
| `selecting` | `pointer-move` | `selecting` |
| `selecting` | `pointer-up` with zero area and no snap target | `idle` |
| `selecting` | `pointer-up` with positive area, quick confirmation off | `selected` |
| `selecting` | `pointer-up` with positive area, quick confirmation on | `confirming` |
| `selected` | `pointer-down` | `selecting` (new selection) |
| `selected` | `key-enter` or `confirm` | `confirming` |
| `confirming` | `capture-succeeded` | `completed` |
| `confirming` | `capture-failed` | `failed` |
| any non-terminal | `key-escape` or `cancel` | `cancelled` |
| any non-terminal | `topology-changed` | `cancelled` |

`key-enter` in `idle` or `selecting` has no transition and is ignored.

Quick confirmation transitions directly from `selecting` to `confirming` on
pointer release. Every terminal transition completes the caller exactly once.
Display-topology change is a cancellation unless the user explicitly restarts a
new session with a new topology snapshot.
