# Linux implementation plan

Implement the approved English Product Contract in the `linux-qt` reference
edition. Keep shared expected results independent of Linux, and record partial
requirements honestly until native behavior and evidence cover their full scope.

The next editor work follows these dependencies:

1. Complete arbitrary-angle rectangle export under ES-025 using exact polygon
   coverage. Expose native selection rotation, preserve atomic session history,
   and make selection and hit testing follow the rotated object. Verify known
   analytical coverage and real PNG/sidecar persistence.
2. Implement viewport zoom, fit, reset, gestures, and pan under EC-005/EC-006.
   Keep viewport state outside the document and history; convert all pointer
   input into document pixels and keep hit targets usable at every zoom. Verify
   pointer anchoring, finite scale limits, history isolation, and native input.
3. Add atomic crop, pixel resize, canvas resize, rotations and flips with shared
   production geometry and conformance operations, then extend the image and
   annotation model for insert, background composition, flatten and comparison.
4. Expand annotation tools, effects, output actions, settings and utilities,
   alongside the remaining capture and post-capture integrations.

Each checkpoint updates requirement traceability, runs native tests and the
implemented shared vectors, and lands on `main`. Passing the small pilot vector
set does not establish full capability completion. Linux edition qualification,
shared goldens, accessibility and release reviews remain required as described
in `product-contract/READINESS.md`.
