// main.cpp - software rasterizer, day 1
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

struct Vec2 { float x, y; };
struct Color { uint8_t r, g, b; };

struct Stats {
    long long trisSubmitted = 0, trisRasterized = 0;
    long long bboxPixels = 0, fragsShaded = 0;
    void report() const {
        std::printf("triangles submitted : %lld\n", trisSubmitted);
        std::printf("triangles rasterized: %lld\n", trisRasterized);
        std::printf("bbox pixels tested  : %lld\n", bboxPixels);
        std::printf("fragments shaded    : %lld\n", fragsShaded);
        if (bboxPixels)
            std::printf("efficiency          : %.1f%%\n",
                        100.0 * double(fragsShaded) / double(bboxPixels));
    }
};

struct Framebuffer {
    int w, h;
    std::vector<Color> px;
    Framebuffer(int W, int H) : w(W), h(H), px(size_t(W) * H, Color{0,0,0}) {}
    void clear(Color c) { std::fill(px.begin(), px.end(), c); }
    void set(int x, int y, Color c) { px[size_t(y) * w + x] = c; }

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
        for (int y = h - 1; y >= 0; --y) {           // BMP is bottom-up
            for (int x = 0; x < w; ++x) {
                const Color& c = px[size_t(y) * w + x];
                row[size_t(x)*3+0] = c.b;            // and BGR
                row[size_t(x)*3+1] = c.g;
                row[size_t(x)*3+2] = c.r;
            }
            std::fwrite(row.data(), 1, row.size(), f);
        }
        std::fclose(f);
        return true;
    }
};

// Signed area: positive on one side of edge ab, negative on the other.
static inline float edge(const Vec2& a, const Vec2& b, const Vec2& p) {
    return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
}

void drawTriangle(Framebuffer& fb, Vec2 a, Vec2 b, Vec2 c,
                  Color ca, Color cb, Color cc, Stats& st) {
    st.trisSubmitted++;
    float area = edge(a, b, c);
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
            Vec2 p{ float(x) + 0.5f, float(y) + 0.5f };
            float w0 = edge(b, c, p), w1 = edge(c, a, p), w2 = edge(a, b, p);
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float l0 = w0 * inv, l1 = w1 * inv, l2 = w2 * inv;
            fb.set(x, y, Color{
                uint8_t(l0*ca.r + l1*cb.r + l2*cc.r),
                uint8_t(l0*ca.g + l1*cb.g + l2*cc.g),
                uint8_t(l0*ca.b + l1*cb.b + l2*cc.b) });
            st.fragsShaded++;
        }
    }
}

int main() {
    const int W = 800, H = 600;
    Framebuffer fb(W, H);
    fb.clear(Color{12, 14, 18});
    Stats st;

    auto t0 = std::chrono::high_resolution_clock::now();
    drawTriangle(fb, Vec2{400,80}, Vec2{120,500}, Vec2{680,520},
                 Color{220,60,60}, Color{60,220,100}, Color{70,120,240}, st);
    auto t1 = std::chrono::high_resolution_clock::now();

    if (!fb.writeBMP("out.bmp")) { std::printf("write failed\n"); return 1; }
    std::printf("rendered %dx%d in %.3f ms\n", W, H,
                std::chrono::duration<double, std::milli>(t1 - t0).count());
    st.report();
    return 0;
}