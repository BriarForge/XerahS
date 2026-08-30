# EDITOR-UTILITIES-001 Image utility suite

Version: 0.1.0

Status: Draft; product-owner approval required

## Requirements

- **EU-001:** The editor start surface MUST make every utility in
  `utility-catalog.json` reachable or record an approved platform disposition.
  Missing host services MUST be explained and MUST NOT crash utility startup.
- **EU-002:** Screen Color Picker MUST request required capture permission,
  magnify the sampled area, expose coordinates and canonical color values,
  support keyboard cancellation, and copy only after explicit user action.
- **EU-003:** QR generation MUST validate non-empty content and output size,
  render a deterministic scannable preview, and support explicit copy, save, and
  upload through host services with typed failures.
- **EU-004:** QR scanning MUST support Screen, Region, and Image File modes,
  obtain capture/file consent, validate and limit input, return zero, one, or
  multiple decoded values distinctly, and never upload sampled pixels.
- **EU-005:** Hash Checker MUST offer CRC32, MD5, SHA-1, SHA-256, SHA-384, and
  SHA-512, default to SHA-256, stream files without unbounded memory, expose
  progress/cancellation, and compare two results using constant-time equality
  where security-sensitive use is claimed.
- **EU-006:** Hash results MUST identify the algorithm and file unambiguously;
  MD5 and SHA-1 MUST be labelled unsuitable for security verification and MUST
  NOT be recommended for authenticity decisions.
- **EU-007:** Icon Converter MUST accept a validated image, offer 16, 32, 48,
  64, 128, and 256-pixel entries, support 8-bit palette and 32-bit true-color
  modes, retain alpha as permitted, and write a structurally valid `.ico` only
  after at least one size is selected.
- **EU-008:** Image Comparison MUST load two independent validated images,
  report size/format incompatibility, expose an accessible before/after slider
  or equivalent, and MUST NOT modify, save, or upload either image.
- **EU-009:** Background Removal MUST use an explicitly selected local model and
  Auto, CPU, or GPU device policy, validate model provenance and tensor shapes,
  show model/device/setup/inference status, and retain the source on failure.
- **EU-010:** Background-removal models MUST NOT be downloaded or executed from
  untrusted locations silently. Model directory access, optional download,
  license, integrity, size, and network behavior require explicit disclosure.
- **EU-011:** Background-removal preview and save MUST preserve source dimensions
  and color, produce defined alpha, support cancellation, and distinguish model,
  device, memory, inference, and output failures.
- **EU-012:** Video Converter MUST expose the 15 codec choices recorded in the
  utility catalog, input, output folder/name, quality or bitrate where applicable,
  progress, cancellation, and optional open-folder behavior.
- **EU-013:** Hardware video codecs MUST be shown only when usable or must fail
  with an actionable fallback. Command arguments MUST be structured or safely
  escaped, MUST NOT permit injection, and output MUST be written atomically.
- **EU-014:** The converter MUST validate container/codec/input compatibility,
  reserve a non-conflicting output path, preserve the input, delete incomplete
  output on cancellation where safe, and distinguish success, cancellation, and
  failure in its host result.
- **EU-015:** Utility file and screen inputs are private user data. No utility
  MUST send them over a network, retain them after the declared session, or add
  them to diagnostics without explicit user action and a separately approved
  integration contract.
- **EU-016:** Utility windows MUST use native accessible controls, visible focus,
  keyboard traversal, descriptive busy/error state, cancel/close semantics,
  high contrast, and native file/capture permission surfaces on all platforms.
- **EU-017:** Each utility MUST own cancellation and dispose images, streams,
  models, sessions, and temporary files deterministically. Closing a busy window
  MUST cancel or explicitly confirm background continuation.
- **EU-018:** Conformance MUST cover each catalog item, command, algorithm,
  codec, input mode, success, cancellation, corrupt input, permission denial,
  missing host, unsupported hardware, resource limit, and output failure.
- **EU-019:** The hidden Konami-code shader animation MUST remain inventoried
  but MUST NOT become required product behavior until the product owner records
  a preserve, correct, or omit decision after accessibility, performance,
  security, discoverability, and maintenance review.

## Baseline traceability and disposition

The pinned UI includes start screen, screen color picker, QR, hash checker, icon
converter, image comparer, background remover, and video converter windows. The
candidate has no dedicated test project at the pinned commit; this draft
therefore makes contract-derived fixtures and platform runtime journeys a
precondition for approval rather than treating reachability as correctness.
