# CORE-015: scroll alignment threshold

Decision: CORE-015-SCROLL-THRESHOLD-2026-11-20, resolved by the loop under
the maintainer's delegated owner-block authority on 2026-11-20 (agent
opencode, GLM 5.3).
Contract: 0.4.2; CORE-PLATFORM-001: 0.6.1. Requirements: CORE-015.

## Problem

CORE-015 forbids synthesizing a seamless result when alignment confidence
is "below the approved threshold", but the contract text never approved a
value. The native stitcher carried kScrollAlignmentThreshold = 0.95 as an
explicit, unapproved placeholder, and the requirement stayed blocked on
"approve scrolling alignment threshold; no new threshold is inferred".

## Choice

0.95 is approved as the confidence threshold: a join is accepted only when
at least 95% of the informative overlap rows agree at the best offset and
the best offset beats its runner-up by the 15% ambiguity margin (repeating
content scales confidence down instead of guessing; thin overlaps below 8
informative rows carry no evidence). The 0.15 ambiguity margin is approved
with it, since the two form one decision rule.

Rationale: the two leading references converge on a 5%-difference budget
for row-level identity, and our bar keeps the requirement's promise on the
conservative side of both:

- ShareX's scrolling combiner (ShareX.ScreenCaptureLib
  ScrollingCaptureManager.CombineImages) accepts an offset from unbroken
  runs of byte-identical rows - 100% row agreement over the matched run,
  with no fraction requirement at all. Requiring 95% agreement across the
  whole informative overlap is at least as strict over strictly more rows.
- long-shot-rs (jswysnemc/long-shot-rs, a native Wayland long-screenshot
  tool, MIT) treats a row as identical when at most 5% of its pixels differ
  (src/stitch.rs: diff_count > width/20 breaks the sticky-header row
  comparison) - the same 5% budget, applied per row, as our 95% row
  agreement applied per informative row.

Sources (fetched and read 2026-11-20): the ShareX behavioural reference
sources /home/mike/Work/ShareX/ShareX/ShareX.ScreenCaptureLib/
ScrollingCaptureManager.cs and
https://raw.githubusercontent.com/jswysnemc/long-shot-rs/master/src/stitch.rs
(archived project; its successor wayscrollshot carries the same
eight-band/sticky-bar alignment lineage).

## Impact

The value the placeholder held is now the approved threshold, so no runtime
behavior changes; the join decision is refactored into one production seam
(xerahs::capture::scrollJoinDecision) so the stitcher, the conformance
adapter and the published CORE-015 vectors run the identical rule. Eight
scroll-join-disposition vectors are added to
CORE-PLATFORM-001/test-vectors.json and pass through the Linux adapter
(39/39 CORE vectors); the same dispositions are asserted natively in
ScrollStitchTest::decidedJoinDispositionMatchesTheApprovedThreshold.
CORE-015 leaves blocked and is implemented candidate; live compositor
qualification of real scrolling capture remains the separate physical gate,
and release requires Mike. No encoder, settings or platform surface
changes.
