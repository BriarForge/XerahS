# Post-capture pipeline state machine

```text
created -> selecting -> running -> completed
             |            |  \-> cancelling -> cancelled
             |            \----> interrupted -> recovering -> running
             \-> cancelled                     \-> completed
```

`completed` describes a finished pipeline, not universal success. It contains
`succeeded`, `failed`, and dependency-`skipped` action results. `cancelled`
means user or caller cancellation prevented the remaining plan. A process exit
or unknown external outcome enters `interrupted` and requires recovery before a
terminal claim.

Each action moves `pending -> running -> succeeded|failed|cancelled|skipped`.
Only `pending` actions may start. Results are append-only; retry creates a linked
attempt rather than overwriting the prior result.
