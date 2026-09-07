// main.cpp - software rasterizer, day 3: overdraw heatmap + depth prepass
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
    long long bboxPixels = 0, fragsInside = 0, fragsShaded = 0;
    long long depthTestsFailed = 0, pixelsCovered = 0;
};

enum class DepthFunc { Less, Equal };

struct RenderState {
    bool      writeColor = true;
    bool      writeDepth = true;
    bool      shade      = true;   // false = depth-only pass
    DepthFunc func       = DepthFunc::Less;
};

struct Framebuffer {
    int w, h;
    std::vector<Color>    color;
    std::vector<float>    depth;
    std::vector<uint8_t>  covered;
    std::vector<uint32_t> overdraw;   // times each pixel was shaded

    Framebuffer(int W, int H) : w(W), h(H),
        color(size_t(W)*H, Color{0,0,0}),
        depth(size_t(W)*H, std::numeric_limits<float>::infinity()),
        covered(size_t(W)*H, 0),
        overdraw(size_t(W)*H, 0) {}

    void clear(Color c) {
        std::fill(color.begin(), color.end(), c);
        std::fill(depth.begin(), depth.end(), std::numeric_limits<float>::infinity());
        std::fill(covered.begin(), covered.end(), 0);
        std::fill(overdraw.begin(), overdraw.end(), 0);
    }

    uint64_t checksum() const {
        uint64_t hv = 1469598103934665603ULL;
        for (const Color& c : color)
            for (uint8_t b : {c.r, c.g, c.b}) { hv ^= b; hv *= 1099511628211ULL; }
        return hv;
    }

    bool writeBMP(const char* path) const {
        int rowBytes = w * 3, pad = (4 - (rowBytes % 4)) % 4;
        int imgBytes = (rowBytes + pad) * h, fileSize = 54 + imgBytes;
        FILE* f = std::fopen(path, "wb");
        if (!f) return false;
        uint8_t hd[54] = {};
        hd[0]='B'; hd[1]='M';
        auto p32 = [&](int o, uint32_t v){
            hd[o]=uint8_t(v); hd[o+1]=uint8_t(v>>8);
            hd[o+2]=uint8_t(v>>16); hd[o+3]=uint8_t(v>>24); };
        p32(2,fileSize); p32(10,54); p32(14,40);
        p32(18,uint32_t(w)); p32(22,uint32_t(h));
        hd[26]=1; hd[28]=24; p32(34,uint32_t(imgBytes));
        std::fwrite(hd,1,54,f);
        std::vector<uint8_t> row(size_t(rowBytes+pad), 0);
        for (int y = h-1; y >= 0; --y) {
            for (int x = 0; x < w; ++x) {
                const Color& c = color[size_t(y)*w+x];
                row[size_t(x)*3+0]=c.b; row[size_t(x)*3+1]=c.g; row[size_t(x)*3+2]=c.r;
            }
            std::fwrite(row.data(),1,row.size(),f);
        }
        std::fclose(f);
        return true;
    }

    bool writeDepthBMP(const char* path) const {
        float lo = std::numeric_limits<float>::infinity(), hi = -lo;
        for (float d : depth) if (std::isfinite(d)) { lo=std::min(lo,d); hi=std::max(hi,d); }
        Framebuffer vis(w,h);
        float span = (hi>lo) ? (hi-lo) : 1.0f;
        for (size_t i = 0; i < depth.size(); ++i) {
            if (!std::isfinite(depth[i])) continue;
            uint8_t g = uint8_t(255.0f * (1.0f - (depth[i]-lo)/span));
            vis.color[i] = Color{g,g,g};
        }
        return vis.writeBMP(path);
    }

    // 0 = black, 1 = blue, 2 = green, 3 = yellow, 4+ = red.
    bool writeOverdrawBMP(const char* path) const {
        static const Color ramp[5] = {
            {12,14,18}, {40,90,200}, {60,190,110}, {235,200,60}, {225,60,50} };
        Framebuffer vis(w,h);
        for (size_t i = 0; i < overdraw.size(); ++i)
            vis.color[i] = ramp[overdraw[i] > 4 ? 4 : overdraw[i]];
        return vis.writeBMP(path);
    }

    uint32_t maxOverdraw() const {
        uint32_t m = 0;
        for (uint32_t v : overdraw) m = std::max(m, v);
        return m;
    }
};

static inline float edge(const Vec3& a, const Vec3& b, float px, float py) {
    return (px-a.x)*(b.y-a.y) - (py-a.y)*(b.x-a.x);
}

void drawTriangle(Framebuffer& fb, Vec3 a, Vec3 b, Vec3 c,
                  Color ca, Color cb, Color cc,
                  Stats& st, const RenderState& rs) {
    st.trisSubmitted++;
    float area = edge(a, b, c.x, c.y);
    if (area == 0.0f) return;
    if (area < 0.0f) { std::swap(b,c); std::swap(cb,cc); area = -area; }
    st.trisRasterized++;

    int minX = std::max(0, int(std::floor(std::min({a.x,b.x,c.x}))));
    int minY = std::max(0, int(std::floor(std::min({a.y,b.y,c.y}))));
    int maxX = std::min(fb.w-1, int(std::ceil(std::max({a.x,b.x,c.x}))));
    int maxY = std::min(fb.h-1, int(std::ceil(std::max({a.y,b.y,c.y}))));
    float inv = 1.0f / area;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            st.bboxPixels++;
            float px = float(x)+0.5f, py = float(y)+0.5f;
            float w0 = edge(b,c,px,py), w1 = edge(c,a,px,py), w2 = edge(a,b,px,py);
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;

            st.fragsInside++;
            float l0 = w0*inv, l1 = w1*inv, l2 = w2*inv;
            size_t i = size_t(y)*fb.w + x;
            if (!fb.covered[i]) { fb.covered[i] = 1; st.pixelsCovered++; }

            float z = l0*a.z + l1*b.z + l2*c.z;
            bool pass = (rs.func == DepthFunc::Less)
                      ? (z < fb.depth[i])
                      : (std::fabs(z - fb.depth[i]) < 1e-6f);
            if (!pass) { st.depthTestsFailed++; continue; }

            if (rs.writeDepth) fb.depth[i] = z;

            if (rs.shade) {                      // shading happens after the test
                st.fragsShaded++;
                fb.overdraw[i]++;
                if (rs.writeColor)
                    fb.color[i] = Color{
                        uint8_t(l0*ca.r + l1*cb.r + l2*cc.r),
                        uint8_t(l0*ca.g + l1*cb.g + l2*cc.g),
                        uint8_t(l0*ca.b + l1*cb.b + l2*cc.b) };
            }
        }
    }
}

static const Vec3 TRI[3][3] = {
    { {150,100,0.10f}, {150,500,0.10f}, {650,300,0.90f} },
    { {650,100,0.10f}, {650,500,0.10f}, {150,300,0.90f} },
    { {300, 80,0.50f}, {500, 80,0.50f}, {400,560,0.50f} },
};
static const Color COL[3][3] = {
    { {230,70,70},  {230,70,70},  {120,20,20} },
    { {70,200,110}, {70,200,110}, {20,90,45}  },
    { {80,130,240}, {80,130,240}, {30,55,120} },
};

void submitAll(Framebuffer& fb, Stats& st, const RenderState& rs, const int order[3]) {
    for (int k = 0; k < 3; ++k) {
        int t = order[k];
        drawTriangle(fb, TRI[t][0], TRI[t][1], TRI[t][2],
                         COL[t][0], COL[t][1], COL[t][2], st, rs);
    }
}

void renderNaive(Framebuffer& fb, Stats& st, const int order[3]) {
    fb.clear(Color{12,14,18});
    RenderState rs;
    submitAll(fb, st, rs, order);
}

void renderPrepass(Framebuffer& fb, Stats& st, const int order[3]) {
    fb.clear(Color{12,14,18});

    RenderState pre;
    pre.shade = false; pre.writeColor = false; pre.writeDepth = true;
    pre.func = DepthFunc::Less;
    submitAll(fb, st, pre, order);        // pass 1: depth only

    RenderState shading;
    shading.shade = true; shading.writeColor = true; shading.writeDepth = false;
    shading.func = DepthFunc::Equal;      // only exact survivors shade
    submitAll(fb, st, shading, order);    // pass 2
}

void report(const char* label, const Stats& st, const Framebuffer& fb, double ms) {
    std::printf("\n--- %s ---\n", label);
    std::printf("  time                : %.3f ms\n", ms);
    std::printf("  triangles submitted : %lld\n", st.trisSubmitted);
    std::printf("  fragments inside    : %lld\n", st.fragsInside);
    std::printf("  fragments SHADED    : %lld\n", st.fragsShaded);
    std::printf("  depth tests failed  : %lld\n", st.depthTestsFailed);
    std::printf("  pixels covered      : %lld\n", st.pixelsCovered);
    if (st.pixelsCovered)
        std::printf("  overdraw            : %.2fx\n",
                    double(st.fragsShaded)/double(st.pixelsCovered));
    std::printf("  peak overdraw       : %ux\n", fb.maxOverdraw());
}

void validateCoverage() {
    Framebuffer fb(800,600); fb.clear(Color{0,0,0});
    Stats st; RenderState rs;
    Vec3 a{400,80,0.5f}, b{120,500,0.5f}, c{680,520,0.5f};
    drawTriangle(fb,a,b,c,Color{255,0,0},Color{0,255,0},Color{0,0,255},st,rs);
    double area = 0.5*std::fabs(double(a.x)*(b.y-c.y)+double(b.x)*(c.y-a.y)+double(c.x)*(a.y-b.y));
    double err = 100.0*std::fabs(double(st.fragsShaded)-area)/area;
    std::printf("[test] coverage vs shoelace area: %lld vs %.0f  (%.3f%% error)  %s\n",
                st.fragsShaded, area, err, err < 0.1 ? "PASS" : "FAIL");
}

int main() {
    const int W = 800, H = 600;
    validateCoverage();

    int fwd[3] = {0,1,2}, rev[3] = {2,1,0}, mid[3] = {1,2,0};

    Framebuffer fbN(W,H); Stats stN;
    auto t0 = std::chrono::high_resolution_clock::now();
    renderNaive(fbN, stN, fwd);
    auto t1 = std::chrono::high_resolution_clock::now();
    double msN = std::chrono::duration<double,std::milli>(t1-t0).count();

    Framebuffer fbP(W,H); Stats stP;
    auto t2 = std::chrono::high_resolution_clock::now();
    renderPrepass(fbP, stP, fwd);
    auto t3 = std::chrono::high_resolution_clock::now();
    double msP = std::chrono::duration<double,std::milli>(t3-t2).count();

    bool same = (fbN.checksum() == fbP.checksum());
    std::printf("[test] prepass image identical to naive: %s\n", same ? "PASS" : "FAIL");

    Stats sink;
    Framebuffer f2(W,H); renderNaive(f2, sink, rev);
    Framebuffer f3(W,H); renderNaive(f3, sink, mid);
    bool orderOk = (f2.checksum()==fbN.checksum()) && (f3.checksum()==fbN.checksum());
    std::printf("[test] draw-order independence (3 permutations): %s\n",
                orderOk ? "PASS" : "FAIL");

    report("naive: shade then depth-test", stN, fbN, msN);
    report("depth prepass: depth then shade", stP, fbP, msP);

    double saved = 100.0*(1.0 - double(stP.fragsShaded)/double(stN.fragsShaded));
    std::printf("\nshading work eliminated by prepass: %.1f%%\n", saved);
    std::printf("geometry cost: %lld -> %lld triangles submitted\n",
                stN.trisSubmitted, stP.trisSubmitted);

    fbN.writeBMP("out.bmp");
    fbN.writeDepthBMP("depth.bmp");
    fbN.writeOverdrawBMP("overdraw.bmp");
    std::printf("\nwrote out.bmp, depth.bmp, overdraw.bmp\n");
    return (same && orderOk) ? 0 : 1;
}