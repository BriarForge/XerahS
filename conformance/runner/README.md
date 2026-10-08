# Conformance runner

Pinned Python 3.12-or-later CLI for the pilot (D-CON-001). Standard library only.

It reads each capability's `test-vectors.json`, merges `input_defaults`, sends one request per vector to a platform adapter, and compares the result under `product-contract/VECTORS.md`. Expected values come only from the contract.

```bash
python3 conformance/runner/xerahs_conformance.py \
  --adapter platforms/linux/linux-qt/build/xerahs-conformance-adapter \
  --platform linux --edition linux-qt \
  --capability FILENAME-GENERATION-001 \
  --report conformance/reports/linux-qt.json
```

Without `--capability` it runs every capability that has vectors. Vectors the adapter cannot perform fail; "not implemented" is never a pass. The exit code is 1 when any vector fails.

## Adapter protocol

The runner starts the adapter once per vector and writes one UTF-8 JSON request to its stdin:

```json
{ "capability": "FILENAME-GENERATION-001", "operation": "expand", "input": { } }
```

The adapter writes one JSON object to stdout and exits 0. That object is the result compared with the vector's `expected`. An adapter that cannot perform the request returns `{"adapter_error": "<reason>"}`, which fails the vector. A non-zero exit or invalid JSON also fails it.

## Tests

```bash
python3 -m unittest discover conformance/runner/tests -v
```
