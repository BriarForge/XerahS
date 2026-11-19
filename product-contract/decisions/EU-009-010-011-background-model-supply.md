# EU-009/010/011: background-removal model supply

Decision: `EU-009-010-011-BACKGROUND-MODEL-SUPPLY-2026-11-20`, resolved by the
loop under the maintainer's delegated owner-block authority on 2026-11-20
(agent opencode, GLM 5.3).
Contract: 0.4.5; EDITOR-UTILITIES-001: 0.1.1. Requirements: EU-009, EU-010,
EU-011.

## Problem

The SPEC requires Background Removal to run "an explicitly selected local
model" with provenance and tensor-shape validation, explicit disclosure and
opt-in for any model download (licence, integrity, size, directory, network
behaviour), and defined alpha output with cancellation and typed failures.
The shared loop state held the requirement blocked as "Mike: approve local
background-removal model, license/provenance and device policy". No model,
supply channel, runtime or tensor contract was recorded, so nothing could be
built or tested against an approved source.

## Choice

**Model: `u2netp`**, the lightweight U-2-Net salient-object-segmentation
model (Qin et al., Pattern Recognition 2020), consumed as the ONNX artifact
published at the pinned GitHub release URL
`https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2netp.onnx`
(4,574,861 bytes). Integrity is pinned by both
SHA-256 `309c8469258dda742793dce0ebea8e6dd393174f89934733ecc8b14c76f4ddd8`
and the upstream MD5 `8e83ca70e441ab06c318d82300c84806` (rembg's own pin);
the sha256 was computed from the fetched artifact and the md5 was then
confirmed equal to rembg's published pin, so the two records are
independently sourced. The tensor contract is the artifact's own header,
read from its bytes: input `input.1` float32 `[1,3,320,320]`, first output
`1959` float32 `[1,1,320,320]` (the artifact also carries five auxiliary
outputs, some with symbolic dims; the mask policy reads the first output
only). Preprocessing is the U-2-Net preprocessing: RGB, resized 320x320,
ImageNet mean (0.485, 0.456, 0.406), std (0.229, 0.224, 0.225); the first
output is min-max normalized to the output alpha and resized to the source
dimensions. This catalogues the whole model supply in
`capabilities/EDITOR-UTILITIES-001/background-model-catalogue.json`, which
the application loads as product data.

**Rejected:** `briaai` RMBG-1.4/2.0 is state of the art for this task but
is released under BRIA's non-commercial licence (rembg's README names this
explicitly and the loop's dependency policy rejects non-commercial licences
outright), and the 1.02 GB / 1024x1024 RMBG-2.0 is also far outside a
CPU-first default. The full `u2net` (176 MB) is a possible later catalogue
addition but was not pinned here; a second pinned model needs its own
verified checksum record.

**Runtime (approved for the inference build, not linked in this slice):**
onnxruntime (MIT, `onnxruntime-cpu 1.29.0` packaged in Arch extra),
dynamically linked, optional at build time exactly like libgcrypt — a build
without it keeps the utility usable for model setup and reports the
inference engine as not linked (the EU-001 missing-host explanation), never
as a platform waiver. CPU is the device policy for the first inference
build; Auto and GPU selection are reported honestly from the linked
runtime's execution providers when that runtime exists.

**Supply behaviour:** the application never downloads a model silently.
The Background Remover window discloses, for the selected model, the
upstream project, the licence with its URL, the exact artifact URL and host,
the byte size, the pinned SHA-256 and MD5, the on-disk model store
directory, and that the one-time download is the only network activity (no
image data is ever uploaded). The download starts only after an explicit
confirmation. The download lands in a temporary sibling file, is verified
against the pinned SHA-256 and MD5 and size and its ONNX header must match
the catalogue's tensor contract; only then is it atomically renamed into the
store. Any failure discards the temporary file and keeps the previous state.
A user may instead explicitly select any local `.onnx` file; its header is
parsed and its tensor contract validated and displayed, with the provenance
honestly reported as user-supplied and checksum-unverified.

## Sources (read 2026-11-20)

- U-2-Net official repository, https://github.com/xuebinqin/U-2-Net :
  Apache-2.0 licence; the README documents `u2netp.pth` (4.7 MB) as the
  lightweight version, 320x320 as the pretrained input size, and records
  that Pixelmator Pro uses U-2-Net as one of its background-removal models.
- rembg, https://github.com/danielgatis/rembg (MIT): the leading open-source
  local background-removal tool. The Models section pins the same artifact
  URL, states models are downloaded on demand under `~/.rembg/models/` with
  checksum verification, and states "Model weights carry their own licenses,
  independent of rembg's MIT license", plus the BRIA non-commercial warning
  that rules RMBG out. Its `rembg/sessions/u2netp.py` pins the MD5
  `8e83ca70e441ab06c318d82300c84806` and defines the exact preprocessing
  (320x320, mean (0.485, 0.456, 0.406), std (0.229, 0.224, 0.225)) and the
  first-output min-max mask policy this decision adopts.
- The fetched artifact itself: header parsed from its bytes (input `input.1`
  [1,3,320,320] float32, first output `1959` [1,1,320,320] float32), sha256
  computed as above, md5 equal to rembg's pin.
- onnxruntime, https://onnxruntime.ai and the Arch package page
  https://archlinux.org/packages/extra/x86_64/onnxruntime-cpu/ : MIT licence,
  packaged in Arch extra (1.29.0), already a dependency of packaged desktop
  applications (calibre, darktable, crow-translate).
- TechSmith Snagit features, https://www.techsmith.com/snagit/features/ :
  "Background remover — Create clean, professional visuals by removing the
  background of your screenshots" is a stock editing feature of the leading
  commercial capture tool, which establishes the capability as in-scope for
  a capture-and-share tool rather than an image-editor extra.

## Impact

EU-010 moves to implemented (candidate): the disclosure set, explicit
opt-in, pinned-integrity download and model store are implemented and tested
offscreen with no network (the artifact is served from a local file or a
loopback socket in tests). EU-009 moves to partial: explicit model
selection, catalogue provenance validation, ONNX header parsing with
tensor-shape validation, and model/device/setup/inference status rows are
implemented; the Auto/CPU/GPU device policy and inference status become
live when the approved runtime is linked. EU-011 remains not-started with its
blocker resolved: preview/save with defined alpha, cancellation and the
model/device/memory/inference/output failure split are the next slice, built
on this approved supply. Expected test values come from the catalogue and the
pinned artifact's header, not from the implementation. Live verification
with the real 4.6 MB artifact and real CPU inference, and any visual-quality
acceptance, remain separate desktop qualification and are not claimed here.
