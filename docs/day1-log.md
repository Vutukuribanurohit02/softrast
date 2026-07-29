\# Software Rasterizer — Day 1 Log



\*\*Project:\*\* `softrast` — CPU-only software rasterizer in C++, no GPU API

\*\*Sprint item:\*\* 1 of 6 (Aug/Sep hiring window, target: Arm Mali GPU architecture)

\*\*Scope:\*\* weeks 1–2 of the sprint

\*\*Date:\*\* 29 July 2026

\*\*Repo:\*\* https://github.com/Vutukuribanurohit02/softrast

\*\*Commit:\*\* `e6876ec` — Day 1: framebuffer, BMP writer, edge-function triangle with counters



\---



\## 1. Environment setup



| Item | Status |

|---|---|

| Visual Studio Community 2026 (18.8.2) | Installed |

| Desktop development with C++ workload | Confirmed present |

| MSVC compiler | Version 19.51.36252 — verified with `cl` |

| Git for Windows | Version 2.55.0 |

| Project directory | `C:\\dev\\softrast` |

| GitHub remote | `origin` → `Vutukuribanurohit02/softrast`, branch `main` |



Editor is VS Code. The Visual Studio IDE is not used directly — only its compiler,

invoked from the Developer Command Prompt.



\*\*Note:\*\* the prompt opened from inside VS is the x86 one. Prefer \*\*x64 Native Tools

Command Prompt for VS 2026\*\* going forward.



\### Build and run



```

cd /d C:\\dev\\softrast

cl /EHsc /O2 /std:c++17 main.cpp

main.exe

start out.bmp

```



\---



\## 2. What was built



A single file, `main.cpp` (123 lines), containing four pieces.



\### Framebuffer



A `std::vector<Color>` of `w \* h` RGB triples, row 0 = top. Operations: `clear(Color)`

and `set(x, y, Color)`. No windowing library — output goes to a file, so the renderer is

headless and testable from day one.



\### BMP writer



24-bit uncompressed BMP, written by hand. Chosen over PPM because Windows Photos opens

BMP on a double-click.



Two format details that are easy to get wrong:



\- \*\*Rows are stored bottom-up\*\* when the header height is positive, so the write loop

&#x20; runs `y` from `h - 1` down to `0`.

\- \*\*Pixels are stored BGR\*\*, not RGB.



Rows are padded to a 4-byte boundary. Header is 54 bytes: a 14-byte file header plus a

40-byte BITMAPINFOHEADER.



\### Rasterizer



The core is one function:



```

edge(a, b, p) = (p.x - a.x) \* (b.y - a.y) - (p.y - a.y) \* (b.x - a.x)

```



This is the signed area of the parallelogram spanned by `b - a` and `p - a`. It is

positive on one side of the line through `ab`, negative on the other, zero exactly on

it. Evaluating it for all three edges and testing the signs answers "is this pixel

inside?".



Per triangle:



1\. Compute signed area; reject if zero (degenerate).

2\. If negative, swap two vertices so winding is consistent — this makes the

&#x20;  "all three edge functions >= 0" test valid for either input winding.

3\. Compute the screen-space bounding box, clamped to the framebuffer.

4\. For every pixel in the box, sample at the \*\*pixel centre\*\* `(x + 0.5, y + 0.5)`

&#x20;  and evaluate the three edge functions.

5\. If all three are non-negative, the pixel is inside. Divide each by the total area to

&#x20;  get \*\*barycentric weights\*\*, and interpolate vertex colour with them.



This is the same structure real GPU rasterizers use — Mali, Adreno and desktop parts all

evaluate edge equations, just in fixed point and on many pixels in parallel.



\### Instrumentation



Four counters, built in from the start rather than bolted on later:



| Counter | Meaning |

|---|---|

| `trisSubmitted` | Triangles handed to the rasterizer |

| `trisRasterized` | Survived degenerate/winding rejection |

| `bboxPixels` | Pixels tested inside bounding boxes |

| `fragsShaded` | Pixels that passed the inside test |



Plus a derived efficiency figure: `fragsShaded / bboxPixels`. This is the part that

separates an architecture portfolio piece from a rendering tutorial follow-along.



\---



\## 3. Results



First run: 800 x 600, one triangle with vertices (400, 80), (120, 500), (680, 520).



```

rendered 800x600 in 0.852 ms

triangles submitted : 1

triangles rasterized: 1

bbox pixels tested  : 247401

fragments shaded    : 120420

efficiency          : 48.7%

```



\### Validation



\*\*Bounding box.\*\* The triangle spans x in \[120, 680] and y in \[80, 520], so the clamped

box is 561 x 441 = 247,401 pixels. Matches `bboxPixels` exactly — loop bounds are right.



\*\*Coverage.\*\* True area by the shoelace formula:



```

A = 1/2 |x\_a(y\_b - y\_c) + x\_b(y\_c - y\_a) + x\_c(y\_a - y\_b)|

&#x20; = 1/2 |400(500 - 520) + 120(520 - 80) + 680(80 - 500)|

&#x20; = 1/2 |-8,000 + 52,800 - 285,600|

&#x20; = 120,400 px^2

```



Measured `fragsShaded` = 120,420 — agreement of \*\*0.02%\*\* (20 pixels, the expected

boundary discretisation).



This matters because the most common bug in a fresh rasterizer is a broken fill rule

that either double-covers shared edges or drops them. A 0.02% match against closed-form

area rules that out.



\### Baseline



\- \*\*0.852 ms\*\* for 120,420 fragments ≈ \*\*141 M fragments/sec\*\*, single-threaded,

&#x20; unoptimised.

\- \*\*48.7% efficiency\*\* — roughly half of every bounding box is wasted work. Not a bug;

&#x20; inherent to bounding-box traversal, and precisely why real hardware rasterizes in

&#x20; tiles and 2x2 quads instead.



Every optimisation later gets measured against these two numbers.



\---



\## 4. Version control state



```

main.cpp        123 lines, committed

.gitignore      \*.exe, \*.obj, \*.bmp

```



Commit `e6876ec`, authored as Banu Rohit Vutukuri <vutukuribanurohit02@gmail.com>,

pushed to `origin/main`.



The author was initially committed with placeholder details, then corrected with

`git commit --amend --reset-author` and force-pushed with `--force-with-lease`.

A commit with a mismatched email does not appear on the GitHub contribution graph, so it

does not count as visible evidence of work.



\### Practices worth keeping



\- Commit at the end of every session. Dated commits across two weeks read as sustained

&#x20; engineering; one large "initial commit" on day 14 reads as a weekend clone of someone

&#x20; else's tutorial.

\- Check `pwd` before running `git init` (a repo was accidentally initialised in

&#x20; `AppData\\Roaming\\SPB\_Data` and had to be removed).

\- Read the compiler's own first error line before anything else. `C1083: Cannot open

&#x20; source file` was the real message; the Windows "cannot find out.bmp" dialog was

&#x20; downstream noise.

\- Watch `--flag` vs `-- flag`. A stray space silently changes the meaning.



\---



\## 5. Open item



\*\*The rendered BMP has not yet been visually confirmed.\*\* The numbers strongly imply

correctness, but the file format is unverified by eye. Expected: a triangle with red,

green and blue corners blending smoothly toward each other on a near-black (12, 14, 18)

background.



| Symptom | Likely cause |

|---|---|

| Vertically flipped | Bottom-up row order handled backwards |

| Red and blue swapped | BGR byte order wrong |

| Diagonal skew / striping | Row padding to 4-byte boundary wrong |

| Colours banded, not smooth | Barycentric interpolation or uint8\_t cast issue |



Everything downstream — depth buffering, texturing, the demo GIFs — renders through this

writer.



\---



\## 6. Next: days 2–5, the z-buffer



1\. \*\*Add a depth buffer\*\* — a `std::vector<float>` parallel to the colour buffer,

&#x20;  cleared to +infinity.

2\. \*\*Give vertices a z coordinate\*\* and interpolate depth with the barycentric weights

&#x20;  already being computed.

3\. \*\*Depth test\*\* per fragment: keep it only if nearer than what is stored, then write

&#x20;  both colour and depth.

4\. \*\*Draw two intersecting triangles\*\* at different depths. Correct output shows them

&#x20;  interpenetrating cleanly with no sorting anywhere in the code — that is the whole

&#x20;  point of a z-buffer.

5\. \*\*Add two counters:\*\* `depthTestsFailed` and an overdraw ratio

&#x20;  (`fragsShaded / pixelsCovered`).



That overdraw number is the bridge to the Mali material in sprint item 3. Once the scene

has real depth complexity, overdraw shows how much shading work an immediate-mode

renderer throws away — exactly the problem tile-based architectures exist to solve.

Measuring it here means the tile-based rendering explainer later cites your own data

instead of quoting a blog post.



\### Remaining sprint calendar



| Days | Work |

|---|---|

| 1 | Framebuffer, BMP output, first triangle — done |

| 2–5 | Z-buffer, depth interpolation, overdraw counter |

| 6–8 | OBJ loader, model-view-projection transforms, perspective-correct interpolation |

| 9–11 | Texture mapping, basic lighting, near-plane clipping |

| 12–14 | README, demo GIFs, counter tables and write-up |



Link this repo to `processor-design-study` in the final README. A CPU architecture study

alongside a rasterizer with fragment counters reads as one coherent line of interest in

hardware, not two unrelated hobby projects.

