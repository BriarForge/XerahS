# Baseline census

This tool inventories the pinned KovaForge/XerahS discovery baseline required by
BXIP001 section 20. It verifies the exact parent and editor commits before it
writes evidence, then derives the initial parity ledgers from independent source
categories.

Run from the BriarForge/XerahS repository root:

```powershell
python tools/baseline-census/census.py --reference C:\Users\Public\source\repos\KovaForge\XerahS
python tools/baseline-census/census.py --reference C:\Users\Public\source\repos\KovaForge\XerahS --check
```

`--check` regenerates every owned artifact in memory and fails if a committed
artifact differs. The tool does not decide product behavior. It records source
evidence and creates the queue that contract authors and product reviewers must
resolve.

Owned generated artifacts:

- `product-contract/reference-baselines/kova-0.29.0.yaml`
- `product-contract/reference-baselines/provenance/kova-0.29.0-census.json`
- every YAML file under `product-contract/parity/`

The census is deliberately not a closure attestation. Runtime observation,
user-journey review, unresolved-symbol review, and product-owner sign-off remain
manual gates documented in the provenance README.
