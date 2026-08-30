# Contract backlog tracker

`tracker.py` turns the maintained Product Contract completion plan into an
enforced queue:

- validates wave membership, target ordering, dependencies, and cycles;
- routes every open parity-ledger row to one work package;
- rejects completed ledger packages that still own open rows;
- renders the reproducible `product-contract/backlog/STATUS.md` view; and
- lists the exact rows owned by a package for focused daily work.

The parser intentionally reads only stable top-level ledger fields and uses the
Python standard library, so it runs the same way locally and in CI.

```powershell
python tools/contract-backlog/tracker.py --write
python tools/contract-backlog/tracker.py --check
python tools/contract-backlog/tracker.py --package PC-SETTINGS-CATALOG-001
python -m unittest discover tools/contract-backlog/tests -v
```

When a package is finished, first link all assigned rows to stable requirement
IDs and satisfy its exit criteria. Then update its maintained status. Do not
mark a package complete merely to reduce the queue.
