## 1. Preamble: palette + package loads

- [x] 1.1 Add new `\usepackage` lines in `design-document.tex` preamble: `titlesec`, `caption`, `tocloft`, `colortbl`, `microtype`. Place after existing `xcolor` load so package color options resolve correctly. Load `hyperref` last among packages that reference colors.
- [x] 1.2 After the `xcolor` load, add the seven `\definecolor` lines for `pvzPlum`, `pvzMagenta`, `pvzMist`, `pvzSky`, `pvzRoyal`, `pvzInk`, `pvzOnDark` using the exact RGB triplets from Decision 1.
- [x] 1.3 Add the three `\colorlet` aliases: `pvzHPS` → `pvzMagenta`, `pvzFPGA` → `pvzSky`, `pvzBridge` → `pvzPlum`.
- [x] 1.4 Run `pdflatex design-document` and confirm the build still succeeds with the new package loads (no layout changes yet).

## 2. Preamble: listings theme

- [x] 2.1 Replace the existing `\lstset{ ... }` block with the `pvzListing` style from Decision 5 (plum keywords, royal types, magenta strings, slate comments, Mist background, royal rule).
- [x] 2.2 Define the custom `pvzC` language derived from `[C]` with the Decision 5 two-tier keyword lists and type-level styling.
- [x] 2.3 Define the custom `pvzVerilog` language derived from `[Verilog]` with the Decision 5 two-tier keyword lists.
- [x] 2.4 Add `\lstset{style=pvzListing}` after the language definitions so every block defaults to it.

## 3. Preamble: section, caption, TOC, hyperref, and typography

- [x] 3.1 Add the `\pvzSectionBar` TikZ-node helper and the `\titleformat{\section}` block from Decision 4 (plum vertical bar + magenta eyebrow + ink title + Mist rule).
- [x] 3.2 Add the `\titleformat{\subsection}` block from Decision 4 (Sky bar + royal number/title).
- [x] 3.3 Add the `\captionsetup` package options and `\captionsetup[figure]` / `[table]` spacing from Decision 7.
- [x] 3.4 Replace the current `\usepackage[...]{hyperref}` options with `linkcolor=pvzRoyal`, `urlcolor=pvzMagenta`, `citecolor=pvzPlum`, `filecolor=pvzRoyal` per Decision 9.
- [x] 3.5 Add `tocloft` renewcommands for `\cftsecfont`, `\cftsecpagefont`, `\cftsecnumfont`, `\cftsubsec*fonts`, and `\cftdotsep` per Decision 11.
- [x] 3.6 Add `\linespread{1.15}`, update `\usepackage[margin=1in]{geometry}` to `\usepackage[margin=1in,top=1.2in,bottom=1.2in]{geometry}`, and add the `\setlength` lines for `\parskip`, `\abovecaptionskip`, `\belowcaptionskip`, `\intextsep` per Decision 12.
- [x] 3.7 Define the two table helpers from Decision 6: `\pvzTH{#1}` column-header macro and `\pvzZebra` row-color command. Add `\arrayrulecolor{pvzPlum}` and the `\heavyrulewidth` / `\lightrulewidth` lengths.
- [x] 3.8 Compile and visually inspect: every section renders with side-bar + eyebrow; every subsection renders with Sky bar; TOC numbers are tinted; captions show "Figure N:" in plum small caps; links are Royal/Magenta.

## 4. Title page restyle

- [x] 4.1 Replace the current `\begin{center} ... \end{center}` title block (near the top of the document, around the team `tabular`) with the Decision 10 block: sans-serif Huge ink title, gradient rule, royal subtitle line, magenta author names in the `tabular`.
- [x] 4.2 Compile and check: title page uses the new layout; gradient rule renders with Plum→Mist→Royal arc; author rows are Magenta names + typewriter UNIs.

## 5. TikZ figure 1 — `fig:system-block`

- [x] 5.1 In the `\begin{tikzpicture}` at `design-document.tex:125` (system block), update the `block` style: `fill=pvzMist, draw=pvzMagenta, text=pvzInk` (HPS soft blocks).
- [x] 5.2 Update the `hwblock` style: `fill=pvzMist, draw=pvzSky, text=pvzInk` (FPGA soft blocks).
- [x] 5.3 Update the `extblock` style: `fill=pvzMist, draw=gray!40, text=pvzInk` (external peripherals; previously `fill=green!10`).
- [x] 5.4 Update the kernel-driver node's override `fill=blue!16` → `fill=pvzMagenta, text=pvzOnDark`.
- [x] 5.5 Update the `pvztop` node's override `fill=orange!20` → `fill=pvzSky, draw=pvzRoyal, text=pvzInk`.
- [x] 5.6 Update the bridge rectangle (`right=1.2cm of driver`) from `fill=gray!10` → `fill=pvzPlum, draw=pvzPlum`. Keep the dashed border style; the "LW Bridge" label color set to `pvzPlum` (or Ink on white if outside the plum rectangle).
- [x] 5.7 Update `bus` and `dbus` styles' `color=NavyBlue` → `color=pvzRoyal`.

## 6. TikZ figure 2 — `fig:peripheral-internal`

- [x] 6.1 At `design-document.tex:239`, walk the figure's `\tikzset` and any inline node styles. Replace every `fill=blue!..`, `fill=orange!..`, `fill=purple!..` with the matching `pvz*` color family: memories → `pvzSky`/`pvzMist`; combinational blocks → `pvzMist`/`pvzInk`; the Avalon-MM agent stays the emphasized Sky block.
- [x] 6.2 Ensure no white-text-on-light violations: any node with `fill=pvzMist` or `fill=pvzSky` uses `text=pvzInk`.

## 7. TikZ figure 3 — `fig:scanline-timing`

- [x] 7.1 At `design-document.tex:541`, update any colored strokes or fills to the palette (most of this figure is axis/timeline strokes; check that `draw=...` references are palette-native, typically `pvzRoyal` or `pvzInk`).

## 8. TikZ figure 4 — `fig:scanline-dataflow`

- [x] 8.1 At `design-document.tex:594`, update the `ext` style's `fill=green!10` → `fill=pvzMist, draw=gray!40`.
- [x] 8.2 Update any other block styles to the palette (HPS vs FPGA role mapping as in Decision 2).
- [x] 8.3 In the figure's caption/prose reference at `design-document.tex:654`, change "green path" → "sky-blue path" (Decision 8 prose edit).

## 9. TikZ figure 5 — `fig:shape-bits`

- [x] 9.1 At `design-document.tex:1258`, change the `y[8:0]` node's `fill=green!10` → `fill=pvzMist, draw=pvzSky!50, text=pvzInk`.
- [x] 9.2 Update any sibling field nodes' fills for palette consistency (x-field, kind-field, etc.).

## 10. TikZ figure 6 — `fig:sw-stack`

- [x] 10.1 At `design-document.tex:1322`, update block styles: user-space game-loop box → `fill=pvzMist, draw=pvzMagenta`; kernel driver → `fill=pvzMagenta, text=pvzOnDark`; Avalon register writes → `fill=pvzSky, text=pvzInk`.

## 11. TikZ figure 7 — `fig:screen-layout` (carve-out)

- [x] 11.1 At `design-document.tex:1538-1557`, **leave `ForestGreen!25` and `ForestGreen!40` untouched**. These are the grass checkerboard; green has semantic meaning here per Decision 8.
- [x] 11.2 Restyle only the non-grass elements in this figure: HUD band background (`fill=gray!15` at line 1538) → `fill=pvzMist` with dark text; dimension labels (`text=gray`) → `text=pvzInk` or `pvzRoyal!70`.

## 12. Listings language switches (body)

- [x] 12.1 Search the body for `[language=C]` and replace all occurrences with `[language=pvzC]`.
- [x] 12.2 Search the body for `[language=Verilog]` and replace all occurrences with `[language=pvzVerilog]`.
- [x] 12.3 Leave `[language={}]` and `[language=tcl]` blocks alone — they use single-tier styling automatically via `\lstset`.

## 13. Tables

- [x] 13.1 For each `\begin{tabular}...\toprule` in the document (10+ occurrences), replace the first-row header cells with `\pvzTH{header}` syntax. Example: `Module & Role & Notes` → `\pvzTH{Module} & \pvzTH{Role} & \pvzTH{Notes}`.
- [x] 13.2 For the two widest tables (register map at §Hardware/Software Interface, shape-table index allocation), wrap the `\begin{tabular}` in a `\pvzZebra` group per Decision 6.
- [x] 13.3 Compile and check: booktabs rules render in plum; headers render in plum small caps; zebra striping appears on the two flagged tables only.

## 14. Build and visual verification

- [x] 14.1 `pdflatex design-document` → `bibtex` (if used) → `pdflatex` twice more to settle the TOC and cross-references. No missing-package errors.
- [x] 14.2 Open the regenerated PDF and verify every section heading uses the side-bar + eyebrow layout.
- [x] 14.3 Verify every code listing uses the palette theme (plum keywords, royal types, magenta strings, mist background).
- [x] 14.4 Verify every table uses plum rules and small-caps headers.
- [x] 14.5 Verify Figure 7 grass is still green; verify Figures 1, 4, 5 have Mist replacing the old `green!10` structural fills.
- [x] 14.6 Verify no white text appears on Sky, Mist, or white fills anywhere in the document.
- [x] 14.7 Verify links are Royal; URLs are Magenta; TOC numbers are plum/magenta.
- [x] 14.8 Grep the `.log` file for `Overfull \hbox` warnings >4pt; fix any that emerged.

## 15. Humanize prose touch-up (carried over from sibling change)

- [x] 15.1 The only prose change in this restyle is the "green path" → "sky-blue path" edit at `design-document.tex:654`. Confirm no other prose was touched during the restyle.

## 16. Commit and archive

- [x] 16.1 Commit the preamble and body changes in two commits for easier review: first the preamble (1.x, 2.x, 3.x, 4.x), second the body (5.x–13.x). Commit message style matches existing `doc:` prefix convention.
- [x] 16.2 Regenerate `doc/design-document/design-document.pdf` and commit it as the third commit.
- [ ] 16.3 Once merged to `v2Dino` and the PDF is reviewed by the team, move `openspec/changes/restyle-design-doc-colors/` to `openspec/changes/archive/YYYY-MM-DD-restyle-design-doc-colors/` per the project's openspec archive convention.
