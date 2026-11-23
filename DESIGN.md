---
name: A22
description: Firmware for the M-VAVE FM-1, presented as a printed synth voice-bank catalogue.
colors:
  paper: "#e2e6eb"
  sheet: "#f5f7f9"
  ink: "#14171c"
  ink-2: "#485059"
  rule: "#b8bfc8"
  rule-soft: "#d2d7dd"
  screen-black: "#000000"
  knob-blue: "#3b9eff"
  knob-green: "#2cc96b"
  knob-white: "#ffffff"
  knob-orange: "#ff6b1a"
  bank-a-fm4: "#3b9eff"
  bank-b-electric: "#44e37e"
  bank-c-stack: "#4ce07a"
  bank-d-analog: "#eef1f5"
  bank-e-shaper: "#2ae6e0"
  bank-f-wire: "#ffd24a"
  bank-g-haze: "#9b76ff"
  bank-h-mono: "#ff6b1a"
  bank-i-kit: "#ff6b1a"
  bank-j-perc: "#ffc252"
  bank-k-wave: "#3b9eff"
  bank-l-chip: "#44e37e"
  bank-m-cycle: "#ffaa40"
  bank-n-skins: "#eef1f5"
typography:
  display:
    fontFamily: "Barlow SC, Barlow Semi Condensed, Arial Narrow, sans-serif"
    fontSize: "clamp(64px, 9vw, 96px)"
    fontWeight: 700
    lineHeight: 0.9
    letterSpacing: "-0.02em"
  headline:
    fontFamily: "Barlow SC, Barlow Semi Condensed, Arial Narrow, sans-serif"
    fontSize: "clamp(30px, 3.4vw, 44px)"
    fontWeight: 700
    lineHeight: 1.05
    letterSpacing: "-0.01em"
  title:
    fontFamily: "Barlow SC, Barlow Semi Condensed, Arial Narrow, sans-serif"
    fontSize: "30px"
    fontWeight: 700
    lineHeight: 1.05
    letterSpacing: "-0.01em"
  lede:
    fontFamily: "Barlow SC, Barlow Semi Condensed, Arial Narrow, sans-serif"
    fontSize: "clamp(20px, 2.2vw, 26px)"
    fontWeight: 500
    lineHeight: 1.3
  body:
    fontFamily: "Barlow SC, Barlow Semi Condensed, Arial Narrow, sans-serif"
    fontSize: "18px"
    fontWeight: 500
    lineHeight: 1.55
    fontFeature: "tnum"
  small:
    fontFamily: "Barlow SC, Barlow Semi Condensed, Arial Narrow, sans-serif"
    fontSize: "14px"
    fontWeight: 500
    lineHeight: 1.45
  label:
    fontFamily: "Barlow SC, Barlow Semi Condensed, Arial Narrow, sans-serif"
    fontSize: "13px"
    fontWeight: 700
  button:
    fontFamily: "Barlow SC, Barlow Semi Condensed, Arial Narrow, sans-serif"
    fontSize: "18px"
    fontWeight: 700
  band-title:
    fontFamily: "Barlow SC, Barlow Semi Condensed, Arial Narrow, sans-serif"
    fontSize: "clamp(28px, 3vw, 38px)"
    fontWeight: 700
    lineHeight: 1.05
    letterSpacing: "-0.01em"
  code:
    fontFamily: "ui-monospace, SF Mono, Menlo, Consolas, monospace"
    fontSize: "15px"
rounded:
  hairline: "2px"
  sm: "3px"
  screen: "4px"
  dot: "50%"
spacing:
  gutter: "clamp(16px, 4vw, 48px)"
  xs: "8px"
  sm: "14px"
  md: "18px"
  lg: "28px"
  part: "clamp(56px, 9vw, 112px)"
components:
  button-primary:
    backgroundColor: "{colors.ink}"
    textColor: "{colors.sheet}"
    typography: "{typography.button}"
    rounded: "{rounded.sm}"
    padding: "14px 22px"
  bank-tab:
    backgroundColor: "{colors.bank-a-fm4}"
    textColor: "{colors.ink}"
    width: "66px"
    padding: "4px 0 4px 14px"
  bank-tab-selected:
    backgroundColor: "{colors.bank-a-fm4}"
    textColor: "{colors.ink}"
    width: "92px"
  bank-band:
    backgroundColor: "{colors.bank-a-fm4}"
    textColor: "{colors.ink}"
    typography: "{typography.band-title}"
    padding: "12px clamp(20px, 3vw, 32px) 12px clamp(16px, 4vw, 48px)"
  catalogue-sheet:
    backgroundColor: "{colors.sheet}"
    textColor: "{colors.ink}"
    rounded: "{rounded.sm}"
  screen-window:
    backgroundColor: "{colors.screen-black}"
    rounded: "{rounded.screen}"
    padding: "10px"
    size: "240px"
  bank-swatch:
    backgroundColor: "{colors.bank-a-fm4}"
    textColor: "{colors.ink}"
    rounded: "{rounded.hairline}"
    size: "18px"
  code-block:
    backgroundColor: "{colors.sheet}"
    textColor: "{colors.ink}"
    typography: "{typography.code}"
    rounded: "{rounded.sm}"
    padding: "12px 14px"
  margin-note:
    textColor: "{colors.ink-2}"
    typography: "{typography.small}"
  masthead:
    backgroundColor: "{colors.paper}"
    textColor: "{colors.ink-2}"
    padding: "22px clamp(16px, 4vw, 48px)"
  arrow-link:
    textColor: "{colors.ink}"
    typography: "{typography.button}"
---

# Design System: A22

## Overview

**Creative North Star: "The Voice-Bank Catalogue"**

A22's site is printed matter for a synthesiser: a voice-ROM catalogue on cool coated paper. Fourteen lettered bank tabs (A to N) run down the edge, each printed in its engine's screen ink. One bank is open at a time. Its ink fills the header band across the sheet, and below it the eight voices are listed in a ruled table with catalogue numbers (A1 to A8). Everything else is black print on grey-white paper, set in one condensed face, the FM-1's own Barlow lettering.

The page is dense where a catalogue is dense (voice tables, the spec sheet, the manual's charts) and quiet in between. Wide paper margins separate the parts, and secondary facts sit in small print in a right-hand margin column. Colour belongs to the hardware. The bank inks are the engines' screen inks, the knob dots are the FM-1's knob colours, and the real 240×240 screen captures sit in black windows the way the screen sits in the device. The only motion is a short print-in when a bank is opened.

The site turns down the dark product page: no neon accents on black, no wall of feature cards.

**Key Characteristics:**
- Cool paper ground with a lighter sheet. Black ink and hairline rules do the structural work.
- Bank inks used as flat, full-strength fills on tabs, header bands and swatches, never as tints or gradients.
- One face, Barlow Semi Condensed (Medium for text, Bold for headings), with tabular numerals.
- Ruled tables in place of cards. Catalogue codes (A1 to L8) in place of bullets.
- Real device screen captures, shown pixel-exact in black windows.
- Small-print margin notes for secondary facts.

## Colors

The palette is cool printed paper and black ink, with the FM-1's own screen and knob colours as the only chroma.

### Primary
- **Bank Inks** (`bank-a-fm4` to `bank-n-skins`): each bank's flat fill on its tab, its header band and its manual swatch. They are the per-engine inks set in the site generator, matching each engine's screen. Fourteen banks share ten distinct inks: FM-4 and WAVE share Signal Blue, ELECTRIC and CHIP share Phosphor Green, MONO and KIT share Knob Orange, and ANALOG and SKINS share the same white. The bank letter, not the ink, is what identifies a bank. A bank's ink appears only where that bank is named.

### Secondary
- **Knob Colours** (`knob-blue`, `knob-green`, `knob-white`, `knob-orange`): the FM-1's knob 1 to 4 colours, shown as 11px dots before each knob name in the knob legend and the manual's knob tables. The order is fixed (blue, green, white, orange) because the device's is. Knob Green is a separate value from the bank greens. Keep it separate.

### Neutral
- **Coated Paper** (`paper`): the page ground, the masthead, the part sections and the footer.
- **Sheet White** (`sheet`): the open catalogue sheet beside the tab rail, code blocks, the button's text, the selection text.
- **Print Black** (`ink`): text, headings, the primary button, focus rings and the selection background.
- **Second Ink** (`ink-2`): descriptions, column heads, margin notes, the footer and inactive masthead links.
- **Rule** (`rule`): section borders, the sheet's edge, spec and step rules, and the manual index's divider.
- **Soft Rule** (`rule-soft`): row rules inside voice, knob and chart tables, and the dividers between questions.
- **Screen Black** (`screen-black`): the window around every screen capture.

### Named Rules
**The Hardware Colour Rule.** Every colour on the page belongs to the FM-1: a bank's screen ink, a knob colour, or a real screen capture. Interface chrome (buttons, links, focus, rules) stays in black ink on paper.

**The Flat Ink Rule.** Bank inks are printed solid at full strength. Lines that sit on an ink fill use translucent black (`rgb(20 23 28 / .18–.35)`), so the same edge works on every bank.

## Typography

**Display Font:** Barlow Semi Condensed, self-hosted at 500 and 700 under the CSS family name "Barlow SC" (fallback: Arial Narrow, sans-serif)
**Body Font:** the same face at 500
**Label/Mono Font:** a system monospace (ui-monospace, SF Mono, Menlo, Consolas), only for shell commands and inline code

**Character:** the FM-1's own lettering, condensed and engineered, so the site reads in the device's voice. Bold carries the wordmark, headings and voice names. Medium carries everything else.

The user chose Barlow alone (2026-11-21): no wide display face. The device's own lettering is the type.

### Hierarchy
- **Display** (700, clamp 64–96px, line-height 0.9, −0.02em): the "A22" wordmark heading on the cover. Used once.
- **Headline** (700, clamp 30–44px, line-height 1.05): part headings ("What's inside", "Install") and manual chapter headings. The bank band's title uses the same weight a step smaller (clamp 28–38px).
- **Title** (700, 30px): the manual's bank headings, led by a lettered swatch. Subheads in two-column passages use 22px.
- **Lede** (500, clamp 20–26px, line-height 1.3, max 34ch): the single line under the wordmark.
- **Body** (500, 18px, line-height 1.55, max 60–64ch): running text and table cells.
- **Small** (500, 14px, line-height 1.45): margin notes, the reassurance line under the actions, the footer.
- **Label** (700, 13–15px): table column heads (No., Voice, Character) and the manual's "Knobs"/"Algorithms" subheads, set in Second Ink in sentence or title case. Tab engine names are 11px with +0.04em tracking.

### Named Rules
**The Device Lettering Rule.** Barlow Semi Condensed is the only text face. Monospace appears only inside code. Device vocabulary (PRESETS, ALGORITHM, REC, voice and knob names) is set in capitals as on the FM-1, and site prose stays in sentence case.

**The Tabular Rule.** Tables set numerals tabular, so catalogue codes and figures line up down the column.

## Layout

The front page opens with a cover. It is a two-column grid: a 92px tab rail, then the body, with a minimum height of clamp(660px, 100svh − 74px, 940px). The rail is sticky at the top and runs the cover's full height, with each tab taking an equal share (44px minimum). The body holds the title block: the wordmark and lede on the left, and on the right the actions and small print, in a column clamped to 260–380px. The open bank's sheet follows. The sheet's paper (Sheet White) starts at the top of the cover, right beside the rail, so the open tab meets its page.

Inside the bank, the screen window sits in an auto-width column on the left and the description, voice table, knob legend and manual link fill the rest. The ink band spans the full sheet width and bleeds to its edges.

After the cover, each **part** is a three-column grid (1fr / 1.6fr / minmax(180px, .7fr)): heading and aside, then the main content, then a margin note. Parts are separated by a full-width rule and padded clamp(56–112px) vertically. The horizontal gutter is clamp(16px, 4vw, 48px) everywhere. Vertical rhythm steps through 8, 14, 18 and 28px.

The manual is a 250px sticky index plus a content column, with chapters capped at 980px. Each manual bank pairs its screen window with the content column.

**Breakpoints** (the build's four):
- **≤1180px:** parts go to two columns, and the margin note drops under the main content.
- **≤980px:** the manual's index stops being sticky and becomes a collapsible "Contents" disclosure with the list in two columns. Manual banks and two-column passages stack.
- **≤820px:** the cover stacks in this order: title, tab strip, sheet. The tab rail becomes a horizontal scroll-snap strip of 54px-wide, 64px-tall tabs, and the sheet becomes a bordered card under it. Parts go to one column.
- **≤420px:** voice tables drop their header row and set each voice as a two-line grid (code, then name over description). Spec rows stack.

**Motion:** the tab width eases over .28s `cubic-bezier(.16, 1, .3, 1)`. Opening a bank runs a print-in: rows and the screen window fade up 4px from 40% opacity over .42s on the same curve, staggered 28ms per row. `prefers-reduced-motion: reduce` removes both.

## Elevation & Depth

The system is flat paper with no cast shadows. Depth comes from paper tone (Sheet White lying on Coated Paper), 1px rules, and the open tab growing out to meet the sheet. The only shadows are inset 1px hairlines that act as printed edges: the inner edge of an unselected tab, the ring around a knob dot (which keeps the white knob visible on white), and, at ≤820px, a 4px inset ink underline under the selected tab.

### Shadow Vocabulary
- **Tab edge** (`box-shadow: inset -1px 0 0 rgb(20 23 28 / .18)`): the right edge of an unselected tab, removed when the tab is selected so it joins the sheet.
- **Dot ring** (`box-shadow: inset 0 0 0 1px rgb(20 23 28 / .35)`): outlines every knob dot.
- **Strip selection** (`box-shadow: inset 0 -4px 0 #14171c`): the selected tab in the horizontal strip (≤820px).

### Named Rules
**The Paper Stack Rule.** Depth is paper on paper, drawn with rules and tone. Nothing floats, and no surface casts a shadow.

## Shapes

Shapes are rectangular, as on printed stock. Corners are barely softened: 2px on swatches and the focus ring, 3px on the button, the sheet's free corner and code blocks, and 4px on screen windows. Knob dots are the only circles. The sheet has no top or left border, because it joins the tab rail and the band. Tabs are square-cut, and their width (66px, 76px on hover, 92px selected) is the only shape that changes.

## Components

### Buttons
Exactly one solid button: the download.
- **Shape:** slightly softened rectangle (3px).
- **Primary:** Print Black fill, Sheet White Bold 18px text, 14px × 22px padding, with a 16px line-drawn download icon (1.8px stroke, square caps) before the label.
- **Hover / Focus:** hover darkens the fill slightly. Focus is the global 2px Print Black outline at a 3px offset.
- **Secondary (text link with arrow):** a Bold ink link followed by a 16px drawn arrow, with no underline at rest and an underline on hover. Used for "Play it in your browser" and "[engine] in the manual".

### Bank Tab Rail (signature)
A vertical stack of fourteen lettered tabs, each filled solid in its bank's ink. Each tab shows its letter in Bold 22px over the engine name at 11px, with translucent black hairlines between tabs. Unselected tabs are 66px wide and grow to 76px on hover. The selected tab is 92px wide, the rail's full width, so it meets the sheet's paper and reads as the tab of the open page. The rail is an ARIA tablist with arrow, Home and End keys. At ≤820px it becomes a horizontal snap strip with a 4px ink underline under the selected tab.

### Bank Header Band (signature, front page)
The open bank's ink spans the full width of the sheet's top edge. On the left, the letter and engine name in Bold ("A · FM-4"). On the right, the catalogue code in Bold 15px tabular ("Bank A · A1–A8"). A translucent black rule runs along the bottom.

### Voice Table
A ruled table with three columns: No. (the catalogue code in Second Ink, 3.2ch), Voice (the name in Bold capitals, 11ch) and Character (the description in Second Ink). Soft Rule hairlines separate the rows, with a closing rule after the last. On the front page it has printed column heads (13px Bold, Second Ink). The manual's version is headless and sits under a "Algorithms" label. At ≤420px each row stacks to two lines.

### Screen Window
Each real 240×240 capture of the FM-1's screen sits in a pure black frame with 10px padding and 4px corners, rendered pixelated so every device pixel stays sharp. Captures are always real device output.

### Knob Legend and Knob Table
The front page shows a row of four knob names, each led by an 11px dot in the knob's colour. The manual shows the same as a ruled two-column table ("K1 RATIO" with its dot, then what the knob does).

### Part Grid with Margin Notes
The three-column section used after the cover: heading and a short aside, then the main content (a spec table, steps or prose), then a small-print margin note in Second Ink, which starts with a Rule hairline above it.

### Spec Table and Install Steps
Spec rows put a Bold row label (9.5em) beside the value, separated by full Rule hairlines. Install steps are a ruled ordered list with Bold tabular step numbers. Commands sit in code blocks: Sheet White, a 1px Rule border, 3px corners, monospace 15px, and horizontal scroll rather than wrapping.

### Manual Index
The manual's index is a 250px sticky column with a Rule divider on its right. It has two groups, Banks and Playing, under 15px Bold Second Ink heads. Each bank entry is led by an 18px lettered swatch in its ink. Links are ink with no underline until hover. At ≤980px the index becomes a "Contents" disclosure.

### Navigation
- **Masthead:** a running head. "A22" in Bold 24px on the left, and Manual, Simulator and GitHub at 17px on the right in Second Ink. The current page and hovered links turn ink and underline. A Rule hairline sits underneath.
- **Footer:** small print in Second Ink. The licence line on the left. GitHub, Buy me a coffee and the trademark notice on the right, as plain underlined links.

## Do's and Don'ts

### Do:
- **Do** fill tabs, bands and swatches with the bank's own screen ink at full strength, and keep the bank letter visible wherever an ink appears.
- **Do** list voices in ruled tables with catalogue codes (A1–A8) and Bold capital names. Use tables, not cards.
- **Do** show real FM-1 screen captures at their native 240×240 in a black window with `image-rendering: pixelated`.
- **Do** put secondary facts in small-print margin notes (14px, Second Ink) under a Rule hairline.
- **Do** keep the knob colours in the device's order (blue, green, white, orange) and ring each dot with the translucent black hairline.
- **Do** sign the site as A22, and keep GitHub and Buy me a coffee as quiet links in the masthead or footer.
- **Do** honour `prefers-reduced-motion` by dropping the tab transition and the print-in.

### Don't:
- **Don't** introduce colour that isn't the FM-1's (a bank ink, a knob colour or a screen capture). Links, buttons and focus stay black ink.
- **Don't** cast drop shadows, use gradients or tint the bank inks. Depth is paper, tone and rules.
- **Don't** add a second text face. Monospace is only for code, and the user has declined a wide display face.
- **Don't** build a dark product page with neon accents or a grid of feature cards.
- **Don't** add a credits section or a "related to h1" FAQ. The repository carries the credits, and a small licence link in the footer is enough.
- **Don't** attach a personal name to the site.
