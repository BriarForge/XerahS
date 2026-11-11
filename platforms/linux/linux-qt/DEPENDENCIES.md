# Local recognition dependencies

On 2026-11-02 Mike explicitly approved integrating the installed zxing-cpp 3.1.1
and Tesseract 5.5.3 for local QR scanning and OCR, with explicit unavailable
messages when a dependency is missing. His answer was “Approve both”. He also
authorized applying recommended options automatically during this work.

| Dependency | Checked local version | Upstream licence | Native use |
|---|---|---|---|
| [zxing-cpp](https://github.com/zxing-cpp/zxing-cpp/releases/tag/v3.1.1) | 3.1.1 (`pkg-config --modversion zxing`) | [Apache-2.0](https://github.com/zxing-cpp/zxing-cpp/blob/master/LICENSE) | Dynamically linked QR decoding of user-selected local images |
| [Tesseract](https://github.com/tesseract-ocr/tesseract/releases/tag/5.5.3) | 5.5.3 (`pkg-config --modversion tesseract`) | [Apache-2.0](https://github.com/tesseract-ocr/tesseract/blob/main/LICENSE) | Dynamically linked local text recognition |

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
