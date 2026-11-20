# Native dependencies

On 2026-11-02 Mike explicitly approved integrating the installed zxing-cpp 3.1.1
and Tesseract 5.5.3 for local QR scanning and OCR, with explicit unavailable
messages when a dependency is missing. His answer was “Approve both”. He also
authorized applying recommended options automatically during this work.

| Dependency | Checked local version | Upstream licence | Native use |
|---|---|---|---|
| [zxing-cpp](https://github.com/zxing-cpp/zxing-cpp/releases/tag/v3.1.1) | 3.1.1 (`pkg-config --modversion zxing`) | [Apache-2.0](https://github.com/zxing-cpp/zxing-cpp/blob/master/LICENSE) | Dynamically linked QR decoding of user-selected local images |
| [Tesseract](https://github.com/tesseract-ocr/tesseract/releases/tag/5.5.3) | 5.5.3 (`pkg-config --modversion tesseract`) | [Apache-2.0](https://github.com/tesseract-ocr/tesseract/blob/main/LICENSE) | Dynamically linked local text recognition |
| [libgcrypt](https://gnupg.org/software/libgcrypt/index.html) | 1.12.4 (`pkg-config --modversion libgcrypt`) | [LGPL-2.1+](https://github.com/gpg/libgcrypt/blob/master/COPYING.LIB) | Dynamically linked Diffie-Hellman and AES for the encrypted Secret Service session |
| [onnxruntime](https://github.com/microsoft/onnxruntime) | 1.29.0 (`pacman -Qi onnxruntime-cpu`) | [MIT](https://archlinux.org/packages/extra/x86_64/onnxruntime-cpu/) | Approved inference runtime for the Background Remover's local models; optional at build time, dynamically linked, CPU execution first |

## Background-removal model supply

Decided by the loop under its delegated owner-block authority on 2026-11-20
(decision `EU-009-010-011-BACKGROUND-MODEL-SUPPLY-2026-11-20`,
record `product-contract/decisions/EU-009-010-011-background-model-supply.md`).
The approved model is `u2netp` (U-2-Net lightweight salient-object
segmentation, Apache-2.0, https://github.com/xuebinqin/U-2-Net), consumed as
the ONNX artifact pinned in the contract catalogue
`product-contract/capabilities/EDITOR-UTILITIES-001/background-model-catalogue.json`
(GitHub release asset of the MIT-licensed rembg tool; SHA-256 and MD5 pinned,
both verified against the fetched artifact). The application downloads
nothing silently: the model is offered behind an explicit confirmation that
discloses licence, size, exact URL and host, checksums, the model store
directory and that the download is the only network activity; the bytes are
verified before adoption and a mismatch is discarded. RMBG models were
rejected as non-commercial. The inference runtime is onnxruntime (MIT,
packaged in Arch extra as `onnxruntime-cpu 1.29.0`, already used by packaged
desktop apps such as calibre and darktable); it stays optional at build
time exactly like libgcrypt — without it the Background Remover explains
that its inference engine is not linked while model setup still works, and a
build without it is not a platform waiver. The engine is implemented and
tested on this host against the real 1.29.0 runtime: the optional CMake
dependency is discovered with pkg-config (libonnxruntime), the session
cross-checks the catalogue tensor contract at open, CPU is the device
policy, and an explicit GPU request on a CPU-only build is the typed
`model-device-unavailable` failure. Tests run real CPU inference against
hand-encoded executable ONNX fixtures, so no model file is downloaded or
committed; the real 4.6 MB artifact was additionally run once out-of-band
through the production engine. CPU is the device policy for the first
inference build; Auto and GPU report honestly from the linked runtime's
execution providers. No library or model is downloaded at build
time, and the model download itself never happens in tests (local files and
loopback sockets only).

## Secret Service session encryption

Decided by the loop under its delegated dependency authority on 2026-11-19
(decision `CORE-013-ENCRYPTED-SECRET-SESSION-2026-11-19`, review fix A3 of the
ROOT-SECURITY-001 review): the credential store negotiates
`dh-ietf1024-sha256-aes128-cbc-pkcs7` first and falls back to the plain session
only when the service answers `NotSupported`, exactly as libsecret does. The
1024-bit MODP group, the RFC 5869 HKDF-SHA256 derivation (no salt, no info,
16-byte output) and the AES-128-CBC/PKCS#7 encoding follow the published
reference exchange byte for byte; the licence was verified on libgcrypt's own
COPYING.LIB and the algorithm on the libsecret sources cited in the decision
record.

libgcrypt is optional at build time. Without it the store keeps the plain-only
transport and everything else works; a build without it is not a platform
waiver, and distributions that want encrypted sessions install libgcrypt
(>=1.8) and rebuild. The encrypted exchange is proven against an independently
implemented fake service in `SecretServiceCredentialStoreTest`; live
interoperability with a real gnome-keyring/KeePassXC daemon remains separate
desktop qualification.

OCR initially uses the installed English (`eng`) trained data. The checked
machine exposes `eng` and `osd` through `tesseract --list-langs`. Missing library
or language data is an actionable unavailable result. The application does not
download libraries or trained data as part of recognition.

Dependency detection is optional at build time so the minimum Qt build remains
usable and explains missing recognition services. Distributions must provide
and qualify the recognition dependencies to claim those capabilities; an
unavailable build is not an accepted platform waiver or full conformance.

Recognition keeps pixels and text in the utility session. Copy is explicit.
Capture uses the existing desktop permission transport; utility inputs are not
handed to capture save/upload workflows or diagnostic history.

This explicit approval supersedes the older loop/audit question requesting the
same dependency approval. The QR implementation has real decoder, native
Screen/Region portal, file-input, permission-denial and cancellation evidence in
`QrRecognitionTest`, `ImageRecognitionDialogTest` and `PortalScreenshotTest`.
It does not approve unrelated model, credential-store, privacy or release gates.

## Legacy SQLite history

The history chooser dynamically links SQLite (minimum 3.36; checked locally:
3.54.0) to read user-selected legacy history. SQLite is
[public domain](https://sqlite.org/copyright.html), compatible with GPLv3.
`libsqlite3-dev` is required for builds; the packaged application depends on the
shared library. No database engine is downloaded at runtime.

The reader uses a read-only connection and a held read transaction, then the
[online backup API](https://sqlite.org/backup.html) to retain a consistent database
snapshot, including committed WAL data. The original source is never migrated
in place. Extension loading, views and triggers are disabled, and only fixed
queries execute. The native migration candidate still requires the existing
human privacy/security review and shared compatibility qualification.
