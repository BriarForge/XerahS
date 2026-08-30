# KovaForge 0.29.0 census provenance

This directory records discovery evidence for the pinned KovaForge/XerahS
baseline. It is evidence about the reference product, not normative product
behavior.

## Current state

The automated source-structure pass is reproducible and feeds all five parity
ledgers. The census remains **open**. Contract authors MAY work from inventoried
rows, but nobody may claim baseline closure or full parity yet.

| BXIP001 section 20.5 source | State | Evidence |
|---|---|---|
| Projects, packages, tools, plugins, submodules | Automated | `kova-0.29.0-census.json` |
| Public enums and registries | Automated, review pending | Census enum inventory and workflow ledger |
| Settings, defaults, serializers, migrations, secrets | Automated, semantic review pending | Census settings inventory and settings ledger |
| GUI routes, views, menus, shortcuts, accessibility and errors | Static surface inventory complete; journey review pending | Census UI inventory and interface ledger |
| CLI, MCP, assistant, daemon, Send To and automation | Static entry-point inventory complete; behavior review pending | Interface and capability ledgers |
| Formats, schemas, databases and protocols | Seeded; fixture-level review pending | Compatibility ledger |
| Tests, docs, XIPs, issues and release notes | Hashed and indexed; semantic review pending | Census input hashes |
| Runtime observation on each baseline platform | Not started | Required manual evidence |

## Closure work still required

1. A source-structure reviewer MUST reconcile dynamic registrations, reflection,
   generated code, stale graph data, dead code, and every automated false
   positive or omission.
2. A separate user-journey reviewer MUST trace every visible action from launch
   through success, cancellation, failure, retry, and recovery.
3. Representative runtime journeys MUST be observed on Windows, macOS, and
   Linux where the pinned baseline runs. Permission and degraded-mode surfaces
   MUST be included.
4. Settings rows MUST be enriched with exact defaults, validation, secret
   classification, migration, and downstream effects before their contracts are
   approved.
5. Compatibility rows MUST gain sanitized fixtures, version/encoding limits,
   migration expectations, and round-trip tests.
6. Each ledger row MUST map to approved requirement IDs before it can move to
   `contracted`.
7. The product owner MUST sign census closure. The generated files do not and
   cannot provide that approval.

The checked-in graph report in KovaForge was built from commit `5bbdb7db`, not
the pinned `5c7e36d` baseline. It is useful navigation evidence but MUST NOT be
used as a closure source until regenerated or reconciled.
