# Product Contract completion backlog

This directory is the maintained plan for progressively completing the Product
Contract. It is planning and traceability metadata, not normative product
behavior.

- `backlog.json` is the source of truth for work packages, daily waves,
  dependencies, ownership roles, approvals, deliverables, and completion gates.
- `STATUS.md` is generated from the backlog, manifest, baseline, and five parity
  ledgers. Do not edit it by hand.

Every parity-ledger row whose `contract` is `null` MUST resolve through a route
in `backlog.json` to exactly one work package. The tracker fails when a new
uncontracted domain is not routed, a dependency is invalid, a completed package
still owns open rows, or the generated report is stale.

Run:

```powershell
python tools/contract-backlog/tracker.py --write
python tools/contract-backlog/tracker.py --check
python tools/contract-backlog/tracker.py --package PC-CAPTURE-001
```

Package status is maintained deliberately in `backlog.json`. Row counts are
derived; they are never copied into the maintained plan. A ledger package can be
marked `complete` only after all of its assigned rows link to approved contract
requirements and its package exit criteria are satisfied.

Target dates sequence the work; they are not waivers. A missed date changes the
plan, not the Product Contract or its completion definition.
