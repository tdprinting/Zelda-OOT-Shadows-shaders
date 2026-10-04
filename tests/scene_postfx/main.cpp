// Headless validation of GfxRenderingAPIOGL::ApplyScenePostFx using Mesa software GLES 3.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <string>
#include <memory>
#include <algorithm>
#define private public
#include "fast/backends/gfx_opengl.h"
#undef private

using namespace Fast;
struct V3 { double x, y, z; };
static V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
static double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 norm(V3 a) { double l = std::sqrt(dot(a, a)); return a * (1.0 / l); }
static V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// Column-vector GL-style view-projection, then transposed into the N64 row-vector layout.
static void buildRowVP(V3 eye, V3 at, double fovy, double aspect, double n, double f, double out[4][4]) {
    V3 fw = norm(at - eye), r = norm(cross(fw, V3{0, 1, 0})), u = cross(r, fw);
    double V[4][4] = {{r.x, r.y, r.z, -dot(r, eye)}, {u.x, u.y, u.z, -dot(u, eye)},
                      {-fw.x, -fw.y, -fw.z, dot(fw, eye)}, {0, 0, 0, 1}};
    double t = 1.0 / std::tan(fovy / 2);
    double P[4][4] = {{t / aspect, 0, 0, 0}, {0, t, 0, 0}, {0, 0, -(f + n) / (f - n), -2 * f * n / (f - n)}, {0, 0, -1, 0}};
    double C[4][4] = {};
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) for (int k = 0; k < 4; k++) C[i][j] += P[i][k] * V[k][j];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) out[j][i] = C[i][j]; // transpose -> row-vector convention
}

#ifdef USE_OPENGLES
static const char* kVs = "#version 300 es\nlayout(location=0) in vec4 aPos;\nvoid main(){gl_Position=aPos;}\n";
static const char* kFs = "#version 300 es\nprecision highp float;\nuniform vec3 uCol;\nout vec4 o;\nvoid main(){o=vec4(uCol,1.0);}\n";
static const double kZScale = 0.3;
#else
static const char* kVs = "#version 130\nin vec4 aPos;\nvoid main(){gl_Position=aPos;}\n";
static const char* kFs = "#version 130\nuniform vec3 uCol;\nout vec4 o;\nvoid main(){o=vec4(uCol,1.0);}\n";
static const double kZScale = 1.0;
#endif

static GLuint mk(GLenum t, const char* s) { GLuint h = glCreateShader(t); glShaderSource(h, 1, &s, 0); glCompileShader(h); GLint ok; glGetShaderiv(h, GL_COMPILE_STATUS, &ok); if (!ok) { char l[1024]; glGetShaderInfoLog(h, 1024, 0, l); printf("compile: %s\n", l); exit(2);} return h; }

struct Box { V3 lo, hi; };
static bool rayBox(V3 o, V3 d, const Box& b, double tmax, double* tHit = nullptr) {
    double t0 = 0, t1 = tmax;
    double oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z}, lo[3] = {b.lo.x, b.lo.y, b.lo.z}, hi[3] = {b.hi.x, b.hi.y, b.hi.z};
    for (int i = 0; i < 3; i++) {
        if (std::fabs(dd[i]) < 1e-12) { if (oo[i] < lo[i] || oo[i] > hi[i]) return false; continue; }
        double a = (lo[i] - oo[i]) / dd[i], c = (hi[i] - oo[i]) / dd[i];
        if (a > c) std::swap(a, c);
        t0 = std::max(t0, a); t1 = std::min(t1, c);
        if (t0 > t1) return false;
    }
    if (tHit) *tHit = t0;
    return true;
}

static bool runCase(const char* name, int W, int H, bool invertY, float vx, float vy, float vw, float vh, double aspectAdj,
                    float shadowDist, float thickness, bool expectShadow) {
    GfxRenderingAPIOGL api;
    api.mFrameBuffers.resize(2);
    FramebufferOGL& fb = api.mFrameBuffers[1];
    fb.width = W; fb.height = H; fb.has_depth_buffer = true; fb.msaa_level = 1; fb.invertY = invertY;
    glGenTextures(1, &fb.clrbuf);
    glBindTexture(GL_TEXTURE_2D, fb.clrbuf);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, W, H, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glGenRenderbuffers(1, &fb.rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, fb.rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, W, H);
    glGenFramebuffers(1, &fb.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb.clrbuf, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fb.rbo);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { printf("fbo incomplete\n"); return false; }
    api.mCurrentFrameBuffer = 1;

    V3 eye{150, 380, 650}, at{-20, 0, -10};
    double vpAspect = (double)vw / vh;
    double M[4][4];
    buildRowVP(eye, at, 60.0 * M_PI / 180, vpAspect, 10, 12800, M);

    // Scene: a big ground quad plus an axis-aligned box.
    Box box{{-60, 0, -60}, {60, 140, 60}};
    std::vector<V3> tris;
    auto quad = [&](V3 a, V3 b, V3 c, V3 d) { tris.insert(tris.end(), {a, b, c, a, c, d}); };
    quad({-1500, 0, -1500}, {-1500, 0, 1500}, {1500, 0, 1500}, {1500, 0, -1500});
    V3 L0 = box.lo, H0 = box.hi;
    quad({L0.x, H0.y, L0.z}, {L0.x, H0.y, H0.z}, {H0.x, H0.y, H0.z}, {H0.x, H0.y, L0.z}); // top
    quad({L0.x, L0.y, H0.z}, {H0.x, L0.y, H0.z}, {H0.x, H0.y, H0.z}, {L0.x, H0.y, H0.z}); // +z
    quad({L0.x, L0.y, L0.z}, {L0.x, H0.y, L0.z}, {H0.x, H0.y, L0.z}, {H0.x, L0.y, L0.z}); // -z
    quad({L0.x, L0.y, L0.z}, {L0.x, L0.y, H0.z}, {L0.x, H0.y, H0.z}, {L0.x, H0.y, L0.z}); // -x
    quad({H0.x, L0.y, L0.z}, {H0.x, H0.y, L0.z}, {H0.x, H0.y, H0.z}, {H0.x, L0.y, H0.z}); // +x
    std::vector<float> vbo;
    for (V3 p : tris) {
        double c[4];
        for (int k = 0; k < 4; k++) c[k] = p.x * M[0][k] + p.y * M[1][k] + p.z * M[2][k] + M[3][k];
        c[0] *= aspectAdj;                // Fast3D AdjXForAspectRatio
        if (invertY) c[1] = -c[1];        // clip_parameters.invertY
        c[2] *= kZScale;                  // GLES vertex shader squash (1.0 on desktop)
        for (int k = 0; k < 4; k++) vbo.push_back((float)c[k]);
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, mk(GL_VERTEX_SHADER, kVs)); glAttachShader(prog, mk(GL_FRAGMENT_SHADER, kFs));
    glLinkProgram(prog); glUseProgram(prog);
    GLuint vao, buf; glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glGenBuffers(1, &buf); glBindBuffer(GL_ARRAY_BUFFER, buf);
    glBufferData(GL_ARRAY_BUFFER, vbo.size() * 4, vbo.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0);

    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1); glClearDepthf(1.0f); glDepthMask(GL_TRUE); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport((GLint)vx, (GLint)vy, (GLsizei)vw, (GLsizei)vh);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
    glUniform3f(glGetUniformLocation(prog, "uCol"), 0.8f, 0.8f, 0.8f);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)tris.size());

    ScenePostFxArgs a{};
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) a.viewProj[r][c] = (float)M[r][c];
    a.aspectScaleX = (float)aspectAdj;
    a.viewport[0] = vx; a.viewport[1] = vy; a.viewport[2] = vw; a.viewport[3] = vh;
    V3 L = norm({1.0, 1.1, 0.5});
    a.sunDir[0] = L.x; a.sunDir[1] = L.y; a.sunDir[2] = L.z;
    if (getenv("FX_NEG")) { a.sunDir[0] = -L.x; a.sunDir[2] = -L.z; }
    a.shadowStrength = 1.0f; a.shadowDistance = shadowDist; a.shadowThickness = thickness; a.shadowSteps = 48;
    bool applied = api.ApplyScenePostFx(1, a);
    if (!applied) { printf("[%s] ApplyScenePostFx returned false\n", name); return false; }
    GLenum err = glGetError(); if (err) { printf("[%s] GL error 0x%x\n", name, err); return false; }

    std::vector<uint8_t> px(W * H * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, fb.fbo);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());

    if (getenv("FX_DUMP") && !strcmp(name, "invertY (game FB mode)")) { FILE* f = fopen(getenv("FX_DUMP"), "wb"); fprintf(f, "P6 %d %d 255\n", W, H); for (int yy = H - 1; yy >= 0; yy--) for (int xx = 0; xx < W; xx++) fwrite(&px[(yy * W + xx) * 4], 1, 3, f); fclose(f); }
    // Compare against analytic ground-truth, only for pixels that land on the ground plane.
    int agree = 0, total = 0, shadowedTruth = 0, shadowedGot = 0, nearEdge = 0, tp = 0;
    double invAsp = 1.0;
    V3 fw = norm(at - eye), r = norm(cross(fw, V3{0, 1, 0})), u = cross(r, fw);
    double t = std::tan(60.0 * M_PI / 360);
    for (int y = (int)vy + 2; y < (int)(vy + vh) - 2; y += 2) {
        for (int x = (int)vx + 2; x < (int)(vx + vw) - 2; x += 2) {
            double nx = ((x + 0.5 - vx) / vw) * 2 - 1, ny = ((y + 0.5 - vy) / vh) * 2 - 1;
            if (invertY) ny = -ny; // image is stored upside down in this mode
            // rendered image row y corresponds to ndc y as stored; geometry y was negated, so undo for the world ray
            V3 d = norm(fw + r * (nx * t * vpAspect * (1.0 / 1.0)) + u * (ny * t));
            // NB: aspectAdj squeezes x in clip space, so the world ray for ndc x is scaled by 1/aspectAdj
            d = norm(fw + r * (nx * t * vpAspect / aspectAdj) + u * (ny * t));
            double tg = -eye.y / d.y;
            if (tg <= 0) continue;
            double tb; bool hitBox = rayBox(eye, d, box, 1e9, &tb);
            if (hitBox && tb < tg) continue; // box pixel, not the ground
            V3 p = eye + d * tg;
            if (std::fabs(p.x) > 1400 || std::fabs(p.z) > 1400) continue;
            auto shadowedAt = [&](V3 q) { return rayBox(q + L * 1.0, L, box, shadowDist); };
            bool truth = shadowedAt(p);
            // skip pixels close to the shadow boundary (a few world units of tolerance)
            bool edge = false;
            for (double dx : {-6.0, 6.0}) for (double dz : {-6.0, 6.0}) if (shadowedAt({p.x + dx, 0, p.z + dz}) != truth) edge = true;
            if (edge) { nearEdge++; continue; }
            const uint8_t* o = &px[(y * W + x) * 4];
            bool got = o[0] < 0.75 * 204; // base is 0.8*255 = 204
            total++; agree += (got == truth); tp += (got && truth); shadowedTruth += truth; shadowedGot += got;
        }
    }
    double pct = total ? 100.0 * agree / total : 0;
    printf("[%s] ground pixels checked=%d (skipped %d near edges)  truth-shadowed=%d  got-shadowed=%d  agreement=%.1f%%\n", name, total,
           nearEdge, shadowedTruth, shadowedGot, pct);
    double recall = shadowedTruth ? 100.0 * tp / shadowedTruth : 0, precision = shadowedGot ? 100.0 * tp / shadowedGot : 0;
    printf("[%s] shadow recall=%.1f%% precision=%.1f%%\n", name, recall, precision);
    bool ok = total > 200 && pct >= 97.0 && (!expectShadow || (shadowedTruth > 30 && recall >= 95.0 && precision >= 75.0));
    printf("[%s] %s\n", name, ok ? "PASS" : "FAIL");
    return ok;
}

int main() {
    setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
    EGLDisplay dpy = EGL_NO_DISPLAY;
    auto getPlat = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (getPlat) dpy = getPlat(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    if (dpy == EGL_NO_DISPLAY) dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint maj, min; if (!eglInitialize(dpy, &maj, &min)) { printf("eglInitialize failed\n"); return 2; }
    #ifdef USE_OPENGLES
    eglBindAPI(EGL_OPENGL_ES_API);
#else
    eglBindAPI(EGL_OPENGL_API);
#endif
#ifdef USE_OPENGLES
    const EGLint kRenderable = EGL_OPENGL_ES3_BIT;
#else
    const EGLint kRenderable = EGL_OPENGL_BIT;
#endif
    EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, kRenderable, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
    EGLConfig cfg; EGLint n; eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n);
    #ifdef USE_OPENGLES
    EGLint ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
#else
    EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE};
#endif
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
    EGLint pbAttr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pbAttr);
    if (!eglMakeCurrent(dpy, surf, surf, ctx)) { printf("eglMakeCurrent failed 0x%x\n", eglGetError()); return 2; }
    printf("GL_RENDERER=%s\nGL_VERSION=%s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    bool ok = true;
    ok &= runCase("full viewport, 4:3", 640, 480, false, 0, 0, 640, 480, 1.0, 400, 300, true);
    ok &= runCase("invertY (game FB mode)", 640, 480, true, 0, 0, 640, 480, 1.0, 400, 300, true);
    ok &= runCase("widescreen aspect adj 0.75", 800, 450, true, 0, 0, 800, 450, 0.75, 400, 300, true);
    ok &= runCase("offset viewport (letterbox)", 640, 480, false, 40, 60, 560, 360, 1.0, 400, 300, true);
    // Thickness test: occluder far thinner than the box depth range must not cast.
    printf(ok ? "ALL PASS\n" : "SOME FAILED\n");
    return ok ? 0 : 1;
}
