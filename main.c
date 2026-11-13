/* Pip's Dash 3D - an original 3D side-scrolling platformer for PSP (PSPSDK + libgu/libgum) */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <math.h>
#include <string.h>

PSP_MODULE_INFO("PipsDash3D", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define BUF_W 512
#define SCR_W 480
#define SCR_H 272

#define T 16            /* tile size in pixels (game logic units) */
#define LW 120
#define LH 17
#define PW 12
#define PH 14
#define EW 14
#define EH 12
#define MAXE 16

static unsigned int __attribute__((aligned(16))) list[262144];
typedef struct { unsigned int color; short x, y, z; } Vertex;
typedef struct { unsigned int color; float x, y, z; } Vtx3;

/* ---------- exit callback ---------- */
static int exit_callback(int a1, int a2, void *c) { sceKernelExitGame(); return 0; }
static int callback_thread(SceSize args, void *argp) {
    int id = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void) {
    int t = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (t >= 0) sceKernelStartThread(t, 0, 0);
}

/* ---------- graphics setup ---------- */
static void init_gu(void) {
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, (void *)0x88000, BUF_W);
    sceGuDepthBuffer((void *)0x110000, BUF_W);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

/* 2D rectangle (used for the HUD) */
static void rect(int x, int y, int w, int h, unsigned int color) {
    Vertex *v = (Vertex *)sceGuGetMemory(2 * sizeof(Vertex));
    v[0].color = color; v[0].x = x;     v[0].y = y;     v[0].z = 0;
    v[1].color = color; v[1].x = x + w; v[1].y = y + h; v[1].z = 0;
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, v);
}

static const unsigned short DIGITS[10] = {
    0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF
};

static void draw_number(int x, int y, int n, int s, unsigned int color) {
    char buf[12];
    int len = 0;
    if (n == 0) buf[len++] = 0;
    while (n > 0 && len < 11) { buf[len++] = n % 10; n /= 10; }
    for (int i = len - 1; i >= 0; i--) {
        unsigned short f = DIGITS[(int)buf[i]];
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 3; c++)
                if ((f >> (14 - (r * 3 + c))) & 1)
                    rect(x + c * s, y + r * s, s, s, color);
        x += 4 * s;
    }
}

/* ---------- 3D helpers: colored boxes with per-face shading ---------- */
static unsigned int shade(unsigned int c, float f) {
    int r = (int)((c & 0xFF) * f);
    int g = (int)(((c >> 8) & 0xFF) * f);
    int b = (int)(((c >> 16) & 0xFF) * f);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return 0xFF000000 | ((unsigned)b << 16) | ((unsigned)g << 8) | (unsigned)r;
}

static void put_quad(Vtx3 *v, unsigned int c, const float q[4][3]) {
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < 6; i++) {
        v[i].color = c;
        v[i].x = q[idx[i]][0];
        v[i].y = q[idx[i]][1];
        v[i].z = q[idx[i]][2];
    }
}

/* box with min corner (x,y,z) and size (w,h,d); y is up, +z points to the camera */
static void cube(float x, float y, float z, float w, float h, float d, unsigned int c) {
    float x1 = x + w, y1 = y + h, z1 = z + d;
    float front[4][3] = { {x, y, z1}, {x1, y, z1}, {x1, y1, z1}, {x, y1, z1} };
    float top[4][3]   = { {x, y1, z1}, {x1, y1, z1}, {x1, y1, z}, {x, y1, z} };
    float left[4][3]  = { {x, y, z}, {x, y, z1}, {x, y1, z1}, {x, y1, z} };
    float right[4][3] = { {x1, y, z1}, {x1, y, z}, {x1, y1, z}, {x1, y1, z1} };
    Vtx3 *v = (Vtx3 *)sceGuGetMemory(24 * sizeof(Vtx3));
    put_quad(v,      shade(c, 1.0f), front);
    put_quad(v + 6,  shade(c, 1.2f), top);
    put_quad(v + 12, shade(c, 0.7f), left);
    put_quad(v + 18, shade(c, 0.8f), right);
    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 24, 0, v);
}

/* ---------- level ---------- */
/* tiles: 0 empty, 1 solid, 2 coin, 3 goal pole */
static unsigned char lvl[LH][LW];

static void platform(int x, int y, int len) {
    for (int i = 0; i < len; i++) {
        lvl[y][x + i] = 1;
        lvl[y - 2][x + i] = 2;
    }
}

static void build_level(void) {
    memset(lvl, 0, sizeof(lvl));
    for (int x = 0; x < LW; x++) {
        int gap = (x >= 30 && x <= 32) || (x >= 55 && x <= 58) || (x >= 80 && x <= 82);
        if (gap) continue;
        lvl[15][x] = 1;
        lvl[16][x] = 1;
    }
    platform(12, 11, 4);
    platform(26, 10, 3);
    platform(35, 11, 4);
    platform(45, 9, 5);
    platform(60, 11, 4);
    platform(75, 10, 4);
    platform(86, 11, 3);
    platform(95, 9, 5);
    for (int i = 0; i < 3; i++) lvl[11][30 + i] = 2;
    for (int i = 0; i < 4; i++) lvl[11][55 + i] = 2;
    for (int i = 0; i < 3; i++) lvl[11][80 + i] = 2;
    for (int i = 0; i < 5; i++)
        for (int h = 0; h <= i; h++)
            lvl[14 - h][104 + i] = 1;
    for (int y = 5; y <= 14; y++) lvl[y][115] = 3;
}

static int solid(int tx, int ty) {
    if (tx < 0 || tx >= LW) return 1;
    if (ty < 0 || ty >= LH) return 0;
    return lvl[ty][tx] == 1;
}

static int box_hits(float x, float y, float w, float h) {
    int x0 = (int)floorf(x / T), x1 = (int)floorf((x + w - 0.01f) / T);
    int y0 = (int)floorf(y / T), y1 = (int)floorf((y + h - 0.01f) / T);
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++)
            if (solid(tx, ty)) return 1;
    return 0;
}

static int overlap(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

/* ---------- entities ---------- */
typedef struct { float x, y, vx, vy; int ground, face; } Player;
typedef struct { float x, y, vx; int alive; } Enemy;

static Player p;
static Enemy en[MAXE];
static int ne;
static int coins, deaths, won;
static float frame_t = 0;

static void reset_level(void) {
    static const int ex[] = { 20, 40, 48, 65, 70, 90, 100 };
    build_level();
    coins = 0; won = 0;
    p.x = 2 * T; p.y = 13 * T; p.vx = 0; p.vy = 0; p.ground = 0; p.face = 1;
    ne = 0;
    for (unsigned i = 0; i < sizeof(ex) / sizeof(ex[0]) && ne < MAXE; i++) {
        en[ne].x = ex[i] * T; en[ne].y = 15 * T - EH;
        en[ne].vx = -0.6f; en[ne].alive = 1;
        ne++;
    }
}

static void update_enemies(void) {
    for (int i = 0; i < ne; i++) {
        Enemy *e = &en[i];
        if (!e->alive) continue;
        float footx = (e->vx > 0) ? e->x + EW + 1 : e->x - 1;
        int no_ground = !solid((int)floorf(footx / T), (int)floorf((e->y + EH + 1) / T));
        if (box_hits(e->x + e->vx, e->y, EW, EH) || no_ground) e->vx = -e->vx;
        else e->x += e->vx;
    }
}

static void update_player(unsigned held, unsigned pressed, int lx) {
    int left  = (held & PSP_CTRL_LEFT)  || lx < 64;
    int right = (held & PSP_CTRL_RIGHT) || lx > 192;
    float maxv = (held & PSP_CTRL_SQUARE) ? 3.2f : 2.0f;
    float target = 0;
    if (left)  { target = -maxv; p.face = -1; }
    if (right) { target =  maxv; p.face =  1; }
    float accel = p.ground ? 0.25f : 0.15f;
    if (p.vx < target) { p.vx += accel; if (p.vx > target) p.vx = target; }
    else if (p.vx > target) { p.vx -= accel; if (p.vx < target) p.vx = target; }

    if ((pressed & PSP_CTRL_CROSS) && p.ground) { p.vy = -6.2f; p.ground = 0; }
    if (!(held & PSP_CTRL_CROSS) && p.vy < -2.5f) p.vy = -2.5f;

    p.vy += 0.35f;
    if (p.vy > 8.0f) p.vy = 8.0f;

    int n = (int)ceilf(fabsf(p.vx));
    for (int i = 0; i < n; i++) {
        float s = p.vx / n;
        if (!box_hits(p.x + s, p.y, PW, PH)) p.x += s;
        else { p.vx = 0; break; }
    }
    p.ground = 0;
    n = (int)ceilf(fabsf(p.vy));
    for (int i = 0; i < n; i++) {
        float s = p.vy / n;
        if (!box_hits(p.x, p.y + s, PW, PH)) p.y += s;
        else { if (p.vy > 0) p.ground = 1; p.vy = 0; break; }
    }

    int x0 = (int)floorf(p.x / T), x1 = (int)floorf((p.x + PW - 0.01f) / T);
    int y0 = (int)floorf(p.y / T), y1 = (int)floorf((p.y + PH - 0.01f) / T);
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++) {
            if (tx < 0 || tx >= LW || ty < 0 || ty >= LH) continue;
            if (lvl[ty][tx] == 2) { lvl[ty][tx] = 0; coins++; }
            else if (lvl[ty][tx] == 3) won = 1;
        }

    for (int i = 0; i < ne; i++) {
        Enemy *e = &en[i];
        if (!e->alive) continue;
        if (overlap(p.x, p.y, PW, PH, e->x, e->y, EW, EH)) {
            if (p.vy > 0 && (p.y + PH) - e->y < 10) { e->alive = 0; p.vy = -4.5f; }
            else { deaths++; reset_level(); return; }
        }
    }
    if (p.y > LH * T + 32) { deaths++; reset_level(); }
}

/* ---------- drawing (3D scene, then 2D HUD) ---------- */
static void draw_all(void) {
    float camx = p.x / T + 0.4f;
    if (camx < 15.5f) camx = 15.5f;
    if (camx > LW - 15.5f) camx = LW - 15.5f;
    frame_t += 0.08f;

    sceGuStart(GU_DIRECT, list);
    sceGuClearColor(0xFFF0C890);   /* sky */
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    sceGuEnable(GU_DEPTH_TEST);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(60.0f, 480.0f / 272.0f, 1.0f, 200.0f);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    {
        ScePspFVector3 cam = { -camx, -8.5f, -15.0f };
        sceGumTranslate(&cam);
    }
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    /* far background: hills and clouds (perspective gives free parallax) */
    for (int i = 0; i < 10; i++) {
        float hx = i * 13.0f - 4.0f;
        if (fabsf(hx - camx) < 45.0f)
            cube(hx, 0.0f, -9.0f, 9.0f, 3.0f + (i % 3) * 1.5f, 4.0f, 0xFF40A040);
        float cx = i * 17.0f - 6.0f;
        if (fabsf(cx - camx) < 45.0f)
            cube(cx, 12.0f + (i % 3), -14.0f, 4.0f, 1.0f, 1.5f, 0xFFFFFFFF);
    }

    /* tiles */
    int t0 = (int)camx - 17, t1 = (int)camx + 17;
    if (t0 < 0) t0 = 0;
    if (t1 >= LW) t1 = LW - 1;
    for (int ty = 0; ty < LH; ty++)
        for (int tx = t0; tx <= t1; tx++) {
            float y = (float)(LH - 1 - ty);
            switch (lvl[ty][tx]) {
            case 1:
                if (ty == 0 || lvl[ty - 1][tx] != 1) {
                    cube(tx, y, -0.5f, 1.0f, 0.8f, 1.0f, 0xFF2A5A8C);
                    cube(tx, y + 0.8f, -0.5f, 1.0f, 0.2f, 1.0f, 0xFF30B040);
                } else {
                    cube(tx, y, -0.5f, 1.0f, 1.0f, 1.0f, 0xFF2A5A8C);
                }
                break;
            case 2: {
                float w = 0.1f + 0.4f * fabsf(cosf(frame_t + tx * 0.5f));
                cube(tx + 0.5f - w / 2, y + 0.25f, -0.1f, w, 0.5f, 0.2f, 0xFF00D8FF);
                break;
            }
            case 3:
                cube(tx + 0.42f, y, -0.08f, 0.16f, 1.0f, 0.16f, 0xFFA0A0A0);
                if (ty == 0 || lvl[ty - 1][tx] != 3)
                    cube(tx + 0.58f, y + 0.5f, -0.04f, 0.9f, 0.45f, 0.08f, 0xFF3030E0);
                break;
            }
        }

    /* enemies */
    for (int i = 0; i < ne; i++) {
        Enemy *e = &en[i];
        if (!e->alive) continue;
        float ex = e->x / T;
        float ey = (LH * T - (e->y + EH)) / (float)T;
        cube(ex, ey, -0.4f, EW / (float)T, EH / (float)T, 0.8f, 0xFF5050C0);
        cube(ex + 0.15f, ey + 0.45f, 0.4f, 0.15f, 0.15f, 0.05f, 0xFFFFFFFF);
        cube(ex + 0.6f,  ey + 0.45f, 0.4f, 0.15f, 0.15f, 0.05f, 0xFFFFFFFF);
    }

    /* player: Pip */
    {
        float pxw = p.x / T;
        float pyw = (LH * T - (p.y + PH)) / (float)T;
        float w = PW / (float)T, h = PH / (float)T;
        cube(pxw, pyw, -0.35f, w, h * 0.72f, 0.7f, 0xFFC0B020);
        cube(pxw - 0.05f, pyw + h * 0.72f, -0.4f, w + 0.1f, h * 0.3f, 0.8f, 0xFF40D8F0);
        float ex = (p.face > 0) ? pxw + w * 0.55f : pxw + w * 0.1f;
        cube(ex, pyw + h * 0.35f, 0.35f, 0.22f, 0.22f, 0.05f, 0xFFFFFFFF);
    }

    /* 2D HUD */
    sceGuDisable(GU_DEPTH_TEST);
    rect(8, 8, 8, 8, 0xFF00D8FF);
    draw_number(20, 8, coins, 3, 0xFFFFFFFF);
    rect(80, 8, 8, 8, 0xFF3030E0);
    draw_number(92, 8, deaths, 3, 0xFFFFFFFF);
    if (won) {
        rect(0, 110, SCR_W, 50, 0xC0000000);
        rect(140, 130, 200, 8, 0xFF40E040);
    }

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

/* ---------- main ---------- */
int main(void) {
    setup_callbacks();
    init_gu();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    deaths = 0;
    reset_level();

    SceCtrlData pad;
    unsigned old = 0;

    while (1) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned held = pad.Buttons;
        unsigned pressed = held & ~old;
        old = held;

        if (won) {
            if (pressed & PSP_CTRL_START) { deaths = 0; reset_level(); }
        } else {
            update_enemies();
            update_player(held, pressed, pad.Lx);
        }
        draw_all();
    }
    return 0;
}
