\# Software Rasterizer — Day 2 Log



\*\*Project:\*\* `softrast` — CPU-only software rasterizer in C++, no GPU API

\*\*Sprint item:\*\* 1 of 6 — weeks 1–2

\*\*Date:\*\* 29 July 2026

\*\*Repo:\*\* https://github.com/Vutukuribanurohit02/softrast

\*\*Commit:\*\* `f00dcc5` — Day 2: z-buffer, depth interpolation, overdraw counters, order-independence test

\*\*History:\*\* `e6876ec` (day 1) → `5b6ad12` (day 1 log) → `6710f5d` (README) → `f00dcc5` (day 2)



\---



\## 1. Goal for the day



Day 1 could draw one triangle. It had no concept of depth, so two overlapping

triangles would simply have been painted in submission order, last one winning.



Day 2 adds \*\*per-pixel visibility\*\*: a depth buffer, depth interpolation, a depth

test, and the counters needed to measure how much shading work that test throws away.



\---



\## 2. Day 1 loose ends, closed



\*\*The BMP writer is correct.\*\* Confirmed two independent ways:



\- \*By arithmetic.\* `out.bmp` is 1,440,054 bytes. Expected: 800 x 600 x 3 + 54 =

&#x20; 1,440,054. Header size, row stride and padding check out to the byte. (2400 bytes

&#x20; per row is already 4-byte aligned, so padding is zero at this resolution.)

\- \*By eye.\* The day-1 image opened correctly: red apex top, green bottom-left, blue

&#x20; bottom-right, smooth blending, near-black background. No vertical flip, no BGR

&#x20; channel swap, no row-padding skew.



\*\*Toolchain gotcha, recorded so it stops costing time.\*\* `cl` only exists inside the

Visual Studio environment. A plain Command Prompt gives `'cl' is not recognized`, and

`cd`-ing does not help — a \*different window\* is required:



> Start menu → \*\*x64 Native Tools Command Prompt for VS 2026\*\*



The correct window prints a `\*\* Visual Studio 2026 Developer Command Prompt \*\*`

banner and `Environment initialized for: 'x64'`. Worth pinning to the taskbar.



\---



\## 3. What was added



\### Depth buffer



A `std::vector<float>` running parallel to the colour buffer, one entry per pixel,

cleared to `+infinity` so any real geometry is nearer than the initial state.

Convention: \*\*smaller z = nearer the viewer\*\*.



\### Depth interpolation



The barycentric weights `l0, l1, l2` were already being computed for colour

interpolation in day 1. Depth reuses them for free:



```cpp

float z = l0 \* a.z + l1 \* b.z + l2 \* c.z;

```



Worth being precise about why this is exact here: there is no perspective projection

yet, so screen-space linear interpolation of z is mathematically correct. \*\*Once a

projection matrix is introduced on day 6-8 this stops being true\*\*, and interpolation

has to happen in `1/w` space instead. That is the perspective-correct interpolation

task, and this is the line of code that will have to change.



\### Depth test



```cpp

if (z >= fb.depth\[i]) { st.depthTestsFailed++; continue; }

fb.depth\[i] = z;

fb.color\[i] = /\* interpolated colour \*/;

```



Three lines. That is the whole z-buffer algorithm — no sorting, no BSP tree, no

per-triangle ordering logic anywhere in the program.



\### Depth visualisation



`writeDepthBMP()` normalises the depth buffer to the range actually used and writes

it as greyscale — near is bright, untouched pixels stay black.



\### New counters



| Counter | Meaning |

|---|---|

| `depthTestsFailed` | Fragments shaded, then discarded by the depth test |

| `pixelsWritten` | Fragments that won the depth test and wrote colour |

| `pixelsCovered` | Distinct pixels touched at least once |



Two derived figures:



\- \*\*overdraw\*\* = `fragsShaded / pixelsCovered` — how many times the average visible

&#x20; pixel was shaded.

\- \*\*shading wasted\*\* = `depthTestsFailed / fragsShaded` — the fraction of all shading

&#x20; work computed and then thrown away.



\---



\## 4. The test scene



Three triangles arranged so that \*\*no submission order can render them correctly\*\*:



| Triangle | Geometry | Depth |

|---|---|---|

| A (red) | Wedge opening left | z 0.10 at left edge → 0.90 at right tip |

| B (green) | Wedge opening right | z 0.10 at right edge → 0.90 at left tip |

| C (blue) | Narrow vertical blade | Flat z = 0.50 throughout |



A and B tilt in opposite directions, so each is nearer on its own side. C sits at

constant mid-depth and cuts through both.



This is deliberately a case a painter's algorithm cannot solve. Sorting by depth

requires that one primitive be wholly in front of another, and here none is.



\---



\## 5. Results



```

\[test] coverage vs shoelace area: 120420 vs 120400  (0.017% error)  PASS

\[test] draw-order independence (3 permutations): PASS



rendered 800x600, 3 triangles, in 1.888 ms

&#x20; triangles submitted : 3

&#x20; triangles rasterized: 3

&#x20; bbox pixels tested  : 498483

&#x20; fragments shaded    : 248000

&#x20; depth tests failed  : 49532

&#x20; pixels written      : 198468

&#x20; pixels covered      : 173468

&#x20; raster efficiency   : 49.8%

&#x20; overdraw            : 1.43x

&#x20; shading wasted      : 20.0%

```



\### The image proves itself



\*\*The blue blade is in front at the top and bottom of the frame, and behind in the

middle.\*\* No draw order can produce that — a painter's algorithm must commit to

"blue before red" or "red before blue" for the entire primitive. Visibility is being

decided per pixel.



\*\*A hard vertical seam runs down the centre where red meets green.\*\* That line is the

set of points where the two wedges have equal depth. Nothing in the code computes a

plane-plane intersection; it falls out of two independent per-pixel comparisons.

Emergent 3D intersection from a scalar test.



\*\*`depth.bmp` is a clean bowtie gradient\*\*, bright at the left and right edges

(z = 0.10) fading toward the tips (z = 0.90), no banding or discontinuities —

barycentric depth interpolation is behaving.



\### Accounting checks



\- `pixelsWritten` 198,468 + `depthTestsFailed` 49,532 = \*\*248,000\*\* = `fragsShaded`.

&#x20; Every fragment either won or lost the depth test; none is double-counted or dropped.

\- `raster efficiency` 49.8% — consistent with day 1's 48.7%. Bounding-box traversal

&#x20; wastes about half of every box regardless of scene.



\### Cost scales with fragments, not primitives



| | Triangles | Fragments | Time |

|---|---|---|---|

| Day 1 | 1 | 120,420 | 0.852 ms |

| Day 2 | 3 | 248,000 | 1.888 ms |



2.06x the fragments, 2.22x the time. Near-linear in fragment count. This is a

fragment-bound renderer, which is worth knowing before optimising anything.



\---



\## 6. The order-independence test



The most valuable thing built today, and it is a test rather than a feature.



`Framebuffer::checksum()` computes an FNV-1a hash over the whole colour buffer.

`main()` renders the identical scene in three different submission orders — `{0,1,2}`,

`{2,1,0}`, `{1,2,0}` — and compares hashes. All three match, bit for bit.



\*\*Why this matters.\*\* It converts a claim into a demonstration. "A z-buffer makes the

image independent of draw order" is a sentence anyone can write in a README. A hash

comparison across permutations of a scene specifically constructed so that no correct

ordering exists is \*evidence\*. It is also a genuine regression test: any future change

that breaks depth handling trips it immediately.



The day-1 coverage check survives as `validateCoverage()` and still passes at 0.017%

error against the closed-form shoelace area, confirming the day-2 rewrite did not

disturb the rasterizer core.



\---



\## 7. The number that matters: 20% shading wasted



Of 248,000 fragments shaded, \*\*49,532 were discarded\*\* by the depth test. Each cost

three barycentric divides, a depth interpolation, and three colour interpolations —

then was overwritten or thrown away.



This is the hook into the GPU architecture material, and why the counters were built

in from day 1 rather than added at the end:



\- On real hardware, wasted fragments are wasted \*\*bandwidth and power\*\*, not just

&#x20; cycles. Every discarded fragment still consumed a depth-buffer memory read.

\- Immediate-mode GPUs mitigate this with \*\*early-Z\*\*: move the depth test \*before\*

&#x20; the fragment shader so failing fragments are rejected before shading cost is paid.

\- Mobile GPUs go further. \*\*Tile-based architectures\*\* (Mali included) partition the

&#x20; framebuffer into small tiles, keep depth and colour for a tile in fast on-chip

&#x20; memory, and resolve visibility per tile — so expensive external memory traffic

&#x20; happens once per tile rather than once per fragment.



Sprint item 3 is the tile-based rendering explainer. Because of these counters, it can

cite measurements from this renderer instead of quoting someone else's blog post. That

difference is the whole reason this project reads as an architecture piece rather than

a graphics tutorial.



\---



\## 8. Repository state



```

README.md            front page

main.cpp             \~250 lines: framebuffer, BMP + depth writers,

&#x20;                    rasterizer with depth test, counters, two tests

.gitignore           \*.exe, \*.obj, \*.bmp

docs/day1-log.md

docs/day2-log.md     this file

```



Six commits, all correctly attributed, all dated.



\*\*Note for writeup day (12-14):\*\* `.gitignore` excludes `\*.bmp`, which is correct —

1.4 MB uncompressed frames do not belong in git history. But README figures must be

committed for GitHub to display them. Plan: convert the good renders to PNG into

`docs/images/` and force-add with `git add -f docs/images/\*.png`.



\---



\## 9. In progress: day 3



Code written, not yet run or committed. Two additions:



\*\*Overdraw heatmap.\*\* A per-pixel shade counter rendered as false colour — black =

never shaded, blue = once, green = twice, yellow = three times, red = four or more.

Turns the 1.43x average into a picture showing \*where\* the waste is.



\*\*Depth prepass.\*\* Render the scene twice: pass one writes depth only, shading

disabled entirely; pass two renders with depth writes off and the depth function set

to `Equal`, so only fragments that are exactly the nearest survive to be shaded.

Expected result: overdraw \~1.00x and near-zero wasted shading, at the cost of a second

geometry pass. This is what a depth prepass does in a real engine, and it is the

software analogue of early-Z.



A correctness test guards it: the prepass image must be \*\*pixel-identical\*\* to the

naive render (FNV hash comparison), otherwise it is not an optimisation but a bug.



\*\*Expected honest result:\*\* the prepass will likely be \*slower\* in wall-clock time,

because this fragment shader is three multiply-adds — trivially cheap — while a second

full rasterization pass is not. That is the real engineering tradeoff: a prepass pays

off only when shading is expensive relative to geometry. Reporting that plainly is

more valuable than reporting a win.



\---



\## 10. Sprint calendar



| Days | Work | Status |

|---|---|---|

| 1 | Framebuffer, BMP output, first triangle | Done — `e6876ec` |

| 2 | Z-buffer, depth interpolation, overdraw counters | Done — `f00dcc5` |

| 3 | Overdraw heatmap, depth prepass, prepass/naive comparison | In progress |

| 4-5 | Triangle clipping, backface culling, scanline vs bbox traversal | |

| 6-8 | OBJ loader, model-view-projection, perspective-correct interpolation | |

| 9-11 | Texture mapping, basic lighting, near-plane clipping | |

| 12-14 | README, demo images, counter tables, write-up | |



\---



\## 11. Concepts learned today



\- A depth buffer is a parallel float array cleared to infinity, and the entire

&#x20; algorithm is one comparison per fragment.

\- Barycentric weights computed for the inside test are reusable for interpolating

&#x20; \*any\* vertex attribute — colour, depth, and later UVs and normals.

\- Screen-space linear depth interpolation is exact only without perspective

&#x20; projection. This breaks on day 6 and is the reason perspective-correct

&#x20; interpolation exists.

\- Correct 3D surface intersection is emergent from per-pixel depth comparison; it is

&#x20; never computed explicitly.

\- Hashing the framebuffer turns "the renderer is correct" into an automated,

&#x20; repeatable test — and comparing hashes across submission orders is a direct

&#x20; demonstration of what a z-buffer buys you.

\- This renderer is fragment-bound, and 20% of its shading work is wasted. Both facts

&#x20; are measured, not assumed.

