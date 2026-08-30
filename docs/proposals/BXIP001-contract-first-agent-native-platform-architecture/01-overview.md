# 1. Executive Summary

Cross-platform UI frameworks historically reduced the cost of implementing and maintaining the same application on multiple operating systems. Agentic development changes that cost model: AI agents can implement the same feature independently in the native framework of each operating system with much less manual authoring effort than before.

This does not make cross-platform consistency automatic. It moves the primary engineering problem from code reuse to product governance, specification quality, conformance testing, and coordinated release management.

This proposal asks that XerahS treat a version-controlled **Product Contract** as the conceptual replacement for the common cross-platform DLL. The Product Contract is written primarily in precise plain English so that humans and AI agents can understand it. It is strengthened by executable scenarios, schemas, state machines, fixtures, and test vectors wherever prose alone would be ambiguous.

Implementation will occur as a greenfield project in the [BriarForge/XerahS](https://github.com/BriarForge/XerahS) repository. Full desktop functional parity is measured against the clean [KovaForge/XerahS](https://github.com/KovaForge/XerahS) baseline pinned in [section 20](11-reference-baseline.md). The ShareX Team repository remains historical and architectural reference material. Neither reference tree dictates the new repository structure or becomes a production dependency.

Under the proposed target architecture:

1. The Product Contract defines what XerahS does.
2. Windows, macOS, and Linux implementations define how each operating system delivers that behavior natively.
3. A shared conformance system proves that the three implementations remain one product.
4. Platform-specific differences are explicit, justified, reviewable, and time-bound where appropriate.
5. A hierarchy of scoped `AGENTS.md` files communicates the applicable governance to each agent at the point of implementation.
6. The existing Avalonia application remains supported while the greenfield implementation is developed and validated. This proposal does not authorize removal of the existing application before full-parity release evidence.
7. [Architecture decisions](08-decisions.md) bind the implementation for ownership, baseline, contract format, platform baselines, repository shape, sharing, plugins, release, review, instruction hierarchy, golden-image tolerances, editors, and the rendering-kernel evidence bar.
8. The baseline census and parity ledgers make every KovaForge desktop capability, setting, workflow, command, integration, and compatibility format accountable.
9. Agents have standing authority to improve internal structure and remove material development pain points as they encounter them, within contract-preserving and reviewable boundaries.
10. Agents preserve recoverable, reviewable progress through coherent local commits and perform a verified final push without requiring a second authorization.
11. Agents continue on the branch supplied by the user or environment and never create or switch branches unless the user explicitly requests it.
9. The qualification tranche flows directly into the [full-parity delivery program](12-full-parity-delivery.md); it is not a four-feature stopping point.

The proposal is therefore not "rewrite XerahS three times and trust AI." It is "inventory the complete reference product, specify XerahS once, implement it natively three times, and prove that no functionality was silently lost."

# 2. Motivation

## 2.1 Agentic development changes the reuse calculation

Shared UI code is valuable because it avoids repeating implementation work. AI agents substantially reduce the marginal cost of that repetition. A well-governed agent can read a feature contract, inspect a platform implementation, implement the feature using native APIs and conventions, add tests, and report deviations.

The potential benefits for XerahS are significant:

- Native permission, capture, recording, windowing, shortcut, notification, menu, and accessibility behavior.
- Native user experience instead of a lowest-common-denominator interaction model.
- Direct use of new operating-system capabilities without waiting for cross-platform framework support.
- Smaller platform-specific context for agents and maintainers.
- Independent platform evolution when an operating system requires a different design.

## 2.2 XerahS is unusually platform-sensitive

Many of XerahS's defining capabilities are already operating-system integrations rather than portable UI concerns:

- Screen, display, window, and region capture
- Screen recording and audio capture
- Global hotkeys and input hooks
- Clipboard and drag-and-drop behavior
- Tray, menu, notification, and startup integration
- Permissions and security prompts
- GPU, HDR, DPI, and color handling
- Wayland, X11, desktop portal, AppKit, and Win32 behavior

Avalonia can provide a consistent presentation layer, but it cannot make these capabilities identical. Existing XIPs for macOS, Linux, platform abstraction, and Windows capture parity demonstrate that the hardest work already lives at platform boundaries.

## 2.3 The current code cannot be the only specification

When one platform's implementation is treated as the product definition, other platforms become permanent ports of accidental behavior. This creates several problems:

- Windows behavior can become authoritative merely because it existed first.
- Bugs can be copied as if they were requirements.
- Agents must reverse-engineer intent from implementation details.
- Platform ports can look complete while differing in edge cases, error handling, persistence, or ordering.
- Refactoring one implementation can silently redefine the product.

The source of product truth should be independent of any UI framework, programming language, or operating system.
