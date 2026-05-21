# UI Controls Guide (U++ Ui)

This is the living guide for building, reviewing, and using the repo's `Ui*` control set. It keeps the old checklist role, but also records the practical control-layer and demo-layer rules that new AI agents need before they touch the codebase.

Repo rules:
## What This Guide Covers

Use this file for three things:

- understanding what the current Ui* controls are
- reviewing new/changed controls before merge
- learning the repo-specific gotchas around theme/style/layout work

## Current Ui Controls

Core styled surfaces and infrastructure:

- UiStyle, CtrlStyled, UiDraw, UiTheme, UiIcons
- UiLayoutCursor for lightweight shell/demo placement

Primary controls:

- UiLabel, UiButton, UiCheckBox, UiToggle, UiRadioButton
- UiPanel, UiAccordion, UiScrollPanel, UiTitleCard
- UiBaseEdit, UiLineEdit, UiPasswordEdit, UiMaskEdit, UiMultiEdit
- UiIntEdit, UiFloatEdit, UiSlider, UiSliderEdit, UiScrollBar
- UiColorPicker
- UiGridLayout, UiBoxLayout, UiDoc
- UiBezierCurveEditor, UiBezierCurveField

## Repo-Specific Control Rules

- Demos are tests: if a control API or baseline style changes, update the affected demos and copied usage output immediately.
- Prefer the control/theme default first. Do not add demo-local style overrides unless the demo is intentionally showing a variation.
- Do not restate defaults just because a setter exists. If a call does not materially change behavior, remove it.
- For UiLabel, remember that metrics.text_font is the actual paint path when metrics.use_text_font is enabled. Setting only style.font can be a no-op.
- For accordions inside scroll panels, section body heights must be explicit and the owner layout must notify the scroll container when sections open or close.
- Use UiLayoutCursor for lightweight shell/manual placement, but keep box/grid layouts for repeated stacked content.
- Add comments around meaningful constructor groups, layout groups, and theme groups in demos and new controls. New code should be readable without tracing every line.
- Keep style helpers compact. If a style helper mostly repeats defaults, trim it.
- For custom-painted primitives, use the dedicated part hooks (`WhenPaintTrack`, `WhenPaintActiveTrack`, `WhenPaintThumb`, `WhenPaintArrow`) instead of demo-local overpaint or geometry guessing.

- No backward-compat naming shims.
- Demos are tests: any API change requires updating `examples/**` and `README.md`.
- Prefer no allocations in `Paint()`; cache where possible.

## Shared spacing vocabulary

Use one naming system across item controls:

- `item_spacing`: spacing between repeated owned items such as accordion sections, list rows, menu rows, tree rows, dropdown popup rows, and tabs
- `content_gap`: primary spacing inside a single item/control surface, for example icon-to-text or check/icon/text lane spacing
- semantic secondary gaps stay explicit where they represent a separate lane, for example `right_gap`, `metadata_gap`, `drag_gap`, `chevron_gap`, or `accessory_gap`
- `content_margin`: outer inset around a control's painted content

Do not add alias setters or compatibility fields when renaming to this vocabulary.
## Gold delta checklist (copy 1:1)

Use this gate before merge for any new/changed `Ui*` control. `UiLabel` + `UiButton` are the gold references.

### Cache lifecycle

- [ ] Keep explicit dirty flags for derived caches (`layout_dirty_`, `metrics_dirty_`, `minsize_dirty_`).
- [ ] Recompute caches only in invalidation paths (`SetText`, `SetStyle`, `SetFont`, `Layout`, size/state changes), not in `Paint()`.
- [ ] Setters that affect geometry/text/style set the correct dirty flags and call `RefreshLayout()` (and `Refresh()` when visuals change).
- [ ] `Layout()` early-outs when caches are valid; otherwise rebuild once and clear dirty flags.
- [ ] `GetMinSize()` uses cached natural size and invalidates only when inputs change.

### Paint contract

- [ ] Paint phase order stays `background -> content -> foreground`.
- [ ] `Paint()` remains render-only: no hidden state mutation, no layout/text measurement recompute, no expensive allocation.
- [ ] If a cache is dirty inside `Paint()`, call `RefreshLayout()` and return (gold pattern).
- [ ] Content draw uses precomputed rects/splits (`layout_`, line sizes, icon rects), not ad-hoc recomputation.
- [ ] State mapping (`normal/hot/pressed/disabled/focus`) is explicit and consistent before drawing.

### Style contract

- [ ] Style structs are data-only (`StyledPalette`, `StyledMetrics`, `StyledSkin` first); behavior stays in control code.
- [ ] Reuse shared primitives/helpers; do not create parallel style/alignment/padding systems.
- [ ] Preserve `frame -> inset -> padding` semantics and shared `UiAlign` conventions.
- [ ] New public style fields satisfy the two-consumer rule (2 controls or 2 real use-cases) or include explicit rationale.
- [ ] Prefer variants/presets/theme tokens over one-off public setters.

### Event/data contract

- [ ] Interactive controls expose `SetData(const Value&)` and `GetData() const`.
- [ ] Action semantics are explicit (`WhenAction`, `WhenPush`, `WhenChange`, etc.) and fire exactly once per user action.
- [ ] Naming symmetry is kept (`SetX`/`GetX`).
- [ ] Async/timer callbacks are lifetime-safe (`Ptr` guard or equivalent), with teardown cancel where needed.
- [ ] GUI work stays on GUI thread; callback paths avoid long/blocking work.

## Build smoke tests

Recommended minimal set (CLI):

```bat
"E:\upp-18468\umk.exe" "E:\apps\github\upp_Ui,E:\upp-18468\uppsrc" examples/UiLabelDemo  CLANGx64 -br +GUI "E:\apps\github\upp_Ui\build\UiLabelDemo"
"E:\upp-18468\umk.exe" "E:\apps\github\upp_Ui,E:\upp-18468\uppsrc" examples/UiButtonDemo CLANGx64 -br +GUI "E:\apps\github\upp_Ui\build\UiButtonDemo"
"E:\upp-18468\umk.exe" "E:\apps\github\upp_Ui,E:\upp-18468\uppsrc" examples/UiCheckBoxDemo CLANGx64 -br +GUI "E:\apps\github\upp_Ui\build\UiCheckBoxDemo"
"E:\upp-18468\umk.exe" "E:\apps\github\upp_Ui,E:\upp-18468\uppsrc" examples/UiToggleDemo CLANGx64 -br +GUI "E:\apps\github\upp_Ui\build\UiToggleDemo"
"E:\upp-18468\umk.exe" "E:\apps\github\upp_Ui,E:\upp-18468\uppsrc" examples/UiRadioButtonDemo CLANGx64 -br +GUI "E:\apps\github\upp_Ui\build\UiRadioButtonDemo"

"E:\upp-18468\umk.exe" "E:\apps\github\upp_Ui,E:\upp-18468\uppsrc" examples/UiPanelDemo CLANGx64 -br +GUI "E:\apps\github\upp_Ui\build\UiPanelDemo"
"E:\upp-18468\umk.exe" "E:\apps\github\upp_Ui,E:\upp-18468\uppsrc" examples/UiAccordionDemo CLANGx64 -br +GUI "E:\apps\github\upp_Ui\build\UiAccordionDemo"
"E:\upp-18468\umk.exe" "E:\apps\github\upp_Ui,E:\upp-18468\uppsrc" examples/UiScrollPanelDemo CLANGx64 -br +GUI "E:\apps\github\upp_Ui\build\UiScrollPanelDemo"
"E:\upp-18468\umk.exe" "E:\apps\github\upp_Ui,E:\upp-18468\uppsrc" examples/UiTitleCardDemo CLANGx64 -br +GUI "E:\apps\github\upp_Ui\build\UiTitleCardDemo"
"E:\upp-18468\umk.exe" "E:\apps\github\upp_Ui,E:\upp-18468\uppsrc" examples/UiGridLayoutDemo CLANGx64 -br +GUI "E:\apps\github\upp_Ui\build\UiGridLayoutDemo"
"E:\upp-18468\umk.exe" "E:\apps\github\upp_Ui,E:\upp-18468\uppsrc" examples/UiLineEditDemo CLANGx64 -br +GUI "E:\apps\github\upp_Ui\build\UiLineEditDemo"
"E:\upp-18468\umk.exe" "E:\apps\github\upp_Ui,E:\upp-18468\uppsrc" examples/UiIntFloatDemo CLANGx64 -br +GUI "E:\apps\github\upp_Ui\build\UiIntFloatDemo"
"E:\upp-18468\umk.exe" "E:\apps\github\upp_Ui,E:\upp-18468\uppsrc" examples/UiBoxLayoutDemo CLANGx64 -br +GUI "E:\apps\github\upp_Ui\build\UiBoxLayoutDemo"
"E:\upp-18468\umk.exe" "E:\apps\github\upp_Ui,E:\upp-18468\uppsrc" examples/UiSliderDemo CLANGx64 -br +GUI "E:\apps\github\upp_Ui\build\UiSliderDemo"
"E:\upp-18468\umk.exe" "E:\apps\github\upp_Ui,E:\upp-18468\uppsrc" examples/UiAllControlsDemo CLANGx64 -br +GUI "E:\apps\github\upp_Ui\build\UiAllControlsDemo"
"E:\upp-18468\umk.exe" "E:\apps\github\upp_Ui,E:\upp-18468\uppsrc" examples/UiScrollBarDemo CLANGx64 -br +GUI "E:\apps\github\upp_Ui\build\UiScrollBarDemo"
"E:\upp-18468\umk.exe" "E:\apps\github\upp_Ui,E:\upp-18468\uppsrc" examples/UiDocDemo CLANGx64 -br +GUI "E:\apps\github\upp_Ui\build\UiDocDemo"

```

## Icon conversion utility (SVG/PNG)

- Tool: `Utilities/MakeIconFromSVG` (build target: `build/MakeIconFromSVG.exe`).
- Default output mode: `iml`.
- Input: vector (`.svg`) and raster (`.png`, other `StreamRaster` formats).
- Shared icon workflow output:
  - `<base>.iml.append` for [UiIcons.iml](E:\apps\github\upp_Ui\Ui\UiIcons.iml)
  - `<base>.icons_h.append` for [UiIcons.h](E:\apps\github\upp_Ui\Ui\UiIcons.h)
- Legacy/local output mode: `uimakeicon`.
- Typical usage:

```bat
build\MakeIconFromSVG.exe designs\search.svg
build\MakeIconFromSVG.exe designs\check.svg designs\radio.svg --size 48x48 --output-base Ui\icon_batch
build\MakeIconFromSVG.exe designs\search.svg --format uimakeicon --output-base Ui\newicons\search_icon
```

- Workflow note: the shared icon pack is IML-backed now. Append the generated `.iml.append` content into `Ui/UiIcons.iml` and the generated `.icons_h.append` content into `Ui/UiIcons.h`.

## Control status

Legend:

- In `Ui/Ui.upp`: included in the Ui package build
- Demo: `examples/<Control>Demo`

| Control | In `Ui/Ui.upp` | Demo | Status | Notes / blockers | Next |
|---|---:|---:|---|---|---|
| UiStyle / CtrlStyled | yes | n/a | baseline | core styling surface | keep stable; add perf notes as needed |
| UiDraw | yes | n/a | baseline | 9-slice + blur helpers used across controls | ensure `Ui/Ui.upp` dependencies stay aligned |
| UiIcons | yes | n/a | baseline | icon sources for demos | keep stable |
| UiLabel | yes | UiLabelDemo | builds (umk) | baseline for 2-block layout + margins | run visual checks + refine edge cases |
| UiButton | yes | UiButtonDemo | builds (umk) | baseline for states + animation hooks | run visual checks + refine edge cases |
| UiCheckBox | yes | UiCheckBoxDemo | builds (umk) | classic/tri-state/switch/chip/list styles | add more grouping + keyboard behavior checks |
| UiToggle | yes | UiToggleDemo | builds (umk) | boolean switch wrapper over UiCheckBox switch style | add optional labels/icons preset variants |
| UiRadioButton | yes | UiRadioButtonDemo | builds (umk) | builder-shell demo now exposes grouping, indicator/body geometry, and full color lanes | add arrow-key group navigation |
| UiPanel | yes | UiPanelDemo | builds (umk) | background-only styled container | add a couple of interaction/focus checks |
| UiAccordion | yes | UiAccordionDemo | builds (umk) | styled collapsible sections (header card + body panel) with single-open option | add animated open/close + drag reorder if needed |
| UiScrollPanel | yes | UiScrollPanelDemo | builds (umk) | styled scroll container with viewport/content separation | add keyboard scroll navigation checks |
| UiTitleCard | yes | UiTitleCardDemo | builds (umk) | header-focused card (title/subtitle/copy + decorative rule + media side) | add image-top and image-bottom demo variants |
| UiGridLayout | yes | UiGridLayoutDemo | builds (umk) | large surface; selection is currently stub-like | add behavioural checks + scroll coverage |
| UiBaseEdit | yes | UiBaseEditDemo | builds (umk) | foundation for edit family; flank/side API stable | add side overlay/stack tests |
| UiLineEdit | yes | UiLineEditDemo | builds (umk) | single-line behaviour; scrollbar hidden | add behavioural checks (enter/tab) |
| UiPasswordEdit | yes | UiPasswordEditDemo | builds (umk) | flank API migrated to `AddToSide` | visual pass on eye/submit compositions |
| UiMaskEdit | yes | UiMaskEditDemo | builds (umk) | animation dependency integrated | add more semantic validator scenarios |
| UiMultiEdit | yes | UiMultiEditDemo | builds (umk) | builder-shell demo now exposes edit behavior, side items, geometry, and full edit color surfaces | add long-text perf checks |
| UiIntEdit / UiFloatEdit | yes | UiIntFloatDemo | builds (umk) | spin arrows + wheel + key support | align style API with other controls |
| UiSlider | yes | UiSliderDemo | builds (umk) | ticks + keyboard/wheel + drag | add label/tick text variants |
| UiSliderEdit | yes | UiSliderDemo | builds (umk) | field alignment LEFT/RIGHT/TOP/BOTTOM | add integer mode and format presets |
| UiScrollBar | yes | UiScrollBarDemo | builds (umk) | styled track/thumb/arrows + hover-expand animation | add behavioural checks (drag, wheel, arrows) |
| UiColorPicker | yes | UiColorPickerDemo | builds (umk) | technical picker now exposes editable value fields, current/previous slot previews, swatch-library transfer flow, and slider-hook based hue/gain/alpha tracks | run a visual polish pass against the mockup and tighten spacing/layout details |
| UiDoc | yes | UiDocDemo | builds (umk) | rich doc base with selection, marks, search, annotations, undo/redo | grow block schema + serializers + screenplay mode policies |
| UiBoxLayout | yes | UiBoxLayoutDemo | builds (umk) | implementation stabilized and integrated | add wrap_rows_expand visual tuning |
| UiMenu | yes | UiMenuDemo | builds (umk) | builder-shell demo now exposes datasets, row/bar geometry, spacing, toggles, and menu color lanes | add keyboard/popup interaction checks |
| UiTab | yes | UiTabDemo | builds (umk) | builder-shell demo now exposes placement, indicator, spacing, affordance, drag/close, and tab/body palettes | add keyboard navigation and overflow checks |
| UiAllControlsDemo | n/a | UiAllControlsDemo | builds (umk) | smoke gallery containing all current controls | keep in sync as controls grow |

## Release milestone: "v1 green baseline"

Criteria:

- `Ui/Ui.upp` builds cleanly.
- `UiLabelDemo` and `UiButtonDemo` build + run cleanly.
- README + docs match the actual API.
- Checklist shows clear blockers for all not-yet-added controls.

## Recent hardening log

- `UiBaseEdit` side visibility/spin fix:
  - `LayoutSides()` no longer force-shows side controls.
  - `UiIntEdit` / `UiFloatEdit` spin controls now use side visibility APIs so hide/show state and side-space reservation stay in sync.
- `UiTitleCard` paint pipeline + text metric cache:
  - Consolidated to one explicit paint pipeline (background -> content -> foreground/focus).
  - Added cached title/subtitle/copy text metrics with dirty invalidation on text/style changes.
- `UiGridLayout` header paint metric cleanup:
  - Replaced paint-time `GetTextSize(...)` for cluster headers with font metric usage.
- `UiAccordion` destructor/timer teardown + `SetData/GetData`:
  - Added explicit destructor teardown to stop active animation callbacks and drag state.
  - Added explicit `SetData/GetData` open-state contract (`ValueArray` mapping, lock-policy normalization).
- Demo non-paint text-metric cleanup:
  - `UiGridLayoutDemo`, `UiButtonDemo`, `UiLabelDemo`, `UiPanelDemo`, and `UiDemoBase` updated to avoid paint-time `GetTextSize(...)` where practical.

