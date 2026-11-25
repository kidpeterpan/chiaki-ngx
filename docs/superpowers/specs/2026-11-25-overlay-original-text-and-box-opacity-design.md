# Overlay: optional original text and box opacity

Date: 2026-11-25. Size: a small change.

## Intent

On the Steam Deck's 7" screen the translation overlay is hard to read. The user wants:

1. A setting to show or hide the original Japanese line in the overlay. Shown: behaves exactly
   as today. Hidden: the translation gets the space.
2. A setting for the opacity of the overlay's background box.

## Goals

- Both settings live in the translator settings panel, section **Appearance**, usable with
  the controller (D-pad left/right changes the value), saved in QSettings, and applied to the
  overlay at once (purely visual: no pipeline restart).
- Defaults keep today's look: original text **shown**, box opacity **80%** (today's `#cc`).

## Non-goals

- No R4 cycling, no separate size for the original text, no early display of the original
  before the translation arrives (offered, deferred by the user).

## Behaviour

**Original text: Show / Hide**

- Show: unchanged.
- Hide, in-place mode ("Over the original text"): the source line is not drawn and the
  translation's text area takes the whole box height. With Font size 0 (auto-fit) the
  translation grows to fill it; with a fixed Font size it keeps its size and gains lines.
  Tapping the box still opens JMdict for the block (it uses the block's source text).
- Hide, bottom mode ("Bottom center"): the source line is not drawn, the box shrinks to the
  translation, and tap-to-define is off (it needs the visible source line).

**Box opacity (%)**

- 20 to 100, step 10, default 80. Applies to the background of both overlay boxes (in-place
  blocks and the bottom subtitle). Text and border stay fully opaque.

## Approaches

- Chosen: one setting each in Appearance, applied live.
- Considered: R4 cycling through original+translation / translation only / hidden. Deferred
  by the user.
- Opacity of the whole box (QML `opacity`) was rejected: it would fade the text too.

## How (builder's calls)

- `TranslatorManager`: `overlayShowSource` (bool, `translator/overlay_show_source`) and
  `overlayBoxOpacity` (int percent, `translator/overlay_box_opacity`), one change signal.
  The opacity clamp lives in `translator/preferences` with the other clamps and is unit
  tested there.
- `TranslatorOverlay.qml`: the two overlay boxes read the settings; Appearance gets an
  "Original text" combo (Show, Hide) and a "Box opacity (%)" spin box.
- QML tests drive both settings with controller keys against the stub Translator.
