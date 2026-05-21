# UiTheme Blueprint

## Purpose

Define a global theme system for the U++ UI layer that sits **above** `UiStyle.h` primitives and **below** per-control custom overrides.

This document is the authoritative design intent for `UiTheme.h` and related resolver code.

It is written so a developer or coding agent can understand not only the API shape, but also the architectural rules, responsibilities, precedence, and implementation goals.

---

## Core Intent

The existing styling system already provides a strong low-level foundation:

* `StyledPalette` for face/frame/ink/icon colors by control state
* `StyledMetrics` for geometry, padding, radius, frame width, shadow, highlight
* `StyledSkin` for skin images, 9-slice data, and geometry insets
* `UiDraw.h` helpers that already paint using those common styling primitives

The problem to solve is not lack of styling power.

The problem is **preset-level usability**.

Most developers do not want to tune many separate settings just to get a decent result. They want to say:

* Minimal
* Rounded
* Linear
* Solid
* Outline
* Compact
* Layered

and have the entire UI adopt that visual family by default.

So the theme system must provide:

1. **One global theme preset source**
2. **Per-control role-based resolution**
3. **Optional per-instance local override**
4. **One central place where theme looks are authored**
5. **No duplication of visual policy across each control implementation**

---

## Styling Axes

This system must keep the major styling axes separate.

If these axes are blurred together, the API becomes confusing, controls drift apart,
and the theme system slowly turns into a pile of overlapping exceptions.

### 1. Primitive tokens

Defined by shared style primitives such as:

* palette values
* fills
* frame and face enable flags
* radius
* frame width
* spacing and padding
* shadow and highlight
* 9-slice skin data
* content inset

These are raw building blocks.

They are not a theme by themselves.

### 2. Preset

A preset is the global visual family.

Examples:

* `Minimal`
* `Rounded`
* `Linear`
* `Solid`
* `Outline`
* `Compact`
* `Layered`

A preset defines broad visual language:

* surface treatment
* density tendency
* corner philosophy
* border emphasis
* elevation behavior
* panel treatment

### 3. Mode

Mode is the environment or palette mapping variant.

Examples:

* `Light`
* `Dark`
* `System`

Mode should mostly influence:

* color mapping
* contrast
* emphasis strength
* shadow/highlight interpretation

Mode should **not** be the primary mechanism for changing geometry language.

### 4. Role

Role is the semantic intent of a control inside the active preset.

Examples:

* button: `Standard`, `Accent`, `Subtle`, `Danger`
* label: `Body`, `Title`, `Caption`, `Badge`

Role answers:

* what this control means
* how strongly it should stand out
* how it should behave visually relative to sibling controls

Role is not a replacement for preset.

### 5. Visual mode

Some controls legitimately need control-specific structural variants.

Examples:

* checkbox: classic, chip, list-check
* radio: classic, pills, list

These are not theme presets.

They are structural rendering modes specific to that control family.

They may affect:

* indicator placement
* structural composition
* selection mark style
* layout model

Visual mode should only exist where the control truly has multiple structural forms.

### 6. Local override

This is the explicit per-instance escape hatch.

It allows a developer to take the fully resolved control style and modify it locally.

This must remain possible, but it must remain the highest-precedence exception layer,
not the primary authoring model.

---

## Architectural Rule

### Styling has three layers

#### 1. Primitive styling vocabulary

Lives in `UiStyle.h`

This layer defines the atoms:

* fills
* palette entries
* frame/face enable flags
* radius
* frame width
* content padding
* shadows
* highlights
* 9-slice skins
* content inset

This layer must remain generic and reusable.

It should **not** become a bag of hardcoded widget theme recipes.

#### 2. Theme + role resolution

Lives in `UiTheme.h` / `UiTheme.cpp`

This layer defines named looks and maps them to concrete `Style` objects for each control type.

Examples:

* `ResolveButtonStyle(UiThemePreset::Rounded, UiThemeMode::Light, UiButtonRole::Accent)`
* `ResolveLabelStyle(UiThemePreset::Rounded, UiThemeMode::Light, UiLabelRole::Caption)`

This is where the actual visual family is defined.

#### 3. Per-control resolved style and custom override

Lives in each control

Each control still keeps its own `Style` struct because each control has control-specific fields.

Examples:

* button press offset
* focus margin
* icon layout
* icon margin
* text margin
* label nowrap
* label transparency
* rich text span support

The control-specific `Style` remains necessary.

But it should become the **resolved result**, not the primary location where the main global theme family is authored.

---

## High-Level Goal

A programmer should be able to do this:

```cpp
UiTheme::SetPreset(UiThemePreset::Rounded);
UiTheme::SetMode(UiThemeMode::Light);
```

Then instantiate controls normally and get a coherent UI.

They may optionally refine a control semantically:

```cpp
ok.SetRole(UiButtonRole::Accent);
cancel.SetRole(UiButtonRole::Subtle);
title.SetRole(UiLabelRole::Title);
note.SetRole(UiLabelRole::Caption);
```

And only if needed should they locally override the fully resolved style:

```cpp
UiButton::Style s = ok.GetResolvedStyle();
s.metrics.radius = DPI(12);
ok.SetStyle(s);
```

That is the intended precedence and usage model.

---

## Precedence Rules

These rules must be deterministic.

### Style source precedence

1. **Explicit local style override**
2. **Explicit local role + current global preset**
3. **Default role + current global preset**
4. **Hardcoded safe fallback**

This means:

* a programmer can still fully override any control
* most programmers do not need to override anything
* controls remain coherent when nothing special is set

### Legacy preset compatibility

During migration, existing per-control preset APIs such as:

* `StyleMinimal()`
* `StyleSoft()`
* `StyleStrong()`
* control-local named convenience styles

may remain temporarily for backward compatibility.

However, they must no longer be treated as independent theme-authoring surfaces.

They should become one of the following:

* wrappers over `UiTheme` resolver output, or
* deprecated compatibility conveniences

They must not become a second competing policy layer beside `UiTheme`.

No new control should introduce fresh family-level preset authorship once the
theme resolver path exists.

---

## What a Theme Preset Means

A theme preset is a **visual language preset**, not just a color palette.

A preset defines things such as:

* corner radius
* border strength
* fill treatment
* control density
* shadow/elevation behavior
* panel treatment
* selection emphasis
* light/dark palette mapping

A preset does **not** define a specific widget by itself.

Instead, the preset is combined with a **control role**.

For example:

* Rounded + Button(Standard)
* Rounded + Button(Accent)
* Rounded + Label(Body)
* Rounded + Label(Caption)

This avoids exploding the API into dozens of unrelated style names.

---

## Default Theme Preset Family

Recommended initial preset family:

* `Minimal`
* `Rounded`
* `Linear`
* `Solid`
* `Outline`
* `Compact`
* `Layered`

### Intent of each preset

#### Minimal

Very low-noise modern UI.
Thin borders, restrained fills, light separators, flat presentation.

#### Rounded

Soft modern UI with larger radii and friendlier control shapes.

#### Linear

Sharp rectangular geometry with crisp boundaries and strong structure.

#### Solid

Stronger filled surfaces and heavier visual weight.

#### Outline

Line-driven controls with restrained fills.

#### Compact

Dense professional layout with tighter spacing and smaller controls.

#### Layered

Modern surface hierarchy using cards, subtle elevation, and panel depth.
Not retro bevel. Not pure neumorphism.

---

## Theme Modes

At minimum:

* `Light`
* `Dark`

Potential future additions:

* `System`
* `HighContrast`

Theme mode must affect palette resolution, and may also affect certain emphasis values such as border visibility or shadow strength.

But geometry semantics should remain preset-driven rather than mode-driven.

---

## Control Roles

Roles give semantic meaning to a control inside the current preset.

### Button roles

Suggested initial button roles:

* `Standard`
* `Accent`
* `Subtle`
* `Icon`
* `Danger` (optional, but useful)

### Label roles

Suggested initial label roles:

* `Body`
* `Headline`
* `Subheadline`
* `Title`
* `Caption`
* `Badge`
* `Footnote`

These roles are not separate themes.
They are semantic variants within the active preset.

Roles should remain compact and semantically meaningful.

They should not turn into large visual catalogs such as:

* `RoundedPrimaryLargeGhostButton`
* `SoftSecondaryPanelLabel`

That kind of naming is a sign that preset, role, and local override concerns are
being mixed together.

---

## Global Theme Context

The framework needs one current theme context.

This context must be cheap to query and safe to use across controls.

Minimum contents:

* current preset
* current mode
* optional scale/density override later

Controls that do not have explicit local style overrides should resolve their appearance from this global context.

---

## Local Control Model

Each control should conceptually support:

* optional explicit local style override
* optional semantic role
* optional control-specific visual mode where structurally required
* ability to resolve its effective style from global theme if no explicit override exists

That means controls should not have to store every global decision themselves.

Instead they should expose something like:

* `SetStyle(const Style&)`
* `ClearStyleOverride()`
* `SetRole(...)`
* `GetResolvedStyle() const`
* `GetEffectiveStyle() const`

`GetResolvedStyle()` may mean the style built from global preset + role.
`GetEffectiveStyle()` may mean resolved style unless there is a local explicit override.

Either naming is fine as long as the semantics are consistent.

Controls should not need to care in `Paint()` whether the effective style came from:

* preset + mode + role
* preset + mode + role + visual mode
* explicit local override
* fallback default

They should paint from one final style object.

---

## Why Per-Control Style Still Exists

Even with a global theme system, `UiButton::Style` and `UiLabel::Style` still need to exist.

Reason:

Each control has fields that are specific to its behavior and layout.

A button needs things like:

* press offset
* focus margin
* icon images
* icon render mode (`UiIconRenderMode`)
* content alignment

A label needs things like:

* transparency
* nowrap
* span/rich text support assumptions
* icon/text spacing behavior

So per-control `Style` remains the correct runtime payload.

The change is this:

### Old mental model

Control style object is where the preset itself is authored.

### New mental model

Control style object is the final resolved payload built by the theme resolver.

That is the key architectural shift.

---

## Resolution Responsibility

`UiTheme` owns the recipes.

Each resolver function should create a fully usable control style for the requested:

* preset
* mode
* role

Example:

```cpp
UiButton::Style ResolveButtonStyle(UiThemePreset preset,
                                   UiThemeMode mode,
                                   UiButtonRole role);

UiLabel::Style ResolveLabelStyle(UiThemePreset preset,
                                 UiThemeMode mode,
                                 UiLabelRole role);
```

These functions should:

1. start from a sensible default style object
2. apply preset-level geometry/surface rules
3. apply light/dark palette mapping
4. apply role-specific semantic adjustments
5. return the final style

This is the single authoritative place where the look of each theme is defined.

Resolver code should be the only place where family-level visual policy is authored.

If multiple controls need the same family-level visual behavior, that should be
expressed through shared recipe helpers inside the theme layer, not by repeating
similar `StyleMinimal/Soft/Strong` logic in every control.

---

## What Must Not Happen

The following are anti-patterns and should be avoided:

### 1. Every control manually hardcodes theme policy

Bad because behavior drifts.

### 2. `UiStyle.h` becomes filled with per-widget preset recipes

Bad because it mixes primitives with theme policy.

### 3. Every UI instance must manually set every control’s style preset

Bad because adoption becomes tedious and inconsistent.

### 4. Theme presets are just color swaps

Bad because style families should differ in geometry, density, borders, and depth as well.

### 5. Controls lose the ability to override locally

Bad because custom UI work still needs escape hatches.

### 6. Theme family concepts are duplicated across controls

Bad because each control will slowly diverge in tone, naming, and behavior.

If `Minimal`, `Rounded`, `Outline`, or similar family ideas exist, they should be
centrally authored once and resolved outward.

### 7. Roles become a dumping ground for visual special cases

Bad because semantic roles must stay understandable.

If a use-case is truly one-off, it belongs in a local style override.

If it is structural and reusable within one control family, it may belong in a
control-specific visual mode.

If it is a whole-UI family rule, it belongs in the preset layer.

### 8. "Anything is possible" is implemented as "everything is public"

Bad because raw flexibility without boundaries leads to bloat.

The system should provide broad expressive power through:

* shared primitives
* centralized resolvers
* semantic roles
* local override

not through unbounded growth of tiny public setters and ad hoc style variants.

---

## Practical Resolution Pattern

### Control paint path should use effective style only

A control should paint using one final style reference.

Pseudo-flow:

```cpp
const Style& s = GetEffectiveStyle();
UiPaintStyledSurface(... s.palette, s.metrics, s.skin ...);
```

That means a control’s paint code should not need to know whether the style came from:

* explicit local override
* global preset resolution
* fallback default

It just paints the final resolved style.

This keeps paint code simple and stable.

---

## Serialization Intent

The global theme preset itself may be process-global rather than per-control serialized.

A local explicit style override, if used, may still be serializable exactly as the control already supports.

That means the theme system should not break existing style serialization logic.

Local explicit styles are durable.
Global theme context is ambient.

If explicit local style serialization already exists for controls, the introduction
of `UiTheme` should preserve that behavior rather than replacing it.

---

## Recommended Implementation Order

### Stage 1

Introduce `UiThemePreset`, `UiThemeMode`, and role enums.

### Stage 2

Add `UiTheme` API for global preset and mode.

### Stage 3

Implement resolver functions for button and label.

### Stage 4

Add per-control role and explicit-style-override flags.

### Stage 5

Make controls paint from `GetEffectiveStyle()`.

### Stage 6

Migrate old `StyleMinimal()`, `StyleSoft()`, etc. into either:

* compatibility wrappers over the resolver, or
* deprecated convenience presets

### Stage 7

Extend to other controls such as panels, tabs, tree/list, inputs, and toolbars.

---

## Agent Guidance: Design Intent in Plain Terms

If an implementation agent reads this document, it should understand the following:

1. The project already has strong styling primitives.
2. The goal is not to replace that low-level system.
3. The goal is to add a clean, curated preset layer above it.
4. The preset layer must be global by default.
5. Controls must still support local explicit override.
6. Control-specific style structs remain valid and necessary.
7. Theme presets should be authored centrally, not scattered across widgets.
8. The resolver must output concrete control styles composed from shared primitives.
9. The system must optimize for fast adoption by ordinary developers.
10. Most users should get a coherent modern UI simply by selecting one preset.

---

## Reference Header Sketch

```cpp
#ifndef _Ui_UiTheme_h_
#define _Ui_UiTheme_h_

#include <Ui/UiStyle.h>
#include <Ui/UiButton.h>
#include <Ui/UiLabel.h>

namespace Upp {

// -----------------------------------------------------------------------------
// Global theme preset family
// -----------------------------------------------------------------------------

enum class UiThemePreset {
    Minimal,
    Rounded,
    Linear,
    Solid,
    Outline,
    Compact,
    Layered
};

enum class UiThemeMode {
    Light,
    Dark,
    System
};

// -----------------------------------------------------------------------------
// Semantic roles per control family
// -----------------------------------------------------------------------------

enum class UiButtonRole {
    Standard,
    Accent,
    Subtle,
    Icon,
    Danger
};

enum class UiLabelRole {
    Body,
    Headline,
    Subheadline,
    Title,
    Caption,
    Badge,
    Footnote
};

// -----------------------------------------------------------------------------
// Optional future extension point for broader context
// -----------------------------------------------------------------------------

struct UiThemeContext {
    UiThemePreset preset = UiThemePreset::Minimal;
    UiThemeMode   mode   = UiThemeMode::Light;
};

// -----------------------------------------------------------------------------
// Theme manager / resolver entry points
// -----------------------------------------------------------------------------

class UiTheme {
public:
    static void SetPreset(UiThemePreset preset);
    static UiThemePreset GetPreset();

    static void SetMode(UiThemeMode mode);
    static UiThemeMode GetMode();

    static void SetContext(const UiThemeContext& ctx);
    static UiThemeContext GetContext();

    // Resolve concrete styles for controls from the active global context.
    static UiButton::Style ResolveButton(UiButtonRole role = UiButtonRole::Standard);
    static UiLabel::Style  ResolveLabel(UiLabelRole role = UiLabelRole::Body);

    // Resolve styles explicitly, without depending on global state.
    static UiButton::Style ResolveButton(UiThemePreset preset,
                                         UiThemeMode mode,
                                         UiButtonRole role = UiButtonRole::Standard);

    static UiLabel::Style ResolveLabel(UiThemePreset preset,
                                       UiThemeMode mode,
                                       UiLabelRole role = UiLabelRole::Body);
};

// -----------------------------------------------------------------------------
// Optional helper namespace if you prefer recipe-style organization
// -----------------------------------------------------------------------------

namespace UiThemeDefaults {
    UiButton::Style MakeButton(UiThemePreset preset,
                               UiThemeMode mode,
                               UiButtonRole role = UiButtonRole::Standard);

    UiLabel::Style MakeLabel(UiThemePreset preset,
                             UiThemeMode mode,
                             UiLabelRole role = UiLabelRole::Body);
}

} // namespace Upp

#endif
```

---

## Notes About This Header

This is intentionally a sketch, not final law.

The important architectural points are:

* the theme preset is global by default
* the resolver is central
* roles are semantic
* control style structs remain the final payload
* local override remains possible

The exact static-vs-instance approach for `UiTheme` can still be adjusted to fit the rest of the U++ architecture.

One implementation caution matters early: avoid creating unnecessary header coupling
between controls and `UiTheme`.

If roles, context, or notification hooks become shared dependencies, it is usually
cleaner to separate lightweight theme enums/context from resolver implementations
rather than making every control include heavy theme headers directly.

---

## Recommended Per-Control Integration Pattern

For controls like `UiButton` and `UiLabel`, the intended future shape is approximately:

```cpp
class UiButton : public Ctrl, public CtrlStyled<UiButton> {
public:
    UiButton& SetRole(UiButtonRole role);
    UiButtonRole GetRole() const;

    UiButton& SetStyle(const Style& s);          // explicit local override
    UiButton& ClearStyleOverride();
    bool HasStyleOverride() const;

    const Style& GetEffectiveStyle() const;

private:
    UiButtonRole role_ = UiButtonRole::Standard;
    bool         has_style_override_ = false;
    Style        style_;
    mutable Style resolved_style_cache_;
};
```

Equivalent pattern for `UiLabel`.

The control can cache the resolved style if useful, but cache invalidation must be clear whenever:

* global theme changes
* local role changes
* local visual mode changes
* explicit local style changes

If global theme notifications are introduced, they should be narrow and predictable.
The theme system should not require controls to poll or manually rebuild every paint.

Cache invalidation must remain explicit and cheap.

---

## Review Lens for Future Changes

When reviewing theme-related changes, ask:

1. Is this adding a new primitive token, or is it actually sneaking in theme policy?
2. Is this a global family concern, a semantic role concern, a structural control-mode concern, or a one-off local override?
3. Will this naming still make sense when more controls are added?
4. Does this change centralize policy, or spread it wider?
5. Does this make common usage simpler, or only make internals more configurable?

If a change cannot be placed cleanly on one layer, it likely needs redesign.

---

## Final Principle

The styling engine is already the orchestra.

`UiTheme` is the conductor and the score.

The goal is to let ordinary developers press one good preset and get a polished UI, without taking away the ability for experts to fine-tune anything later.
