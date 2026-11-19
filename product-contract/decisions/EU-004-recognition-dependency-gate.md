# EU-004: recognition dependency gate reconciliation

Decision: EU-004-RECOGNITION-GATE-2026-11-19, resolved by the loop under the
delegated owner-block policy on 2026-11-19. The underlying dependency approval
is Mike's recorded answer on 2026-11-02 ("Approve both").
Contract: 0.4.0; EDITOR-UTILITIES-001: 0.1.0. Requirements: EU-004, with the
same reconciliation covering the OCR flows ordered by PCA-002 and inventoried
by WFCAT-011.

## Problem

Two records disagreed about whether local QR scanning and OCR may be treated
as approved product behavior. `platforms/linux/linux-qt/DEPENDENCIES.md`
records Mike's explicit 2026-11-02 approval of the installed zxing-cpp 3.1.1
and Tesseract 5.5.3 for local recognition, with explicit unavailable messages
when a dependency is missing. A later loop/audit note nevertheless still
described a scheduler that "explicitly still prohibits QR scanning and labels
zxing-cpp unapproved", and on that basis alone the 2026-11-14 audit downgraded
EU-004 from implemented to blocked even though the Screen/Region/File code and
real-decoder tests existed and passed. Which record governs loop activation
and qualification?

## Choice

The recorded product-owner dependency approval governs. The scheduler
prohibition is retired as superseded: the current loop prompt contains no
recognition prohibition, delegates dependency decisions to the loop with
licence verification, and lists the recognition reconciliation as the top
backlog item. EU-004 is classified implemented (candidate) on Linux on the
existing real-decoder evidence; OCR recognition and the PCA-002 pipeline
`ocr`/`ocr-clipboard`/`qr-scan` flows stand on their existing tests with no
further dependency gate.

The approval rests on verification required by the loop's dependency policy:

- zxing-cpp is Apache-2.0
  ([LICENSE](https://github.com/zxing-cpp/zxing-cpp/blob/master/LICENSE),
  verified 2026-11-19), GPL-v3 compatible, dynamically linked, packaged in
  Arch (`zxing-cpp`, installed 3.1.1) and Debian (`libzxing-cpp-dev`).
- Tesseract is Apache-2.0
  ([LICENSE](https://github.com/tesseract-ocr/tesseract/blob/main/LICENSE),
  verified 2026-11-19), GPL-v3 compatible, dynamically linked, packaged in
  Arch (`tesseract`, installed 5.5.4 satisfies the checked >=5.5.3) and Debian
  (`tesseract-ocr`). English (`eng`) trained data already ships with the
  installed package; nothing is downloaded at runtime.
- The behavioural reference itself uses the same upstream ZXing family for QR
  decoding (the pinned ShareX source references ZXing.Net in
  `ShareX/ShareX.csproj`), so the choice matches the leading comparable
  application rather than inventing a stack.

## Impact

No SPEC text changes: the contract deliberately names no platform dependency,
so the contract version stays 0.4.0 and
`platforms/linux/linux-qt/DEPENDENCIES.md` remains the dependency record of
authority. The decision changes classification state only: EU-004 moves to
implemented (candidate) with its existing anchors, and the stale
scheduler-prohibition text is removed from the Linux traceability rows and
production backlog. Optional build-time detection stays: a distribution
without the libraries still gets explicit unavailable messages, and
qualifying those dependencies remains each distribution's job; an unavailable
build is not a platform waiver. Live-desktop portal qualification, physical
mixed-topology capture, accessibility acceptance, all-platform activation,
parity and release acceptance remain separate human gates exactly as before.

This reconciliation grants nothing beyond the recognition scope of Mike's
2026-11-02 answer. Background-removal models (EU-009/010/011), credential
stores, release trust and signing, and every other blocked owner question
remain blocked.
