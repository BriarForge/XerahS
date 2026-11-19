# EA-008: regular Text border geometry

Decision: EA-008-TEXT-BORDER-2026-11-20, resolved by the loop under the
maintainer's delegated owner-block authority on 2026-11-20 (agent opencode,
GLM 5.3).
Contract: 0.4.4; EDITOR-ANNOTATIONS-001: 0.1.2. Requirements: EA-008.

## Problem

EA-008 requires Text to support "text/border/fill colors", but the shared
loop state held an open geometry question: does a regular Text border apply
to a glyph outline, a text-box outline, or another defined geometry? The
native candidate code stroked glyph outlines (paintOutlinedText) with a
comment claiming the reference draws outlines behind the letters. Reading
the reference showed that claim was wrong.

## Choice

A regular Text border strokes the text box: a rounded rectangle around the
text area with corner radius 4, drawn with the border colour at
max(1, stroke width), centred on the box edge, after the background fill and
behind the text. It never outlines glyphs.

This is the reference's own geometry. ShareX's DrawText
(ShareX.ImageEffectsLib/Drawings/DrawText.cs, read 2026-11-20) draws in
order: the optional background (a rounded rectangle filled with
BackgroundColor), the optional border (the same rounded rectangle path
stroked with BorderColor at max(1, BorderSize)), and the text on top; its
CornerRadius defaults to 4 (clamped non-negative) and its only glyph-level
extra is the optional offset DrawTextShadow duplicate, which is a separate
feature the SPEC does not require for Text and which this decision does not
add. The earlier native glyph-outline candidate matched neither the
reference's DrawText nor any Text tool in the reference tree; it is replaced,
not approved.

The SpeechBalloon's separate balloonBorderColor body/tail outline is
untouched and proceeds independently, exactly as the recorded question
allowed.

Sources (read 2026-11-20): the ShareX behavioural reference
/home/mike/Work/ShareX/ShareX/ShareX.ImageEffectsLib/Drawings/DrawText.cs
(lines 84-109: CornerRadius default 4, BorderColor/BorderSize, DrawBackground;
lines 142-170: rounded-rectangle path, background fill, border stroke, then
text) and ShareX.ImageEditor/Core/Annotations/Text/TextAnnotation.cs, whose
regular Text annotation carries no outline at all - confirming that no
reference surface ever outlines glyphs.

## Impact

The renderer changes for regular Text: the border field (textBorderColor)
keeps its name, parsing, persistence, alpha semantics (half-alpha draws
no opaque pixel, zero alpha draws nothing) and edit/history behavior
unchanged, but the pixels it drives are now the decided box outline.
EditorCanvasTest::textBorderStrokesTheTextBoxPerTheDecidedGeometry pins the
decided geometry with region discriminators that fail against the
glyph-outline candidate (border pixels on the top and side edges where no
glyph reaches; the text pixels exactly unchanged) and pass after - proven
red against the pre-change code via a saved patch. The whole existing
suite passes unchanged otherwise (183/183). EA-008 leaves partial as
implemented candidate; physical/editor target qualification and release
acceptance remain separate gates, and EA-009 was decided separately
(b34fad2).
