# MOBILE-PLATFORM-001 Native mobile applications

Version: 0.1.0

Status: Approved by the human product owner on 2026-09-28; licensing review (MOB-018) and conformance required before activation

## User intent

Users can capture or receive screenshots, images, videos, files, text, and
links on an Android or iOS device, annotate them with the same editor, name
them with the same patterns, and save, copy, or upload them with the same
honest results they get on the desktop, through an application that feels
native on each phone and tablet.

## Scope and relationship to desktop

- **Mobile targets** are Android and iOS, including tablets. They are
  supported native targets alongside the Windows, macOS, and Linux desktop
  targets.
- Every approved capability applies to mobile only as declared in
  `mobile-scope.json`. A capability or requirement not listed there has no
  mobile obligation until the product owner adds it.
- Desktop parity with the pinned baseline remains measured on desktop only.
  The baseline's experimental mobile projects (`MOBILE-EXPERIMENTAL-001`) are
  replaced by these native applications, not ported.

## Requirements

- **MOB-001:** The Android application ID and the iOS bundle identifier MUST be
  `com.xerahs.app` and the display name MUST be `XerahS`. Extensions MUST use
  identifiers beneath it, such as `com.xerahs.app.share`.
- **MOB-002:** `mobile-scope.json` is normative. For each capability it declares
  an Android and an iOS disposition (`required`, `equivalent`, `degraded`,
  `unavailable`, or `not-applicable`) and the adaptation that applies. A
  mobile implementation MUST satisfy every applicable requirement of a listed
  capability under that disposition. Changing a disposition requires the
  product owner. A selected post-capture action whose mobile disposition is
  `not-applicable` MUST be `skipped` with diagnostic `platform-not-applicable`,
  which takes precedence over dependency checks, so imported desktop workflows
  run honestly.
- **MOB-003:** The application MUST accept images, videos, files, text, and URLs
  from the system share sheet (Android `ACTION_SEND` and `ACTION_SEND_MULTIPLE`;
  an iOS share extension). Each shared item MUST start one post-capture
  pipeline in received order, using the mobile workflow and the same action
  engine, result model, and diagnostics as `POST-CAPTURE-ACTIONS-001`. Each item
  MUST be classified as `image` (an `image/*` type), `video` (a `video/*` type),
  `url` (text that is exactly one absolute `http` or `https` URL, or a URL
  item), `text` (other text), or `file` (any other readable content). An item
  that is empty or cannot be read MUST produce `share-item-unsupported` without
  affecting the other items.
- **MOB-004:** On Android, screen capture MUST use the MediaProjection consent
  flow for each capture session, show the required foreground-service
  notification while projection is active, and stop projection when the
  session ends. Region selection then applies `REGION-CAPTURE-001` to the
  captured frame as a single display at the frame's physical size.
- **MOB-005:** iOS does not permit capturing other applications' screens on
  demand. The iOS application MUST instead offer the latest screenshot and a
  photo picker as capture sources, requesting only the Photos access it needs
  (the system picker needs none), and MUST state this limitation where users
  choose a capture source. Region selection applies to the imported image as
  in MOB-004.
- **MOB-006:** Screen recording, where the recording capability is contracted,
  MUST use MediaProjection with MediaCodec on Android and a ReplayKit broadcast
  upload extension on iOS, with a visible system recording indicator and an
  explicit stop control.
- **MOB-007:** Touch input MUST map to the same editor operations and history
  boundaries as pointer input: drag creates or moves, handles resize and
  rotate, two-finger pinch and pan change only the viewport, and long-press
  opens the context actions a secondary click opens on desktop. An attached
  hardware keyboard and pointer MUST work as on desktop. Export output MUST
  meet `EDITOR-SESSION-001` pixel rules unchanged.
- **MOB-008:** In filename expansion on mobile, `%t`, `%pn`, `%un`, and `%uln`
  MUST expand to empty strings, `%width` and `%height` to the capture's
  physical pixel size, and `%cn` to the user-visible device name where the
  operating system provides it without an extra entitlement, otherwise to an
  empty string.
- **MOB-009:** Saving to the shared photo library MUST use MediaStore on Android
  and add-only photo library access on iOS, placing items in an album or
  collection named `XerahS`. Other files MUST be saved through the system
  document picker or app storage. The saved result MUST report the platform
  content identifier in place of an absolute path.
- **MOB-010:** Uploads MUST continue when the application moves to the
  background by using WorkManager on Android and a background `URLSession` on
  iOS, report progress through a system notification or Live Activity where
  permitted, and apply `POST-CAPTURE-ACTIONS-001` recovery rules so an unknown
  remote outcome is never reported as failed.
- **MOB-011:** Credentials and uploader secrets MUST be stored with Android
  Keystore-backed encryption or in the iOS Keychain with a this-device-only
  accessibility class, MUST be excluded from backups and device transfer, and
  MUST never be written to shared containers.
- **MOB-012:** Permissions (screen capture, photos, notifications, camera,
  microphone) MUST be requested at the point of use with a native explanation,
  MUST NOT be requested again in a loop after denial, and MUST produce a typed
  diagnostic and a route to system settings when denied.
- **MOB-013:** The application MUST survive process termination by the
  operating system without losing a pipeline, edit session, or upload: it MUST
  persist the state required by `POST-CAPTURE-ACTIONS-001` PCA-012 and
  `EDITOR-SESSION-001` ES-018 and restore it on the next launch.
- **MOB-014:** Share and broadcast extensions MUST stay within their operating
  system memory and time limits by handing encoding, editing, and upload work
  to the main application or a background transfer. They MUST NOT hold
  credentials beyond a revocable token scoped to that hand-off.
- **MOB-015:** The applications MUST support TalkBack and VoiceOver, system font
  scaling up to the largest accessibility size, reduced motion, high
  contrast, and touch targets of at least 48 dp on Android and 44 pt on iOS.
- **MOB-016:** Settings MUST use the shared settings schema. Exported settings
  MUST import on desktop and mobile; settings that do not apply on the current
  target MUST be preserved unchanged on round trip and MUST NOT be shown as
  editable. `.xann` documents and custom uploader definitions MUST open on
  mobile under their compatibility contracts.
- **MOB-017:** Desktop-only surfaces (global hotkeys, tray, window and monitor
  capture, watch folders, CLI, MCP hosting, shell integration, and plugin
  hosting) are `not-applicable` on mobile. Mobile automation MUST use Android
  intents and iOS App Intents and Shortcuts that invoke the same shared
  operations with the same consent rules.
- **MOB-018:** Mobile distribution MUST satisfy ROOT-LICENSE-001. Android
  releases MAY use Google Play, F-Droid, and signed APKs. An iOS release MUST
  NOT be distributed through a channel whose terms conflict with GPL v3 until
  a licensing review records an approved path, such as an additional
  permission from the copyright holders or a compatible distribution channel.
- **MOB-019:** Minimum supported versions are Android 10 (API level 29) and
  iOS 17. Implementations MUST degrade by accepted disposition rather than
  crash when an optional platform feature is missing.
- **MOB-020:** Mobile implementations MUST run the shared conformance vectors of
  every capability they implement through headless test seams (Android
  instrumented tests; XCTest) under `product-contract/VECTORS.md`, and MUST
  pass the vectors in this package's `test-vectors.json`.

## Native adaptation

Android follows Material 3 and adaptive layouts for phones, foldables, and
tablets. iOS and iPadOS follow the Human Interface Guidelines, including
multitasking on iPad. Presentation, navigation, and gestures MAY differ between
platforms; operation results, file names, pixels, diagnostics, and persisted
formats MUST NOT.

## Baseline traceability and disposition

Ledger link: `MOBILE-EXPERIMENTAL-001`, evidence `src/mobile-experimental`
(Avalonia and MAUI projects and an iOS share extension) at baseline commit
`5c7e36dea77ab131fe0f5e2101e5d578ccde0306`. Disposition: replace with native
Android and iOS applications built from this contract; the experimental
projects are not a source of product behavior.
