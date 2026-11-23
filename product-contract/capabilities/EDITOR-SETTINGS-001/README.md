# EDITOR-SETTINGS-001

Approved contract for persisted ImageEditor preferences and defaults. Exact
candidate fields and source declarations are inventoried in the generated
settings ledger; semantic approval remains a product-owner gate.

`setting-dispositions.json` publishes the decided per-row ledger dispositions
(decision ESET-001-EDITOR-LEDGER-DISPOSITIONS-2026-11-23): an exact 100/100
bijection with the ledger rows linked to this package, 58 approved records
whose defaults are normative and exercised by the `defaults-<family>` and
`invalid-<family>` conformance vectors, and 42 decided drafts whose defaults
stay non-normative until acceptance evidence exists.
`tools/setting-dispositions/check.py` validates the schema, the bijection and
the family vector coverage, and writes the generated coverage report that
SETCAT-020 demands.
