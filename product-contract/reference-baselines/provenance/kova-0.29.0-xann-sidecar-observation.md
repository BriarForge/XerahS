# KovaForge 0.29.0 — runtime observation of the pinned XANN sidecar behaviour

Scope: **ES-030 runtime baseline observation only** (empty-save sidecar policy).
This is evidence about the pinned reference product, not normative product behaviour.
It records what the pinned baseline library *actually does at runtime*, not what its
source appears to do.

Status: **observation succeeded.** ES-030's "baseline companion" precondition is now
backed by runtime fact rather than source reading.

## 1. Provenance of the observed baseline

| Item | Value |
|---|---|
| Reference repository | `/home/mike/Work/ShareX/XerahS/ShareX.ImageEditor` (read-only) |
| Observed commit | `651b1d8de4bc1f874790870560314670cd038684` |
| Observed commit date | Fri Aug 28 06:42:25 2026 +0800 |
| Observed commit subject | `[Fix] Scope toolbar icon fonts to button content` |
| Parent commit (kova 0.29.0) | `5c7e36dea77ab131fe0f5e2101e5d578ccde0306` |
| Reference repo working tree | unmodified — `git-nadia status --short` empty after the run |
| Reference repo HEAD during observation | `0bf2b60` (the reference checkout is ahead of the pinned commit; the pinned commit was extracted by archive, not checked out) |
| Extracted source tree | 696 files; tree-content SHA-256 `6111f546e282d234adf5f1f58b3fc90042f30f051b474d8e3b0c29939f5c2377` |
| Observed service source SHA-256 | `033e2616e462e0d533f9fedcfecd4989a8d9710d1048086a1442a98d7a00a92f` (`src/ShareX.ImageEditor/Core/Persistence/XannProjectFileService.cs`) |
| Built baseline assembly SHA-256 | `d6ed351920fa2c5d54c9c6e267b0695e4e7d7fa51ba034c5c60d63edea85c81a` (`ShareX.ImageEditor.dll`, Release, net10.0) |
| Observing harness source SHA-256 | `96309ab8bd8a71d7c12288cc486937260358ae125d9e0c5263191c0222ea96b7` (`Program.cs`) |
| Observing harness project SHA-256 | `314bcd82d51f8c067fc62d61f66404eb738ce5c7715002bac234dc68f2bc8f6c` (`Harness.csproj`) |
| Harness run output SHA-256 | `d5cab05df414987952bbf793cae70c9308a0dfcec8160e0d175330e550c81e3b` (`run-output.txt`) |

Observed source file, for review:

- `src/ShareX.ImageEditor/Core/Persistence/XannProjectFileService.cs`
- Observed API surface (public members of the pinned library):
  - `public static string GetDefaultSidecarPath(string imagePath)`
  - `public static bool TryDeleteSidecar(string imagePath)`
  - `public static Task<string?> SaveAsync(string imagePath, SKBitmap sourceImage, IEnumerable<Annotation> annotations, DateTimeOffset? createdAt = null, CancellationToken cancellationToken = default)`
  - `public static Task<XannProjectLoadResult> LoadAsync(string sidecarPath, string? expectedImagePath = null, CancellationToken cancellationToken = default)`
  - `public const string FileExtension = ".xann"`, `public const int CurrentVersion = 1`
  - The zero-annotation branch is reached through the **private** `Save` method, so the
    only public entry to that behaviour is `SaveAsync`.

### 1.1 Observation host

| Item | Value |
|---|---|
| OS | Omarchy (Arch-based), `Linux omarchy 7.2.11-arch1-Watanare-T2-1-t2 #1 SMP PREEMPT_DYNAMIC` |
| Architecture | x86_64 |
| .NET SDK | 10.0.404 (installed SDKs 10.0.401–10.0.404) |
| Target framework of observed library | `net10.0` |
| NuGet sources | `nuget.org` only (`https://api.nuget.org/v3/index.json`, enabled) |
| Package cache | `NUGET_PACKAGES=/tmp/…/nuget-packages` (scratch-local, not the user cache) |
| Secrets / accounts / system install | none used; no credential, no account, no system-wide install |
| Private XDG | `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_STATE_HOME`, `XDG_CACHE_HOME`, `XDG_RUNTIME_DIR` and `HOME` were all redirected into the scratch run directory before any library call |
| Scratch root | `/tmp/xerahs-es030-Q4mjQQ` |

## 2. Exact commands

Every command below was run from the scratch root. The reference repository was only
ever read, and only through `git-nadia`.

```bash
# 1. Read-only extraction of the pinned commit into scratch.
git-nadia -C /home/mike/Work/ShareX/XerahS/ShareX.ImageEditor archive \
  --format=tar 651b1d8de4bc1f874790870560314670cd038684 \
  | tar -x -C /tmp/xerahs-es030-Q4mjQQ/archive

# 2. Restore the pinned baseline library (public nuget.org only).
export DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
export NUGET_PACKAGES=/tmp/xerahs-es030-Q4mjQQ/nuget-packages
cd /tmp/xerahs-es030-Q4mjQQ/archive/src/ShareX.ImageEditor
dotnet restore ShareX.ImageEditor.csproj

# 3. Build the pinned baseline library.
dotnet build ShareX.ImageEditor.csproj -c Release --no-restore

# 4. Build the observing harness (project reference to the pinned library).
cd /tmp/xerahs-es030-Q4mjQQ/harness
dotnet build -c Release

# 5. Run the observation.
export ES030_SCRATCH=/tmp/xerahs-es030-Q4mjQQ/run
dotnet bin/Release/net10.0/Es030Harness.dll
```

Build and run results: library restore succeeded (12.98 s), library build succeeded
(9.27 s, 0 warnings, 0 errors), harness build succeeded (1.33 s, 0 warnings, 0 errors),
harness run exited 0.

## 3. Observations

Every line below is **EXTRACTED** — a runtime fact produced by the harness above.
Nothing in this section is inferred from reading the source.

### 3.1 `GetDefaultSidecarPath("d/shot.png")`

**EXTRACTED**

```
STEP1_INPUT_IMAGE_PATH=d/shot.png
STEP1_RETURNED_SIDECAR_PATH=d/shot.xann
STEP1_FILE_EXTENSION_CONST=.xann
STEP1_CURRENT_VERSION_CONST=1
```

The sidecar path is the image path with its extension replaced by `.xann`, in the same
directory. For the literal relative input `d/shot.png` the result is `d/shot.xann`
(relative, no absolute path is introduced).

### 3.2 Save with exactly one annotation

**EXTRACTED**

```
STEP2_IMAGE_PATH=/tmp/xerahs-es030-Q4mjQQ/run/work/shot.png
STEP2_IMAGE_EXISTS=True
STEP2_IMAGE_SHA256=6c9b67a1992f8f588390c8521129945a03f57cfd1b9073913236d5aeeff0ff2f
STEP2_IMAGE_LENGTH_BYTES=196
STEP2_RETURNED_SIDECAR_PATH=/tmp/xerahs-es030-Q4mjQQ/run/work/shot.xann
STEP2_SIDECAR_EXISTS_BEFORE=False
STEP3_ANNOTATION_TYPE=ShareX.ImageEditor.Core.Annotations.RectangleAnnotation
STEP3_ANNOTATION_COUNT_IN=1
STEP3_RETURNED_PATH=/tmp/xerahs-es030-Q4mjQQ/run/work/shot.xann
STEP3_RETURNED_PATH_IS_NULL=False
STEP3_SIDECAR_EXISTS_AFTER=True
STEP3_SIDECAR_LENGTH_BYTES=649
STEP3_SIDECAR_SHA256=afa30fbf371eddc3f8b755414191482a18b16827e183e714167a21e968e20d34
STEP3_TMP_FILE_EXISTS=False
STEP3_DIRECTORY_LISTING=shot.png,shot.xann
```

The saved sidecar was then read back through the library's own public loader:

```
STEP3_LOADED_VERSION=1
STEP3_LOADED_IMAGE_PATH_FIELD=shot.png
STEP3_LOADED_IMAGE_HASH_FIELD=sha256:6c9b67a1992f8f588390c8521129945a03f57cfd1b9073913236d5aeeff0ff2f
STEP3_LOADED_CANVAS_WIDTH=64
STEP3_LOADED_CANVAS_HEIGHT=48
STEP3_LOADED_SOURCE_PNG_BASE64_LENGTH=280
STEP3_LOADED_ANNOTATION_COUNT=1
STEP3_LOADED_ANNOTATION_TYPES=RectangleAnnotation
STEP3_LOADED_IMAGE_HASH_MATCHES=True
```

Facts established: the one-annotation save **writes** a `.xann` sidecar next to the
image, returns that sidecar path (non-null), persists exactly one annotation, records
the source raster's SHA-256 prefixed with `sha256:` in the `imageHash` field, records
the canvas dimensions, and round-trips through `LoadAsync` with the image hash
matching. No `.tmp` file was left behind.

### 3.3 Save with zero annotations (the empty-save case)

**EXTRACTED**

```
STEP4_ANNOTATION_COUNT_IN=0
STEP4_RETURNED_PATH=<null>
STEP4_RETURNED_PATH_IS_NULL=True
STEP4_SIDECAR_EXISTS_AFTER=False
STEP4_SIDECAR_LENGTH_BYTES=<absent>
STEP4_TMP_FILE_EXISTS=False
STEP4_DIRECTORY_LISTING=shot.png
```

Facts established: the empty save **deleted** the previously written sidecar (the
directory listing went from `shot.png,shot.xann` to `shot.png`, and
`File.Exists` on the sidecar path became `False`), returned **null** (not an empty
string, not the sidecar path), and left no `.tmp` residue.

### 3.4 Deletion is reported accurately and is idempotent

**EXTRACTED**

```
STEP5_TRY_DELETE_SIDECAR_WHEN_ABSENT_RETURNED=False
STEP5_SIDECAR_EXISTS=False
STEP6_RETURNED_PATH=<null>
STEP6_SIDECAR_EXISTS=False
STEP6_DIRECTORY_LISTING=shot.png
```

`TryDeleteSidecar` returns `false` when no sidecar is present (it does not throw), and
a second zero-annotation save on an already-clean directory again returns null and
writes nothing.

### 3.5 Summary of the observed empty-save contract

**EXTRACTED**

| Observation | Runtime fact |
|---|---|
| Sidecar name | sibling of the image, extension replaced by `.xann` |
| Save with ≥1 annotation | writes the sidecar, returns its path |
| Save with 0 annotations | **deletes** the sidecar, returns `null` |
| Sidecar content on write | version `1`, source image SHA-256 (`sha256:` + lowercase hex), canvas size, base64 PNG source raster, annotation list |
| Removal semantics | destructive and one-way: the annotations are destroyed, not retained |
| Reversibility | none in the baseline: deleting the default companion restores nothing, because the baseline keeps no second copy |

## 4. Inference (clearly separated from the observations above)

These are **INFERRED** readings of the extracted facts. They are not directly observed
behaviour, and each one is stated so it can be checked against the contract.

1. **INFERRED** — The baseline's empty save is a *destructive delete*, not a write of a
   zero-annotation companion. There is no state in which the baseline retains
   annotations while presenting an empty document. This follows from §3.3: the sidecar
   that held the annotation was removed from the filesystem.
2. **INFERRED** — The baseline provides no shadow/companion mechanism of its own. The
   only two persisted states are "sidecar present" and "sidecar absent", and the
   transition between them is driven only by annotation count. An explicit empty save
   therefore cannot, in the baseline, hide a baseline-named companion while preserving
   it.
3. **INFERRED** — Because the baseline deletes the sidecar on empty save, the
   shadow-persistence design decided in `ES-014-030` (write a zero-annotation default
   companion instead of deleting, so the baseline file stays untouched and hidden) is
   **not** baseline behaviour. It is XerahS-owned behaviour and must be justified as a
   divergence from the observed baseline rather than as conformance to it.
4. **INFERRED** — The observed `sha256:`-prefixed, lowercase-hex source hash is the
   same shape the ES-013 source binding uses, so the shadow companion can carry the
   same field values without a format change. This is a shape comparison against the
   contract, not a runtime observation of ES-013.
5. **INFERRED** — The sidecar path derivation depends only on the image path string
   (§3.1) and not on image content, file existence, or configuration, so a shadow
   companion's existence can be decided by path alone, and the baseline file itself
   never has to be opened to make that decision.

## 5. Limits of this observation

- Observed on Linux only. The behaviour is pure path and file-system logic in a
  platform-neutral library, but Windows and macOS dispositions are not observed here.
- The zero-annotation path was exercised through `SaveAsync`, the only public entry
  point. The private `Save` method was not called directly and was not modified.
- One annotation type (`RectangleAnnotation`) was used for the non-empty case; the
  observation concerns annotation *count*, not per-type rendering.
- No GUI, editor session, or end-to-end user journey was observed. This is a library
  level observation, and it does not by itself close the wider census
  "runtime observation on each baseline platform" row in `README.md`.
- The observation confirms the sidecar *existence and deletion* semantics and the
  persisted field shape. It does not assert anything about XerahS's own Linux
  implementation.

## 6. Reproduction

Scratch root: `/tmp/xerahs-es030-Q4mjQQ`

| Path | Contents |
|---|---|
| `archive/` | read-only extraction of the pinned commit (696 files) |
| `harness/Program.cs` | observing harness source |
| `harness/Harness.csproj` | harness project, project reference to the pinned library |
| `run-output.txt` | complete harness stdout of the recorded run |
| `environment.txt` | toolchain and host details |
| `run/work/shot.png` | the source raster used by the run |
| `nuget-packages/` | scratch-local NuGet package cache |

The harness is self-contained: it writes only inside the scratch root, redirects every
XDG base directory and `HOME` into the scratch root before touching the library, uses
no credential and no account, and modifies neither the reference repository nor any
user configuration.
