# PvZ Style Guide

A reusable design system extracted from the project design document
(`doc/design-document/design-document.tex`). Use it for future LaTeX
artifacts — progress reports, final writeup, slide handouts — so they
carry one visual identity.

The matching LaTeX package lives next door: **`pvz-style.sty`**.

## Quickstart

Drop `pvz-style.sty` next to your `.tex` source and load it:

```latex
\documentclass[11pt,letterpaper]{article}
\usepackage{pvz-style}

\begin{document}

\section{Hello}
\subsection{World}

Body text flows here with relaxed line spacing, \texttt{microtype}
justification, and \hyperref[sec:demo]{royal-blue} cross-references.

\end{document}
```

That gives you: the plum-sidebar section heading, the mist-tinted code
listing theme, booktabs tables with plum rules, microtype typography,
the gradient-accent palette — no additional setup needed.

## The palette

Five primary colors running warm-to-cool, plus two contrast helpers.

| Name | RGB | Hex | Swatch | Role |
|---|---|---|---|---|
| `pvzPlum`    | 119, 41, 93   | `#77295D` | ▓ deep plum          | Hero accent. LW bridge, section numbers, booktabs rules, emphasis. |
| `pvzMagenta` | 195, 79, 162  | `#C34FA2` | ▓ magenta pink       | HPS emphasis. Section eyebrows, TOC subsection numbers, string literals. |
| `pvzMist`    | 237, 241, 253 | `#EDF1FD` | ▒ pale lavender      | Soft fills / backgrounds. Code listing background, section rules, neutral block fills. |
| `pvzSky`     | 119, 183, 240 | `#77B7F0` | ▓ sky blue           | FPGA emphasis. Subsection bars, emphasized FPGA-side blocks, dataflow read paths. |
| `pvzRoyal`   | 83, 100, 192  | `#5364C0` | ▓ royal blue         | Structural. Internal links, subsection title text, data bus arrows, page numbers. |
| `pvzInk`     | 26, 26, 26    | `#1A1A1A` | ▓ near-black ink     | Body text on light fills. |
| `pvzOnDark`  | 255, 255, 255 | `#FFFFFF` | ░ white              | Text on dark fills only (Plum, Magenta, Royal). |

Three role aliases re-export the primary colors with semantic names.
Prefer these in TikZ / figure code so the intent reads at the call site:

```latex
\colorlet{pvzHPS}    {pvzMagenta}   % HPS / software side
\colorlet{pvzFPGA}   {pvzSky}       % FPGA / hardware side
\colorlet{pvzBridge} {pvzPlum}      % The thing between HPS and FPGA
```

## The contrast rule

**White text appears only on the three dark palette colors.** Sky and
Mist are too light to carry white type and always use dark text.

| Background | Text color |
|---|---|
| `pvzPlum`, `pvzMagenta`, `pvzRoyal` | `pvzOnDark` (white) |
| `pvzSky`, `pvzMist`, page white     | `pvzInk` (near-black) or `pvzRoyal` for emphasis |

This must be respected at every TikZ node, every table cell fill,
every call-out box. No exceptions. When a fill looks too light for
white text, use dark text.

## Role mapping for diagrams

When drawing HPS-vs-FPGA block diagrams, follow a warm/cool split. It
gives readers an at-a-glance distinction without having to read every
label.

| Element | Fill | Border | Text |
|---|---|---|---|
| HPS software (soft) | `pvzMist` | `pvzMagenta` | `pvzInk` |
| HPS emphasis (kernel driver, etc.) | `pvzMagenta` | `pvzMagenta` | `pvzOnDark` |
| FPGA module (soft) | `pvzMist` | `pvzSky` | `pvzInk` |
| FPGA emphasis (`pvz_top`, renderer) | `pvzSky` | `pvzRoyal` | `pvzInk` |
| External peripherals | `pvzMist` | `gray!40` | `pvzInk` |
| The LW bridge | `pvzPlum` | `pvzPlum` | `pvzOnDark` |
| Data bus arrows | — | `pvzRoyal` | `pvzInk` (labels) |

The plum bridge is the visual "here is where the two worlds meet"
marker. Use it sparingly — one plum element per figure is enough.

## Section headings

```latex
\section{System Block Diagram}      % ▍ SECTION 2
                                    %   System Block Diagram
                                    %   ─────────────────── (mist rule)

\subsection{Module Summary}         % ▍ 2.1  Module Summary  (sky bar, royal)
\subsubsection{Renderer FSM}        %    3.2.3 Renderer FSM  (royal bold)
```

All three levels render in sans-serif. Section titles use the default
serif font only in the body text, giving a clean designed-document
feel without a font swap.

## Tables

```latex
\begin{table}[H]
\centering
\caption{FPGA module inventory.}
\label{tab:modules}
{\pvzZebra  % optional: soft Mist zebra striping
\begin{tabularx}{\textwidth}{l l X}
\toprule
\pvzTH{Module} & \pvzTH{Type} & \pvzTH{Description} \\
\midrule
\texttt{pvz\_top}        & Avalon-MM agent & Top-level peripheral. \\
\texttt{shape\_renderer} & Scanline FSM    & Fills the draw buffer... \\
\bottomrule
\end{tabularx}}
\end{table}
```

- **`\pvzTH{...}`** wraps each column-header cell in sans-serif bold
  plum. Use it in every header row.
- **`{\pvzZebra ...}`** scoped around the tabular introduces soft Mist
  zebra striping. Use it only for wide or dense tables.
- Booktabs rules (`\toprule`, `\midrule`, `\bottomrule`) automatically
  render in plum at the configured widths (1.4pt / 0.6pt).

## Code listings

```latex
\begin{lstlisting}[language=pvzC, caption={Driver argument structures.}]
struct pvz_shape_arg_t {
    __u8  index;
    __u32 data0;       // type, visible, x, y
    __u32 data1;       // w, h, color
};
\end{lstlisting}

\begin{lstlisting}[language=pvzVerilog]
module shape_renderer (
    input  logic         clk,
    input  logic [47:0]  shape_entry,
    output logic [7:0]   pixel
);
endmodule
\end{lstlisting}
```

- **`pvzC`** and **`pvzVerilog`** are custom languages with two-tier
  keyword styling: tier-1 keywords (`struct`, `input`, `return`) render
  in plum bold, tier-2 (types like `__u32`, `logic`, `bit`) render in
  royal bold.
- Plain or other languages still get the palette theme — just no
  type-level highlighting. Use `language={}` or `language=tcl` as needed.
- The frame uses a royal translucent border with a soft Mist background
  tint. Line numbers are tiny plum.

## Links and cross-references

- `\ref{fig:foo}` and `\cite{bar}` in plum (`pvzPlum`).
- Internal page references in royal (`pvzRoyal`).
- External `\url{...}` in magenta (`pvzMagenta`).
- `\href{...}{text}` in royal.

No underlines — all link styling is color only.

## Typography defaults

The package ships with these typography settings; override them in your
document after `\usepackage{pvz-style}` if you need different values:

| Setting | Value | Where |
|---|---|---|
| Margins | 1in left/right, 1.2in top/bottom | `geometry` |
| Line spread | 1.15 | `\linespread` |
| Paragraph skip | 0.7em plus 0.15em minus 0.1em | `\setlength{\parskip}` |
| Caption skip (fig) | 6pt | `\captionsetup[figure]` |
| Caption skip (tbl) | 4pt | `\captionsetup[table]` |
| Float int-text sep | 14pt | `\setlength{\intextsep}` |
| `\parskip` in tables | 0pt (forced) | `\AtBeginEnvironment{tabular}` |

Body font stays Computer Modern (the LaTeX default). Headings and
sans-serif mocks use Computer Modern Sans. No external font packages
are loaded — the design leans on color + structure, not typography
exotica, to keep the .sty portable across any TeX Live install.

## Gotchas that took debugging in the design doc

These are the issues that cost time during the restyle of
`design-document.tex`. The `.sty` package encodes the fixes so future
documents shouldn't hit them, but it's worth knowing what's going on.

### 1. `titlesec` + `#1` inside `\parbox` → "Illegal parameter number"

The section heading format uses `\parbox[b]{0.88\textwidth}{... #1 ...}`
to stack the eyebrow above the title. Without the `[explicit]` package
option, titlesec's internal macro definition chokes on `#1` nested
inside the parbox:

```latex
% BROKEN
\usepackage{titlesec}
\titleformat{\section}[block]{...}{\parbox{...}{... #1 ...}}[...]
% → ! Illegal parameter number in definition of \ttlf@section.
```

The `.sty` loads titlesec with `[explicit]` to fix this:

```latex
\usepackage[explicit]{titlesec}
```

### 2. `tocloft` doesn't expose `\cftsecnumfont` by default

Some tutorials suggest `\renewcommand{\cftsecnumfont}{...}` for coloring
TOC section numbers. That command doesn't exist in stock tocloft and
errors with "Command \cftsecnumfont undefined." Use `\cftsecpresnum` /
`\cftsecaftersnum` instead — they wrap the number with arbitrary code:

```latex
\renewcommand{\cftsecpresnum}  {\color{pvzPlum}}
\renewcommand{\cftsecaftersnum}{\color{pvzInk}}
```

### 3. tabularx `X`-column baseline mismatch in header rows

Mixing `l` and `X` columns in tabularx puts the `X`-column first line
`\baselineskip` below the `l` columns. Most noticeable in the header
row, where a short header word ("Description") sits visibly lower than
"Module" / "Type" in adjacent `l` columns.

The `.sty` overrides `\tabularxcolumn` to prepend `\strut` so the first
line aligns with a fresh baseline:

```latex
\renewcommand{\tabularxcolumn}[1]{%
  >{\setlength{\parskip}{0pt}\setlength{\parindent}{0pt}%
    \raggedright\arraybackslash\strut\ignorespaces}p{#1}%
}
```

### 4. `\parskip = 0.7em` pollutes tabular cells

The relaxed body `\parskip` inserts vertical space inside p-column /
X-column cells, pushing content down. The `.sty` defends against this
via `etoolbox`:

```latex
\AtBeginEnvironment{tabular} {\setlength{\parskip}{0pt}}
\AtBeginEnvironment{tabularx}{\setlength{\parskip}{0pt}}
```

### 5. `rowcolors` requires `xcolor[table]`

`\pvzZebra` expands to `\rowcolors`, which needs the `table` option on
xcolor (else it's undefined). The `.sty` passes it in the xcolor load:

```latex
\RequirePackage[dvipsnames,svgnames,x11names,table]{xcolor}
```

### 6. Green survives in one place, by design

The design doc's Figure 7 (screen layout) uses `ForestGreen!25` and
`ForestGreen!40` for the 4×8 gameplay grid — that green *means grass*.
It is deliberately preserved outside the palette. In your own
documents, if you introduce a similar "grass" concept, keep it green;
don't force the palette onto semantically green content.

## File checklist — what each file is for

```
doc/style/
├── pvz-style.sty      ← The LaTeX package. Drop next to your .tex and
│                         load with \usepackage{pvz-style}.
├── style-guide.md     ← This file. The "how and why" manual.
```

The reference palette image lives at
`doc/design-document/color/color-design.jpg` for visual reference.

## Migrating an existing document

1. Add `pvz-style.sty` alongside the `.tex`.
2. Replace the existing preamble (up to `\begin{document}`) with:
   ```latex
   \documentclass[11pt,letterpaper]{article}
   \usepackage{pvz-style}
   ```
3. Replace any `\textbf{...}` column headers inside tables with
   `\pvzTH{...}`.
4. For two-tier code syntax, switch `[language=C]` → `[language=pvzC]`
   and `[language=Verilog]` → `[language=pvzVerilog]`.
5. Walk each TikZ figure once: replace `fill=blue!XX`, `fill=orange!XX`,
   `fill=green!XX`, `fill=purple!XX` with palette names per the role
   table above. Add `text=pvzInk` / `text=pvzOnDark` explicitly so the
   contrast rule is enforced at the call site.
6. Compile. Fix any overfull hbox warnings that emerged (pre-existing
   warnings are OK).

On a clean stock-TeX-Live system the migration is usually a 30-minute
job for a ~2000-line document.
