# Contract linter

Validates the governance and Product Contract structure required by D-AGT-001,
D-CON-001, and TOOL-LINT-001 without third-party Python dependencies.

From the repository root:

```powershell
python tools/contract-linter/lint.py
python -m unittest discover tools/contract-linter/tests -v
```

The linter checks:

- the root-to-leaf `AGENTS.md` parent chain, maximum depth, and unique rule IDs;
- manifest SemVer, capability paths, lifecycle files, and exact requirement-ID
  agreement between the manifest and each `SPEC.md`;
- human product-owner approval records covering every approved or active
  capability at the manifest contract version;
- JSON syntax and the presence of JSON Schema dialect declarations;
- all required baseline/parity artifacts, row fields, unique ledger IDs, source
  evidence, platform dispositions, and valid contract links;
- the pinned baseline commit and submodule identifiers.

It reports policy violations and never edits contracts or grants waivers.

`fixtures/good/` and `fixtures/bad/` are the pinned known-good and known-bad
instruction fixture repositories used by the self-tests. They are excluded from
the repository's own instruction-hierarchy scan.
