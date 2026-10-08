# Linux conformance adapter

Thin executable invoked by the shared runner under the protocol in [`../../runner/README.md`](../../runner/README.md).

The `linux-qt` adapter is built from `platforms/linux/linux-qt/conformance-adapter/` as `xerahs-conformance-adapter`, so it links the same production libraries as the application (LINUX-ADAPTER-001).

| Capability | Operations |
|---|---|
| `FILENAME-GENERATION-001` | `expand`, `preview` |
| `POST-CAPTURE-ACTIONS-001` | `run-pipeline`, `map-legacy-flags` |
| `REGION-CAPTURE-001` | `map-selection`, `keyboard-adjust`, `composite-coverage`, `state-events`, `last-region` |
| `EDITOR-SESSION-001` | `history`, `render`, `parse-document`, `round-trip`, `sidecar-path`, `save-sidecar` |
