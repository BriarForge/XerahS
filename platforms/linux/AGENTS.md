# Scope
Applies to: platforms/linux/**
Parent: ../AGENTS.md

# Purpose
Owns the native Linux editions under D-LIN-001 and D-LIN-002. `linux-qt` is the supported reference edition.

# Local Rules
- **LINUX-TOOLCHAIN-001** `linux-qt` builds with CMake 3.21 or later, a C++17 compiler, and Qt 6.4 or later, dynamically linked (ROOT-LICENSE-001). Qt 6.4.2 on Ubuntu 24.04 LTS is the minimum tested baseline; raising the minimum is a recorded toolchain change in this file.
- **LINUX-CORE-001** Product logic that has a conformance seam lives in `linux-qt/src/` libraries that depend only on Qt Core, so the adapter and the application share one production code path.
- **LINUX-ADAPTER-001** `linux-qt/conformance-adapter/` is the Linux adapter for the shared runner. It only translates JSON to production calls and injected inputs; it holds no product logic.
- **LINUX-TRACE-001** Each implemented capability has `linux-qt/traceability/<CAPABILITY>.json` (PLATFORM-TRACE-001), and its ID is added to `XERAHS_IMPLEMENTED_CAPABILITIES` in `linux-qt/CMakeLists.txt` in the same change.

# Verification
From `platforms/linux/linux-qt`:

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

# Prohibited Changes
- Static linking of Qt.
- Product logic inside the conformance adapter.

# Escalation
Toolkit or edition changes follow D-LIN-002 and need the product owner.

# Child Scopes
None.
