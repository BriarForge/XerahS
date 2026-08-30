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

Quick confirmation transitions directly from `selecting` to `confirming` on
pointer release. Every terminal transition completes the caller exactly once.
Display-topology change is a cancellation unless the user explicitly restarts a
new session with a new topology snapshot.
