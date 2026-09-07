# Software Rasterizer — Day 3 Log

**Project:** `softrast` — CPU-only software rasterizer in C++, no GPU API
**Sprint item:** 1 of 6 — weeks 1–2
**Repo:** https://github.com/Vutukuribanurohit02/softrast
**Commit:** Day 3 — overdraw heatmap, depth prepass, prepass/naive comparison

---

## 1. Goal for the day

Day 2 measured that 20% of shading work was thrown away by the depth test. Day 3
does two things about it:

1. **Show where the waste is** — a false-colour overdraw heatmap.
2. **Try to eliminate it** — a depth prepass, and measure honestly whether it helps.

The second one produced a negative result, which is the more valuable outcome.

---

## 2. Environment change: Windows → WSL2

Between day 2 and day 3 the machine was reinstalled. Worth recording what that cost
and what it did not.

**What survived:** everything. All source, logs, images and history were on GitHub.
Ten commits, working tree clean, nothing to recover.

**What was lost:** the toolchain only — a download, not work.

**What blocked the rebuild:** a fresh Windows install enables **Smart App Control**
by default, which enforces through WDAC/Device Guard and refuses to run unsigned
executables. A freshly compiled `main.exe` is exactly that, so every binary produced
by this project was blocked:

```
'C:\dev\softrast\main.exe' was blocked by your organization's Device Guard policy.
```

Smart App Control cannot be re-enabled once turned off without reinstalling Windows,
so disabling it is a permanent decision.

**Resolution: moved development to WSL2 (Ubuntu).** Linux binaries are not subject to
Device Guard. The toolchain is now:

| | Before | After |
|---|---|---|
| Compiler | MSVC 19.51 (`cl`) | GCC (`g++`) |
| Shell | x64 Native Tools Prompt | Ubuntu under WSL2 |
| Editor | VS Code (Windows) | VS Code + WSL extension |
| Image conversion | `mspaint` | ImageMagick (`magick`) |

Build command is now:

```
g++ -O2 -std=c++17 -o raster main.cpp
./raster
```

### An unplanned portability result

The first WSL run produced counters **identical to MSVC, to the last integer**:
498,483 bbox pixels; 248,000 fragments; 49,532 depth failures; 198,468 written;
173,468 covered.

Different compiler, different OS, different standard library, same output bit for
bit. That is evidence of no undefined behaviour and no platform-dependent arithmetic
in the renderer. Worth stating in the README as a portability claim.

Timing differed, though: **1.366 ms under GCC vs 1.888 ms under MSVC** on the same
source — GCC's optimiser is roughly 27% faster here. A small but real data point
about codegen.

---

## 3. What was added

### RenderState

Rendering behaviour is now parameterised rather than hard-coded:

```cpp
enum class DepthFunc { Less, Equal };

struct RenderState {
    bool      writeColor = true;
    bool      writeDepth = true;
    bool      shade      = true;   // false = depth-only pass
    DepthFunc func       = DepthFunc::Less;
};
```

These four fields are a miniature version of the depth/raster state block in a real
graphics pipeline. `glDepthMask`, `glDepthFunc` and colour write masks are the same
knobs.

### Overdraw heatmap

A `std::vector<uint32_t>` counts how many times each pixel was shaded, written out as
false colour:

| Shades | Colour |
|---|---|
| 0 | black |
| 1 | blue |
| 2 | green |
| 3 | yellow |
| 4+ | red |

Turns an average figure into a map — it shows *where* the redundant work happens,
not just how much.

### Depth prepass

Two passes over the same geometry:

**Pass 1** — depth only. `shade = false`, `writeColor = false`, `writeDepth = true`,
`func = Less`. No shading cost at all; this pass exists purely to establish the
nearest depth at every pixel.

**Pass 2** — shade the survivors. `writeDepth = false`, `func = Equal`. A fragment
shades only if its interpolated z matches what pass 1 left behind (within 1e-6), so
by construction each visible pixel is shaded exactly once.

This is what a depth prepass does in a real engine, and it is the software analogue
of hardware early-Z.

### Correctness guard

The prepass must be **pixel-identical** to the naive render, verified by FNV-1a hash
comparison. Without that check it is not an optimisation, just a different image.
It passes.

---
![Overdraw heatmap: blue = shaded once, green = twice](images/day3-overdraw.png)
## 4. Results

```
[test] coverage vs shoelace area: 120420 vs 120400  (0.017% error)  PASS
[test] prepass image identical to naive: PASS
[test] draw-order independence (3 permutations): PASS
```

| | Naive | Depth prepass |
|---|---|---|
| Time | **1.857 ms** | **3.917 ms** |
| Triangles submitted | 3 | 6 |
| Fragments inside | 248,000 | 496,000 |
| Fragments **shaded** | 198,468 | **173,468** |
| Depth tests failed | 49,532 | 124,064 |
| Pixels covered | 173,468 | 173,468 |
| Overdraw | 1.14× | **1.00×** |
| Peak overdraw | 2× | **1×** |

Shading work eliminated: **12.6%**
Wall-clock cost: **2.1× slower**

---

## 5. The result: it works, and it is slower

The prepass does exactly what it was designed to do. Overdraw is 1.00× and peak
overdraw is 1×, meaning every visible pixel was shaded once and no pixel twice —
provably minimal shading. The image is bit-identical to the naive render.

And it more than doubled the runtime.

**Why.** The prepass eliminated 25,000 shaded fragments and paid for it by running
the geometry twice: 496,000 fragments tested instead of 248,000. This renderer's
fragment shader is three multiply-adds. The rasterization surrounding it — edge
functions, barycentric divides, bounds arithmetic — costs far more than the shading
it saves.

### Break-even

The extra pass costs 2.06 ms to save 25,000 shaded fragments. For the prepass to pay
for itself, shading would have to cost more than:

```
2.06 ms / 25,000 fragments ≈ 82 ns per fragment  (~250 cycles at 3 GHz)
```

Three multiply-adds is on the order of a few cycles. A textured, lit, normal-mapped
shader with dependent texture fetches and cache misses comfortably exceeds 250
cycles. **That is the whole rule:** a depth prepass pays off when shading is
expensive relative to geometry, and loses when it is not.

This is exactly why real engines apply prepasses selectively — expensive materials
yes, cheap ones no — and why hardware solves it differently. **Early-Z** performs the
same rejection *inside* the pipeline, before the fragment shader runs, without paying
for a second geometry pass at all. Tile-based architectures like Mali go further
still, resolving visibility per tile in on-chip memory so that redundant shading and
the memory traffic it causes are avoided together.

A negative result with a break-even number attached is more useful than a win would
have been. It shows the tradeoff was measured rather than assumed.

---

## 6. Note on counter semantics

Day 2 reported overdraw 1.43×; day 3 reports 1.14× on the same scene. The renderer
did not change — the counter definition did.

- **Day 2:** `fragsShaded` counted every fragment that entered shading, including
  those later discarded by the depth test.
- **Day 3:** shading is counted only after the depth test passes, so `fragsShaded`
  counts work that actually contributed.

Day 3's definition is the more useful one for comparing against the prepass, since
both paths then measure the same thing. Recording the change explicitly so the two
logs do not look inconsistent.

---

## 7. Sprint calendar

| Days | Work | Status |
|---|---|---|
| 1 | Framebuffer, BMP output, first triangle | Done |
| 2 | Z-buffer, depth interpolation, overdraw counters | Done |
| 3 | Overdraw heatmap, depth prepass, break-even analysis | Done |
| 4–5 | Backface culling, triangle clipping, tiled traversal vs bbox | |
| 6–8 | OBJ loader, model-view-projection, perspective-correct interpolation | |
| 9–11 | Texture mapping, basic lighting, near-plane clipping | |
| 12–14 | README, demo images, counter tables, write-up | |

**Note for day 6:** the depth interpolation on line `z = l0*a.z + l1*b.z + l2*c.z` is
exact only because there is no perspective projection yet. Once a projection matrix
is introduced this becomes wrong, and interpolation must move into `1/w` space. That
is the perspective-correct interpolation task.

**Candidate for days 4–5:** tiled traversal. Raster efficiency has been ~49% for
three days running — half of every bounding box tested is outside the triangle.
Binning triangles into 16×16 or 32×32 tiles and rejecting whole tiles against the
edge functions should raise that number measurably, and it is the same structural
idea a tile-based GPU is built around. Good material for sprint item 3.

---

## 8. Concepts learned today

- Depth and raster behaviour belong in a state block, not hard-coded branches — the
  same design real graphics APIs expose.
- A depth prepass achieves provably minimal shading (1.00× overdraw) by rendering
  depth first and shading only exact matches.
- An optimisation that changes the image is a bug; hash-comparing against the
  baseline is what separates the two.
- Optimisations have break-even points, and the break-even can be computed rather
  than guessed: here, ~82 ns of shading cost per fragment.
- Early-Z exists because a prepass costs a whole extra geometry pass in software but
  costs nothing extra in hardware.
- Portability is testable: identical counters under two compilers on two operating
  systems is evidence of well-defined behaviour.
- Committing at the end of every session meant a full machine reinstall cost a
  download, not two days of work.