// main.cpp - software rasterizer, day 2: z-buffer
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

struct Vec3  { float x, y, z; };
struct Color { uint8_t r, g, b; };

struct Stats {
    long long trisSubmitted = 0, trisRasterized = 0;
    long long bboxPixels = 0, fragsShaded = 0;
    long long depthTestsFailed = 0, pixelsWritten = 0, pixelsCovered = 0;

    void report() const {
        std::printf("  triangles submitted : %lld\n", trisSubmitted);
        std::printf("  triangles rasterized: %lld\n", trisRasterized);
        std::printf("  bbox pixels tested  : %lld\n", bboxPixels);
        std::printf("  fragments shaded    : %lld\n", fragsShaded);
        std::printf("  depth tests failed  : %lld\n", depthTestsFailed);
        std::printf("  pixels written      : %lld\n", pixelsWritten);
        std::printf("  pixels covered      : %lld\n", pixelsCovered);
        if (bboxPixels)
            std::printf("  raster efficiency   : %.1f%%\n",
                        100.0 * double(fragsShaded) / double(bboxPixels));
        if (pixelsCovered)
            std::printf("  overdraw            : %.2fx\n",
                        double(fragsShaded) / double(pixelsCovered));
        if (fragsShaded)
            std::printf("  shading wasted      : %.1f%%\n",
                        100.0 * double(depthTestsFailed) / double(fragsShaded));
    }
};

struct Framebuffer {
    int w, h;
    std::vector<Color>   color;
    std::vector<float>   depth;    // smaller z = nearer
    std::vector<uint8_t> covered;  // touched at least once?

    Framebuffer(int W, int H) : w(W), h(H),
        color(size_t(W) * H, Color{0,0,0}),
        depth(size_t(W) * H, std::numeric_limits<float>::infinity()),
        covered(size_t(W) * H, 0) {}

    void clear(Color c) {
        std::fill(color.begin(), color.end(), c);
        std::fill(depth.begin(), depth.end(), std::numeric_limits<float>::infinity());
        std::fill(covered.begin(), covered.end(), 0);
    }

    // FNV-1a over the colour buffer, for comparing two renders bit-for-bit.
    uint64_t checksum() const {
        uint64_t hv = 1469598103934665603ULL;
        for (const Color& c : color) {
            for (uint8_t b : {c.r, c.g, c.b}) {
                hv ^= b;
                hv *= 1099511628211ULL;
            }
        }
        return hv;
    }

    bool writeBMP(const char* path) const {
        int rowBytes = w * 3, pad = (4 - (rowBytes % 4)) % 4;
        int imgBytes = (rowBytes + pad) * h, fileSize = 54 + imgBytes;
        FILE* f = nullptr;
#ifdef _MSC_VER
        fopen_s(&f, path, "wb");
#else
        f = std::fopen(path, "wb");
#endif
        if (!f) return false;
        uint8_t hd[54] = {};
        hd[0] = 'B'; hd[1] = 'M';
        auto p32 = [&](int o, uint32_t v) {
            hd[o] = uint8_t(v); hd[o+1] = uint8_t(v >> 8);
            hd[o+2] = uint8_t(v >> 16); hd[o+3] = uint8_t(v >> 24);
        };
        p32(2, fileSize); p32(10, 54); p32(14, 40);
        p32(18, uint32_t(w)); p32(22, uint32_t(h));
        hd[26] = 1; hd[28] = 24;
        p32(34, uint32_t(imgBytes));
        std::fwrite(hd, 1, 54, f);
        std::vector<uint8_t> row(size_t(rowBytes + pad), 0);
        for (int y = h - 1; y >= 0; --y) {
            for (int x = 0; x < w; ++x) {
                const Color& c = color[size_t(y) * w + x];
                row[size_t(x)*3+0] = c.b;
                row[size_t(x)*3+1] = c.g;
                row[size_t(x)*3+2] = c.r;
            }
            std::fwrite(row.data(), 1, row.size(), f);
        }
        std::fclose(f);
        return true;
    }

    // Depth as greyscale: near = bright, untouched = black.
    bool writeDepthBMP(const char* path) const {
        float lo = std::numeric_limits<float>::infinity(), hi = -lo;
        for (float d : depth)
            if (std::isfinite(d)) { lo = std::min(lo, d); hi = std::max(hi, d); }
        Framebuffer vis(w, h);
        float span = (hi > lo) ? (hi - lo) : 1.0f;
        for (size_t i = 0; i < depth.size(); ++i) {
            if (!std::isfinite(depth[i])) continue;
            uint8_t g = uint8_t(255.0f * (1.0f - (depth[i] - lo) / span));
            vis.color[i] = Color{g, g, g};
        }
        return vis.writeBMP(path);
    }
};

static inline float edge(const Vec3& a, const Vec3& b, float px, float py) {
    return (px - a.x) * (b.y - a.y) - (py - a.y) * (b.x - a.x);
}

void drawTriangle(Framebuffer& fb, Vec3 a, Vec3 b, Vec3 c,
                  Color ca, Color cb, Color cc, Stats& st) {
    st.trisSubmitted++;
    float area = edge(a, b, c.x, c.y);
    if (area == 0.0f) return;
    if (area < 0.0f) { std::swap(b, c); std::swap(cb, cc); area = -area; }
    st.trisRasterized++;

    int minX = std::max(0, int(std::floor(std::min({a.x, b.x, c.x}))));
    int minY = std::max(0, int(std::floor(std::min({a.y, b.y, c.y}))));
    int maxX = std::min(fb.w - 1, int(std::ceil(std::max({a.x, b.x, c.x}))));
    int maxY = std::min(fb.h - 1, int(std::ceil(std::max({a.y, b.y, c.y}))));
    float inv = 1.0f / area;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            st.bboxPixels++;
            float px = float(x) + 0.5f, py = float(y) + 0.5f;
            float w0 = edge(b, c, px, py);
            float w1 = edge(c, a, px, py);
            float w2 = edge(a, b, px, py);
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;

            st.fragsShaded++;
            float l0 = w0 * inv, l1 = w1 * inv, l2 = w2 * inv;
            size_t i = size_t(y) * fb.w + x;

            if (!fb.covered[i]) { fb.covered[i] = 1; st.pixelsCovered++; }

            // No projection yet, so screen-space linear interpolation of z is exact.
            float z = l0 * a.z + l1 * b.z + l2 * c.z;
            if (z >= fb.depth[i]) { st.depthTestsFailed++; continue; }

            fb.depth[i] = z;
            fb.color[i] = Color{
                uint8_t(l0*ca.r + l1*cb.r + l2*cc.r),
                uint8_t(l0*ca.g + l1*cb.g + l2*cc.g),
                uint8_t(l0*ca.b + l1*cb.b + l2*cc.b) };
            st.pixelsWritten++;
        }
    }
}

// ---- scene: three mutually interpenetrating triangles -----------------------
// A and B are wedges tilted opposite ways in z, so each is nearer on its own
// side. C is a flat mid-depth blade cutting through both. No draw order can
// resolve this correctly - that is the point.

static const Vec3 A[3] = { {150,100,0.10f}, {150,500,0.10f}, {650,300,0.90f} };
static const Vec3 B[3] = { {650,100,0.10f}, {650,500,0.10f}, {150,300,0.90f} };
static const Vec3 C[3] = { {300, 80,0.50f}, {500, 80,0.50f}, {400,560,0.50f} };

static const Color CA[3] = { {230,70,70},  {230,70,70},  {120,20,20}  };
static const Color CB[3] = { {70,200,110}, {70,200,110}, {20,90,45}   };
static const Color CC[3] = { {80,130,240}, {80,130,240}, {30,55,120}  };

// order is a permutation of {0,1,2}
void renderScene(Framebuffer& fb, Stats& st, const int order[3]) {
    fb.clear(Color{12, 14, 18});
    for (int k = 0; k < 3; ++k) {
        switch (order[k]) {
        case 0: drawTriangle(fb, A[0],A[1],A[2], CA[0],CA[1],CA[2], st); break;
        case 1: drawTriangle(fb, B[0],B[1],B[2], CB[0],CB[1],CB[2], st); break;
        case 2: drawTriangle(fb, C[0],C[1],C[2], CC[0],CC[1],CC[2], st); break;
        }
    }
}

// ---- day 1 regression test: coverage vs closed-form area --------------------
void validateCoverage() {
    Framebuffer fb(800, 600);
    fb.clear(Color{0,0,0});
    Stats st;
    Vec3 a{400,80,0.5f}, b{120,500,0.5f}, c{680,520,0.5f};
    drawTriangle(fb, a, b, c, Color{255,0,0}, Color{0,255,0}, Color{0,0,255}, st);

    double shoelace = 0.5 * std::fabs(
        double(a.x)*(b.y - c.y) + double(b.x)*(c.y - a.y) + double(c.x)*(a.y - b.y));
    double err = 100.0 * std::fabs(double(st.fragsShaded) - shoelace) / shoelace;

    std::printf("[test] coverage vs shoelace area: %lld vs %.0f  (%.3f%% error)  %s\n",
                st.fragsShaded, shoelace, err, err < 0.1 ? "PASS" : "FAIL");
}

int main() {
    validateCoverage();

    const int W = 800, H = 600;
    Framebuffer fb(W, H);
    Stats st;

    int fwd[3] = {0, 1, 2};
    auto t0 = std::chrono::high_resolution_clock::now();
    renderScene(fb, st, fwd);
    auto t1 = std::chrono::high_resolution_clock::now();
    uint64_t h1 = fb.checksum();

    // Same scene, two other submission orders. A z-buffer must give identical
    // pixels; a painter's-algorithm renderer would not.
    Stats sink;
    int rev[3] = {2, 1, 0}, mid[3] = {1, 2, 0};
    Framebuffer fb2(W, H); renderScene(fb2, sink, rev);
    Framebuffer fb3(W, H); renderScene(fb3, sink, mid);
    bool orderOk = (fb2.checksum() == h1) && (fb3.checksum() == h1);
    std::printf("[test] draw-order independence (3 permutations): %s\n",
                orderOk ? "PASS" : "FAIL");

    if (!fb.writeBMP("out.bmp"))        { std::printf("write failed\n"); return 1; }
    if (!fb.writeDepthBMP("depth.bmp")) { std::printf("write failed\n"); return 1; }

    std::printf("\nrendered %dx%d, 3 triangles, in %.3f ms\n", W, H,
                std::chrono::duration<double, std::milli>(t1 - t0).count());
    st.report();
    std::printf("\nwrote out.bmp and depth.bmp\n");
    return (orderOk ? 0 : 1);
}