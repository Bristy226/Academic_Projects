#include <windows.h>
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <vector>

/* =====================================================================
   3D MUSEUM  -  C++ / OpenGL / GLUT   (single file, no external images)
   =====================================================================
   PROJECT REQUIREMENTS  ->  WHERE THEY ARE IN THIS CODE
   ---------------------------------------------------------------------
   1. 3D scene ............................. museum() / outdoors()
   2. Rotation, translation, scaling ........ keys a-f (rotate scene),
        (keyboard / mouse)                    g/G v/V (translate statue),
                                              k/l n/m (scale statue/painting),
                                              arrows, x/X y/Y z/Z (move camera)
   3. Complex objects ....................... statue(), chair(), table(),
                                              pillar(), tree(), exhibit()
   4. Continuous rotating object ............ ceiling fan() + golden teapot
                                              exhibit (both spin forever)
   5. Window / picture frame ................ real window in right wall
                                              (see outdoors through it),
                                              2 picture frames on the walls
   6. >= 2 light sources, materials with
      ambient + diffuse + specular .......... initLighting(), setMaterial()
                                              LIGHT0 lamp, LIGHT1 spot, LIGHT2 sun
   7. Object coordinate + viewing
      coordinate transformation ............. display():  gluLookAt() = viewing,
                                              glTranslate/Rotate/Scale = object
   8. Textures .............................. brick wall, wood door/chairs,
                                              tile floor, marble, grass,
                                              starry painting, mosaic painting
                                              (all generated in code)

   CONTROLS
   ---------------------------------------------------------------------
     Up / Down arrows   : walk forward / backward
     Left / Right       : turn left / right
     PageUp / PageDown  : look up / down
     Right-mouse drag   : look around
     x / X , y / Y , z / Z : move camera along world X / Y / Z
     r                  : reset camera (in front of the door)
     o                  : open / close the door (smooth animation)
     a / b , c / d , e / f : rotate whole scene about X / Y / Z
     k / l              : scale statue up / down
     n / m              : scale the back painting up / down
     g / G , v / V      : translate the statue along X / Z
     + / -              : fan speed
     0 / 1              : ceiling lamp  OFF / ON   (LIGHT0)
     8 / 7              : yellow spot   OFF / ON   (LIGHT1)
     2 / 3              : sun           OFF / ON   (LIGHT2)
     Left mouse click   : change the colours of the left painting
     q / Esc            : quit
   ===================================================================== */

/* ------------------------- scene constants ------------------------- */
const float ROOM_S  = 12.0f;    // room is ROOM_S x ROOM_S
const float ROOM_H  = 4.0f;     // wall height
const float WALL_T  = 0.25f;    // wall thickness
const float DOOR_X  = 0.0f;     // door centre on the front wall
const float DOOR_W  = 1.2f;
const float DOOR_H  = 2.3f;
const float WIN_ZC  = -1.5f;    // window centre (right wall, z)
const float WIN_W   = 3.0f;
const float WIN_Y0  = 1.0f;
const float WIN_Y1  = 3.0f;

/* --------------------------- global state -------------------------- */
float degreeX = 0, degreeY = 0, degreeZ = 0;   // whole-scene rotation
float fanAngle = 0, fanSpeed = 3.0f;           // ceiling fan
float exhibitAngle = 0;                        // spinning teapot
float doorAngle = 0, doorTarget = 0;           // door animation
float statueScale = 1.0f, paintingScale = 1.0f;
float statX = -3.5f, statZ = -4.6f;            // statue position
int   paintingColorIdx = 0;
bool  light0On = true, light1On = true, sunOn = true;

const double START_X = DOOR_X, START_Y = 1.5, START_Z = 10.0;
double ex = START_X, ey = START_Y, ez = START_Z;   // camera position
double yaw = 0.0, pitch = 0.0;                     // camera direction

bool dragging = false;
int  lastMX = 0, lastMY = 0;
bool shadowPass = false;

GLuint texBrick, texWood, texTile, texGrass, texMarble, texStars, texMosaic;
GLUquadric *gq;

GLfloat lightPos0[4] = { 0.0f, 3.75f, 1.5f, 1.0f };   // ceiling lamp

static inline float mx(float a, float b) { return a > b ? a : b; }
static inline float mn(float a, float b) { return a < b ? a : b; }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* =====================================================================
   PROCEDURAL TEXTURES
   ===================================================================== */
static float hash2(int x, int y)
{
    unsigned int n = (unsigned int)x * 374761393u + (unsigned int)y * 668265263u + 1013u;
    n = (n ^ (n >> 13)) * 1274126177u;
    n ^= (n >> 16);
    return (n & 0xFFFF) / 65535.0f;
}

/* smooth value noise that repeats every px x py lattice cells (tileable) */
static float vnoise(float x, float y, int px, int py)
{
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float xf = x - xi, yf = y - yi;
    int x0 = ((xi % px) + px) % px, x1 = (x0 + 1) % px;
    int y0 = ((yi % py) + py) % py, y1 = (y0 + 1) % py;
    float u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
    float a = hash2(x0, y0), b = hash2(x1, y0), c = hash2(x0, y1), d = hash2(x1, y1);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}

struct Img
{
    int w, h;
    std::vector<unsigned char> d;
    Img(int W, int H) : w(W), h(H), d(W * H * 3, 0) {}
    static unsigned char cv(float v) { if (v < 0) v = 0; if (v > 1) v = 1; return (unsigned char)(v * 255.0f); }
    void set(int x, int y, float r, float g, float b)
    {
        int i = (y * w + x) * 3;
        d[i] = cv(r); d[i + 1] = cv(g); d[i + 2] = cv(b);
    }
};

GLuint uploadTex(const Img &im, GLuint id)
{
    if (id == 0) glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gluBuild2DMipmaps(GL_TEXTURE_2D, 3, im.w, im.h, GL_RGB, GL_UNSIGNED_BYTE, &im.d[0]);
    return id;
}

/* grey-beige bricks with dark mortar (texture covers 3 x 3 world units) */
GLuint buildBrick()
{
    Img im(256, 256);
    const int BW = 64, BH = 32, M = 3;
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++)
        {
            int row = y / BH;
            int xs = (x + ((row & 1) ? BW / 2 : 0)) % 256;
            int col = xs / BW;
            int bx = xs % BW, by = y % BH;
            float n = hash2(x, y);
            if (bx < M || by < M)
            {
                float m = 0.38f + 0.07f * n;
                im.set(x, y, m, m, m * 0.98f);
            }
            else
            {
                float var  = 0.80f + 0.25f * hash2(col + 11, row + 3);
                float edge = (bx < M + 2 || by < M + 2) ? 0.90f : 1.0f;
                float c = var * edge * (0.93f + 0.12f * n);
                im.set(x, y, 0.74f * c, 0.71f * c, 0.66f * c);
            }
        }
    return uploadTex(im, 0);
}

GLuint buildWood()
{
    Img im(256, 256);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++)
        {
            float n  = vnoise(x / 64.0f, y / 16.0f, 4, 16);
            float n2 = vnoise(x / 16.0f, y / 8.0f, 16, 32);
            float ring = 0.5f + 0.5f * sinf((y / 256.0f * 10.0f + n * 2.5f) * 6.2831853f);
            float t = 0.30f + 0.50f * ring + 0.20f * n2;
            float f = hash2(x, y) * 0.05f;
            im.set(x, y, 0.40f + 0.38f * t + f, 0.23f + 0.29f * t + f, 0.10f + 0.18f * t + f);
        }
    return uploadTex(im, 0);
}

/* 2 x 2 checker tiles (texture covers 2 x 2 world units) */
GLuint buildTile()
{
    Img im(128, 128);
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++)
        {
            bool dark = (((x / 64) + (y / 64)) % 2) == 0;
            float n = vnoise(x / 16.0f, y / 16.0f, 8, 8) * 0.12f + hash2(x, y) * 0.04f;
            float r, g, b;
            if (dark) { r = 0.30f + n; g = 0.26f + n; b = 0.22f + n; }
            else      { r = 0.80f + n; g = 0.76f + n; b = 0.68f + n; }
            if ((x % 64) < 1 || (y % 64) < 1) { r *= 0.6f; g *= 0.6f; b *= 0.6f; }
            im.set(x, y, r, g, b);
        }
    return uploadTex(im, 0);
}

GLuint buildGrass()
{
    Img im(128, 128);
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++)
        {
            float n = vnoise(x / 8.0f, y / 8.0f, 16, 16);
            float g = hash2(x, y);
            im.set(x, y, 0.16f + 0.10f * n + 0.05f * g,
                         0.42f + 0.22f * n + 0.10f * g,
                         0.10f + 0.06f * n);
        }
    return uploadTex(im, 0);
}

GLuint buildMarble()
{
    Img im(256, 256);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++)
        {
            float fb = vnoise(x / 64.0f, y / 64.0f, 4, 4) + 0.5f * vnoise(x / 32.0f, y / 32.0f, 8, 8);
            float ph = (x + y) * 6.2831853f * 2.0f / 256.0f + fb * 5.0f;
            float t = fabsf(sinf(ph));
            float vein = powf(1.0f - t, 10.0f) * 0.65f;
            float base = 0.90f - 0.04f * hash2(x, y);
            im.set(x, y, base * (1 - vein) + 0.40f * vein,
                         base * (1 - vein) + 0.42f * vein,
                         base * (1 - vein) + 0.48f * vein);
        }
    return uploadTex(im, 0);
}

/* night-sky painting: sky + stars + moon on top, grass hills at the bottom */
GLuint buildStars()
{
    Img im(256, 128);
    for (int y = 0; y < 128; y++)       // y = 0 is the BOTTOM of the picture
        for (int x = 0; x < 256; x++)
        {
            float v = y / 128.0f;
            float hill = 0.36f + 0.04f * sinf(x * 0.05f) + 0.02f * sinf(x * 0.13f);
            if (v < hill)
            {
                float n = vnoise(x / 8.0f, y / 8.0f, 32, 16);
                im.set(x, y, 0.10f + 0.10f * n, 0.45f + 0.25f * n, 0.12f + 0.06f * n);
            }
            else
            {
                float k = (v - hill) / (1.0f - hill);
                float r = 0.14f - 0.10f * k, g = 0.22f - 0.15f * k, b = 0.55f - 0.25f * k;
                if (hash2(x * 7 + 13, y * 13 + 5) > 0.990f) { r = g = b = 1.0f; }
                float dx = x - 205.0f, dy = y - 98.0f;
                float dd = sqrtf(dx * dx + dy * dy);
                if (dd < 14.0f)      { r = 1.0f;  g = 0.97f; b = 0.75f; }
                else if (dd < 22.0f) { float a = (22.0f - dd) / 8.0f * 0.35f; r += a; g += a; b += a * 0.8f; }
                im.set(x, y, r, g, b);
            }
        }
    return uploadTex(im, 0);
}

static void hsv2rgb(float h, float s, float v, float &r, float &g, float &b)
{
    float hh = (h - floorf(h)) * 6.0f;
    int i = (int)hh;
    float f = hh - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    switch (i % 6)
    {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
}

/* colourful mosaic painting; palette 0 = all colours, 1 = warm, 2 = cool */
void buildMosaic(int palette)
{
    Img im(128, 128);
    for (int by = 0; by < 16; by++)
        for (int bx = 0; bx < 16; bx++)
        {
            float hu = hash2(bx + palette * 31, by + 7);
            float hue;
            if (palette == 0) hue = hu;
            else if (palette == 1) hue = hu * 0.16f;
            else hue = 0.50f + hu * 0.25f;
            float sat = 0.35f + 0.5f * hash2(bx + 3, by + palette * 17);
            float r, g, b;
            hsv2rgb(hue, sat, 0.95f, r, g, b);
            for (int y = by * 8; y < by * 8 + 8; y++)
                for (int x = bx * 8; x < bx * 8 + 8; x++)
                    im.set(x, y, r, g, b);
        }
    texMosaic = uploadTex(im, texMosaic);
}

void buildTextures()
{
    texBrick  = buildBrick();
    texWood   = buildWood();
    texTile   = buildTile();
    texGrass  = buildGrass();
    texMarble = buildMarble();
    texStars  = buildStars();
    texMosaic = 0;
    buildMosaic(0);
}

/* =====================================================================
   MATERIAL / TEXTURE / PRIMITIVE HELPERS
   (during the shadow pass they do nothing, so the same drawing code can
    be reused to render flat black shadows)
   ===================================================================== */
void setMaterial(float r, float g, float b, float shin = 30.0f, float spec = 0.4f)
{
    if (shadowPass) return;
    const GLfloat amb[]  = { r * 0.4f, g * 0.4f, b * 0.4f, 1.0f };
    const GLfloat diff[] = { r, g, b, 1.0f };
    const GLfloat sp[]   = { spec, spec, spec, 1.0f };
    const GLfloat sh[]   = { shin };
    const GLfloat em[]   = { 0.0f, 0.0f, 0.0f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT,   amb);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE,   diff);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR,  sp);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, sh);
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION,  em);
}

void setEmission(float r, float g, float b)
{
    if (shadowPass) return;
    const GLfloat em[] = { r, g, b, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, em);
}

void useTex(GLuint id)
{
    if (shadowPass) return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, id);
}

void noTex()
{
    if (shadowPass) return;
    glDisable(GL_TEXTURE_2D);
}

void tv(float u, float v, float x, float y, float z)
{
    glTexCoord2f(u, v);
    glVertex3f(x, y, z);
}

/* box with its corner at the origin, extends +x +y +z, with tex coords */
void box(float sx, float sy, float sz)
{
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1);  tv(0, 0, 0, 0, sz);  tv(1, 0, sx, 0, sz);  tv(1, 1, sx, sy, sz);  tv(0, 1, 0, sy, sz);
    glNormal3f(0, 0, -1); tv(0, 0, sx, 0, 0);  tv(1, 0, 0, 0, 0);    tv(1, 1, 0, sy, 0);    tv(0, 1, sx, sy, 0);
    glNormal3f(1, 0, 0);  tv(0, 0, sx, 0, sz); tv(1, 0, sx, 0, 0);   tv(1, 1, sx, sy, 0);   tv(0, 1, sx, sy, sz);
    glNormal3f(-1, 0, 0); tv(0, 0, 0, 0, 0);   tv(1, 0, 0, 0, sz);   tv(1, 1, 0, sy, sz);   tv(0, 1, 0, sy, 0);
    glNormal3f(0, 1, 0);  tv(0, 0, 0, sy, sz); tv(1, 0, sx, sy, sz); tv(1, 1, sx, sy, 0);   tv(0, 1, 0, sy, 0);
    glNormal3f(0, -1, 0); tv(0, 0, 0, 0, 0);   tv(1, 0, sx, 0, 0);   tv(1, 1, sx, 0, sz);   tv(0, 1, 0, 0, sz);
    glEnd();
}

/* ellipsoid centred at (px,py,pz) with radii (sx,sy,sz) */
void ell(float px, float py, float pz, float sx, float sy, float sz)
{
    glPushMatrix();
    glTranslatef(px, py, pz);
    glScalef(sx, sy, sz);
    gluSphere(gq, 1.0, 28, 20);
    glPopMatrix();
}

/* cylinder / cone standing along +Y */
void cylY(float rb, float rt, float h, int slices = 24)
{
    glPushMatrix();
    glRotatef(-90, 1, 0, 0);
    gluCylinder(gq, rb, rt, h, slices, 3);
    glPopMatrix();
}

/* lights used for the inside of the museum / for the outdoors */
void lightsInterior()
{
    if (light0On) glEnable(GL_LIGHT0); else glDisable(GL_LIGHT0);
    if (light1On) glEnable(GL_LIGHT1); else glDisable(GL_LIGHT1);
    glDisable(GL_LIGHT2);
}

void lightsExterior()
{
    glDisable(GL_LIGHT0);
    glDisable(GL_LIGHT1);
    if (sunOn) glEnable(GL_LIGHT2); else glDisable(GL_LIGHT2);
}

/* =====================================================================
   HUD, SKY
   ===================================================================== */
void drawSkyGradient()
{
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, 1, 0, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glBegin(GL_QUADS);
        glColor3f(0.20f, 0.42f, 0.85f);   // top
        glVertex2f(0, 1); glVertex2f(1, 1);
        glColor3f(0.78f, 0.90f, 1.00f);   // horizon
        glVertex2f(1, 0); glVertex2f(0, 0);
    glEnd();

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

void drawText(float x, float y, const char *text)
{
    glRasterPos2f(x, y);
    for (const char *c = text; *c != '\0'; c++)
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);
}

void drawHUD()
{
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, 1, 0, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 0.0f, 0.0f, 0.45f);
    glBegin(GL_QUADS);
        glVertex2f(0.005f, 0.885f); glVertex2f(0.995f, 0.885f);
        glVertex2f(0.995f, 0.995f); glVertex2f(0.005f, 0.995f);
    glEnd();
    glDisable(GL_BLEND);

    glColor3f(1.0f, 1.0f, 1.0f);
    drawText(0.015f, 0.965f, "3D MUSEUM | arrows: walk/turn  PgUp/PgDn: look  right-drag: look around  x/X y/Y z/Z: move camera  r: reset  o: door");
    drawText(0.015f, 0.935f, "a/b c/d e/f: rotate scene   k/l: scale statue   n/m: scale painting   g/G v/V: move statue   +/-: fan speed");
    drawText(0.015f, 0.905f, "0/1: lamp   8/7: spot   2/3: sun   left click: change painting colours");

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

/* =====================================================================
   ROOM SHELL: floor, ceiling, walls (with thickness), door and window
   ===================================================================== */
void roomFloor()
{
    float h = ROOM_S / 2;
    useTex(texTile);
    setMaterial(0.95f, 0.95f, 0.95f, 30.0f, 0.35f);
    glNormal3f(0, 1, 0);
    glBegin(GL_QUADS);
    for (int i = 0; i < 24; i++)
        for (int j = 0; j < 24; j++)
        {
            float x0 = -h + i * 0.5f, z0 = -h + j * 0.5f, x1 = x0 + 0.5f, z1 = z0 + 0.5f;
            tv(x0 / 2, z0 / 2, x0, 0, z0);
            tv(x0 / 2, z1 / 2, x0, 0, z1);
            tv(x1 / 2, z1 / 2, x1, 0, z1);
            tv(x1 / 2, z0 / 2, x1, 0, z0);
        }
    glEnd();
}

void roomCeiling()
{
    float h = ROOM_S / 2;
    noTex();
    setMaterial(0.36f, 0.38f, 0.80f, 15.0f, 0.15f);       // blue ceiling
    glNormal3f(0, -1, 0);
    glBegin(GL_QUADS);
    for (int i = 0; i < 12; i++)
        for (int j = 0; j < 12; j++)
        {
            float x0 = -h + i, z0 = -h + j, x1 = x0 + 1, z1 = z0 + 1;
            glVertex3f(x0, ROOM_H, z0); glVertex3f(x1, ROOM_H, z0);
            glVertex3f(x1, ROOM_H, z1); glVertex3f(x0, ROOM_H, z1);
        }
    glEnd();

    // round ceiling lamp (glows only when LIGHT0 is on)
    glPushMatrix();
    glTranslatef(lightPos0[0], ROOM_H - 0.02f, lightPos0[2]);
    glRotatef(90, 1, 0, 0);                               // disc faces down
    setMaterial(1.0f, 1.0f, 0.95f, 20.0f, 0.2f);
    if (light0On) setEmission(1.0f, 1.0f, 0.95f); else setEmission(0.15f, 0.15f, 0.15f);
    gluDisk(gq, 0.0, 0.28, 32, 1);
    setMaterial(0.15f, 0.15f, 0.15f, 20.0f, 0.2f);
    gluDisk(gq, 0.28, 0.35, 32, 1);
    glPopMatrix();
    setMaterial(0.5f, 0.5f, 0.5f);

    // small fixture of the yellow spot light (follows the statue)
    glPushMatrix();
    glTranslatef(statX, ROOM_H - 0.22f, statZ + 1.0f);
    setMaterial(0.3f, 0.3f, 0.3f, 40.0f, 0.5f);
    cylY(0.09f, 0.16f, 0.22f, 16);
    glRotatef(90, 1, 0, 0);
    setMaterial(1.0f, 0.9f, 0.4f, 20.0f, 0.2f);
    if (light1On) setEmission(1.0f, 0.9f, 0.4f); else setEmission(0.1f, 0.1f, 0.05f);
    gluDisk(gq, 0.0, 0.09, 16, 1);
    glPopMatrix();
    setMaterial(0.5f, 0.5f, 0.5f);
}

/* rectangle of a wall in wall-local coordinates (z = 0, normal +z) */
void wallRect(float x0, float y0, float x1, float y1)
{
    if (x1 <= x0 + 0.0001f || y1 <= y0 + 0.0001f) return;
    const float B = 3.0f;                                   // brick texture size in world units
    int nx = (int)ceilf(x1 - x0), ny = (int)ceilf(y1 - y0);
    if (nx < 1) nx = 1;
    if (ny < 1) ny = 1;
    glNormal3f(0, 0, 1);
    glBegin(GL_QUADS);
    for (int i = 0; i < nx; i++)
        for (int j = 0; j < ny; j++)
        {
            float xa = x0 + (x1 - x0) * i / nx,       xb = x0 + (x1 - x0) * (i + 1) / nx;
            float ya = y0 + (y1 - y0) * j / ny,       yb = y0 + (y1 - y0) * (j + 1) / ny;
            tv(xa / B, ya / B, xa, ya, 0);
            tv(xb / B, ya / B, xb, ya, 0);
            tv(xb / B, yb / B, xb, yb, 0);
            tv(xa / B, yb / B, xa, yb, 0);
        }
    glEnd();
}

/* brick wall w x h with an optional rectangular hole (hx1 <= hx0 = no hole) */
void wallWithHole(float w, float h, float hx0, float hx1, float hy0, float hy1)
{
    useTex(texBrick);
    setMaterial(0.95f, 0.93f, 0.90f, 10.0f, 0.12f);
    if (hx1 <= hx0) { wallRect(0, 0, w, h); return; }
    wallRect(0, 0, hx0, h);          // left of the hole
    wallRect(hx1, 0, w, h);          // right of the hole
    wallRect(hx0, 0, hx1, hy0);      // below the hole
    wallRect(hx0, hy1, hx1, h);      // above the hole
}

/* the thickness of the wall inside the hole */
void holeReveal(float hx0, float hx1, float hy0, float hy1)
{
    const float T = WALL_T;
    useTex(texBrick);
    setMaterial(0.85f, 0.83f, 0.80f, 10.0f, 0.1f);
    glBegin(GL_QUADS);
    glNormal3f(1, 0, 0);                                     // left side
    tv(0, hy0, hx0, hy0, 0); tv(T, hy0, hx0, hy0, -T); tv(T, hy1, hx0, hy1, -T); tv(0, hy1, hx0, hy1, 0);
    glNormal3f(-1, 0, 0);                                    // right side
    tv(0, hy0, hx1, hy0, -T); tv(T, hy0, hx1, hy0, 0); tv(T, hy1, hx1, hy1, 0); tv(0, hy1, hx1, hy1, -T);
    glNormal3f(0, -1, 0);                                    // top
    tv(0, 0, hx0, hy1, 0); tv(T, 0, hx0, hy1, -T); tv(T, 1, hx1, hy1, -T); tv(0, 1, hx1, hy1, 0);
    if (hy0 > 0.01f)
    {
        glNormal3f(0, 1, 0);                                 // bottom (window sill)
        tv(0, 0, hx0, hy0, -T); tv(T, 0, hx0, hy0, 0); tv(T, 1, hx1, hy0, 0); tv(0, 1, hx1, hy0, -T);
    }
    glEnd();
}

/* complete wall: inner skin (lit by lamps), outer skin (lit by sun), reveal */
void wallFull(float w, float h, float hx0, float hx1, float hy0, float hy1)
{
    lightsInterior();
    wallWithHole(w, h, hx0, hx1, hy0, hy1);

    lightsExterior();
    glPushMatrix();
    glTranslatef(w, 0, -WALL_T);
    glRotatef(180, 0, 1, 0);
    if (hx1 > hx0) wallWithHole(w, h, w - hx1, w - hx0, hy0, hy1);
    else           wallWithHole(w, h, 0, 0, 0, 0);
    glPopMatrix();

    lightsInterior();
    if (hx1 > hx0) holeReveal(hx0, hx1, hy0, hy1);
}

void roomWalls()
{
    float h = ROOM_S / 2;

    // back wall (z = -h)
    glPushMatrix();
    glTranslatef(-h, 0, -h);
    wallFull(ROOM_S, ROOM_H, 0, 0, 0, 0);
    glPopMatrix();

    // front wall (z = +h) : door opening
    glPushMatrix();
    glTranslatef(h, 0, h);
    glRotatef(180, 0, 1, 0);
    wallFull(ROOM_S, ROOM_H, h - (DOOR_X + DOOR_W / 2), h - (DOOR_X - DOOR_W / 2), 0, DOOR_H);
    glPopMatrix();

    // left wall (x = -h)
    glPushMatrix();
    glTranslatef(-h, 0, h);
    glRotatef(90, 0, 1, 0);
    wallFull(ROOM_S, ROOM_H, 0, 0, 0, 0);
    glPopMatrix();

    // right wall (x = +h) : window opening
    glPushMatrix();
    glTranslatef(h, 0, -h);
    glRotatef(-90, 0, 1, 0);
    wallFull(ROOM_S, ROOM_H, WIN_ZC - WIN_W / 2 + h, WIN_ZC + WIN_W / 2 + h, WIN_Y0, WIN_Y1);
    glPopMatrix();

    // flat roof on top of the walls (outside, lit by the sun)
    lightsExterior();
    glPushMatrix();
    glTranslatef(-h - WALL_T, ROOM_H + 0.005f, -h - WALL_T);
    noTex();
    setMaterial(0.42f, 0.40f, 0.40f, 10.0f, 0.1f);
    box(ROOM_S + 2 * WALL_T, 0.15f, ROOM_S + 2 * WALL_T);
    glPopMatrix();
    lightsInterior();
}

/* ------------------------------ DOOR ------------------------------ */
void door()
{
    float h = ROOM_S / 2;
    glPushMatrix();
    glTranslatef(DOOR_X, 0, h);                 // centre of the opening, on the inner wall plane

    // fixed wooden frame on both faces of the wall
    useTex(texWood);
    setMaterial(0.55f, 0.36f, 0.20f, 40.0f, 0.3f);
    for (int s = 0; s < 2; s++)
    {
        float z0 = (s == 0) ? -0.06f : WALL_T;
        glPushMatrix(); glTranslatef(-DOOR_W / 2 - 0.10f, 0, z0);            box(0.10f, DOOR_H + 0.10f, 0.06f); glPopMatrix();
        glPushMatrix(); glTranslatef( DOOR_W / 2,         0, z0);            box(0.10f, DOOR_H + 0.10f, 0.06f); glPopMatrix();
        glPushMatrix(); glTranslatef(-DOOR_W / 2 - 0.10f, DOOR_H, z0);       box(DOOR_W + 0.20f, 0.10f, 0.06f); glPopMatrix();
    }

    // door leaf, hinged on its left edge
    float lw = DOOR_W - 0.04f, lh = DOOR_H - 0.02f, lt = 0.06f;
    glPushMatrix();
    glTranslatef(-DOOR_W / 2 + 0.02f, 0, WALL_T * 0.5f);     // hinge position
    glRotatef(doorAngle, 0, 1, 0);                           // rotation about the hinge

    useTex(texWood);
    setMaterial(0.98f, 0.76f, 0.46f, 40.0f, 0.35f);
    glPushMatrix(); glTranslatef(0, 0.01f, -lt / 2); box(lw, lh, lt); glPopMatrix();

    setMaterial(0.86f, 0.64f, 0.36f, 30.0f, 0.3f);           // raised panels, both faces
    for (int s = 0; s < 2; s++)
    {
        float zz = (s == 0) ? lt / 2 : -lt / 2 - 0.012f;
        glPushMatrix(); glTranslatef(0.12f, 0.15f, zz); box(lw - 0.24f, 0.80f, 0.012f); glPopMatrix();
        glPushMatrix(); glTranslatef(0.12f, 1.15f, zz); box(lw - 0.24f, 1.00f, 0.012f); glPopMatrix();
    }

    noTex();                                                 // knobs
    setMaterial(0.95f, 0.92f, 0.72f, 100.0f, 0.9f);
    ell(lw - 0.12f, 1.05f,  lt / 2 + 0.05f, 0.05f, 0.05f, 0.05f);
    ell(lw - 0.12f, 1.05f, -lt / 2 - 0.05f, 0.05f, 0.05f, 0.05f);
    glPopMatrix();

    glPopMatrix();
}

/* ----------------------------- WINDOW ----------------------------- */
void windowFrame()
{
    float h = ROOM_S / 2;
    float H = WIN_Y1 - WIN_Y0;
    glPushMatrix();
    glTranslatef(h + WALL_T * 0.5f, 0, WIN_ZC);      // local x = across the wall, z = along the wall
    useTex(texWood);
    setMaterial(0.90f, 0.90f, 0.86f, 40.0f, 0.3f);
    glPushMatrix(); glTranslatef(-0.06f, WIN_Y0, -WIN_W / 2);            box(0.12f, 0.08f, WIN_W); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.06f, WIN_Y1 - 0.08f, -WIN_W / 2);    box(0.12f, 0.08f, WIN_W); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.06f, WIN_Y0, -WIN_W / 2);            box(0.12f, H, 0.08f);     glPopMatrix();
    glPushMatrix(); glTranslatef(-0.06f, WIN_Y0, WIN_W / 2 - 0.08f);     box(0.12f, H, 0.08f);     glPopMatrix();
    glPushMatrix(); glTranslatef(-0.04f, WIN_Y0, -0.03f);                box(0.08f, H, 0.06f);     glPopMatrix();
    glPushMatrix(); glTranslatef(-0.04f, (WIN_Y0 + WIN_Y1) / 2 - 0.03f, -WIN_W / 2); box(0.08f, 0.06f, WIN_W); glPopMatrix();
    // inner sill
    glPushMatrix(); glTranslatef(-WALL_T / 2 - 0.12f, WIN_Y0 - 0.04f, -WIN_W / 2 - 0.10f);
    box(WALL_T + 0.17f, 0.05f, WIN_W + 0.20f); glPopMatrix();
    glPopMatrix();
}

/* transparent glass (drawn last, after everything behind it) */
void windowGlass()
{
    float h = ROOM_S / 2;
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glColor4f(0.65f, 0.85f, 1.0f, 0.16f);
    float x = h + WALL_T * 0.5f;
    glBegin(GL_QUADS);
    glVertex3f(x, WIN_Y0, WIN_ZC - WIN_W / 2);
    glVertex3f(x, WIN_Y0, WIN_ZC + WIN_W / 2);
    glVertex3f(x, WIN_Y1, WIN_ZC + WIN_W / 2);
    glVertex3f(x, WIN_Y1, WIN_ZC - WIN_W / 2);
    glEnd();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

/* =====================================================================
   OUTDOORS  (what you see through the window and outside the door)
   ===================================================================== */
void tree(float x, float z, float s)
{
    glPushMatrix();
    glTranslatef(x, -0.01f, z);
    glScalef(s, s, s);
    useTex(texWood);
    setMaterial(0.80f, 0.60f, 0.45f, 10.0f, 0.1f);
    cylY(0.28f, 0.18f, 2.4f, 12);
    useTex(texGrass);
    setMaterial(0.35f, 0.90f, 0.35f, 10.0f, 0.1f);
    ell(0.0f, 3.0f, 0.0f, 1.5f, 1.3f, 1.5f);
    ell(0.0f, 4.1f, 0.0f, 1.05f, 1.0f, 1.05f);
    ell(0.8f, 2.6f, 0.4f, 0.9f, 0.8f, 0.9f);
    glPopMatrix();
}

void outdoors()
{
    float h = ROOM_S / 2;
    lightsExterior();

    // grass
    useTex(texGrass);
    setMaterial(1.0f, 1.0f, 1.0f, 5.0f, 0.05f);
    glNormal3f(0, 1, 0);
    glBegin(GL_QUADS);
    tv(-30, -30, -90, -0.01f, -90);
    tv(-30,  30, -90, -0.01f,  90);
    tv( 30,  30,  90, -0.01f,  90);
    tv( 30, -30,  90, -0.01f, -90);
    glEnd();

    // stone path leading to the door
    useTex(texTile);
    setMaterial(0.9f, 0.9f, 0.9f, 20.0f, 0.2f);
    glBegin(GL_QUADS);
    tv(-0.65f, h / 2, -1.3f, 0.02f, h);
    tv(-0.65f, 20,    -1.3f, 0.02f, 40);
    tv( 0.65f, 20,     1.3f, 0.02f, 40);
    tv( 0.65f, h / 2,  1.3f, 0.02f, h);
    glEnd();

    // distant hill
    useTex(texGrass);
    setMaterial(0.70f, 1.0f, 0.70f, 5.0f, 0.05f);
    ell(75, -4, -25, 38, 13, 38);

    // trees around the museum
    tree( 12.0f, -2.0f, 1.2f);
    tree( 16.0f,  3.0f, 1.4f);
    tree( 10.0f, -9.0f, 1.1f);
    tree( 19.0f, -6.0f, 1.5f);
    tree(-11.0f, -6.0f, 1.2f);
    tree(-15.0f,  4.0f, 1.4f);
    tree( -9.0f, 12.0f, 1.2f);
    tree( -7.0f, 18.0f, 1.0f);
    tree(  8.0f, 14.0f, 1.2f);
    tree(  7.5f, 22.0f, 1.0f);
    tree( -5.0f,-14.0f, 1.3f);
    tree(  6.0f,-15.0f, 1.1f);

    // sun disc (visible through the window)
    glDisable(GL_LIGHTING);
    noTex();
    glColor3f(1.0f, 0.95f, 0.55f);
    ell(70, 12, -3, 3.5f, 3.5f, 3.5f);
    glEnable(GL_LIGHTING);
}

/* =====================================================================
   COMPLEX OBJECTS
   ===================================================================== */
void pillar(float x, float z)
{
    glPushMatrix();
    glTranslatef(x, 0, z);
    useTex(texMarble);
    setMaterial(0.95f, 0.95f, 0.92f, 70.0f, 0.5f);
    glPushMatrix(); glTranslatef(-0.3f, 0, -0.3f);      box(0.6f, 0.15f, 0.6f); glPopMatrix();
    glPushMatrix(); glTranslatef(0, 0.15f, 0);          cylY(0.22f, 0.18f, 3.7f, 24); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.3f, 3.85f, -0.3f);  box(0.6f, 0.15f, 0.6f); glPopMatrix();
    glPopMatrix();
}

void statue()
{
    glPushMatrix();
    glTranslatef(statX, 0, statZ);                           // object translation
    glScalef(statueScale, statueScale, statueScale);         // object scaling

    // stepped marble pedestal
    useTex(texMarble);
    setMaterial(0.95f, 0.95f, 0.92f, 90.0f, 0.6f);
    glPushMatrix(); glTranslatef(-0.55f, 0, -0.55f);       box(1.1f, 0.18f, 1.1f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.40f, 0.18f, -0.40f);   box(0.8f, 0.50f, 0.8f); glPopMatrix();

    // gold name plate
    noTex();
    setMaterial(0.90f, 0.75f, 0.15f, 90.0f, 0.9f);
    glPushMatrix(); glTranslatef(-0.20f, 0.28f, 0.40f);    box(0.4f, 0.15f, 0.02f); glPopMatrix();

    // the figure
    useTex(texMarble);
    setMaterial(0.90f, 0.90f, 0.88f, 100.0f, 0.7f);
    glPushMatrix(); glTranslatef(0, 0.68f, 0); cylY(0.38f, 0.24f, 0.50f, 28); glPopMatrix();   // robe
    ell(0.0f, 1.35f, 0.0f, 0.27f, 0.50f, 0.20f);                                              // torso
    ell(0.0f, 1.75f, 0.0f, 0.42f, 0.12f, 0.19f);                                              // shoulders
    glPushMatrix(); glTranslatef(0, 1.82f, 0); cylY(0.08f, 0.08f, 0.18f, 16); glPopMatrix();  // neck
    ell(0.0f, 2.15f, 0.0f, 0.19f, 0.23f, 0.19f);                                              // head
    for (int s = -1; s <= 1; s += 2)                                                          // raised arms
    {
        glPushMatrix();
        glTranslatef(s * 0.40f, 1.72f, 0);
        glRotatef(-s * 40.0f, 0, 0, 1);
        ell(0.0f, 0.32f, 0.0f, 0.09f, 0.34f, 0.09f);
        ell(0.0f, 0.68f, 0.0f, 0.11f, 0.11f, 0.11f);
        glPopMatrix();
    }
    glPopMatrix();
}

void chair(float x, float z, float ry)
{
    glPushMatrix();
    glTranslatef(x, 0, z);
    glRotatef(ry, 0, 1, 0);

    useTex(texWood);
    setMaterial(0.92f, 0.78f, 0.62f, 40.0f, 0.3f);
    glPushMatrix(); glTranslatef(-0.28f, 0.42f, -0.28f); box(0.56f, 0.06f, 0.56f); glPopMatrix();   // seat board
    float lx[4] = { -0.28f, 0.22f, -0.28f, 0.22f };
    float lz[4] = { -0.28f, -0.28f, 0.22f, 0.22f };
    for (int i = 0; i < 4; i++)                                                                     // legs
    {
        glPushMatrix(); glTranslatef(lx[i], 0, lz[i]); box(0.06f, 0.42f, 0.06f); glPopMatrix();
    }
    glPushMatrix(); glTranslatef(-0.28f, 0.48f, -0.31f); box(0.05f, 0.72f, 0.05f); glPopMatrix();   // back posts
    glPushMatrix(); glTranslatef( 0.23f, 0.48f, -0.31f); box(0.05f, 0.72f, 0.05f); glPopMatrix();

    noTex();
    setMaterial(0.65f, 0.08f, 0.10f, 60.0f, 0.5f);                                                  // red cushion + back
    glPushMatrix(); glTranslatef(-0.25f, 0.48f, -0.25f); box(0.50f, 0.05f, 0.50f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.23f, 0.62f, -0.30f); box(0.46f, 0.50f, 0.04f); glPopMatrix();
    glPopMatrix();
}

void table()
{
    glPushMatrix();
    glTranslatef(0, 0, -1.5f);
    useTex(texWood);
    setMaterial(0.92f, 0.78f, 0.62f, 50.0f, 0.4f);
    glPushMatrix(); glTranslatef(-0.9f, 0.72f, -0.5f); box(1.8f, 0.07f, 1.0f); glPopMatrix();
    float lx[4] = { -0.85f, 0.77f, -0.85f, 0.77f };
    float lz[4] = { -0.45f, -0.45f, 0.37f, 0.37f };
    for (int i = 0; i < 4; i++)
    {
        glPushMatrix(); glTranslatef(lx[i], 0, lz[i]); box(0.08f, 0.72f, 0.08f); glPopMatrix();
    }
    // vase
    noTex();
    setMaterial(0.20f, 0.35f, 0.85f, 120.0f, 0.9f);
    ell(0.0f, 0.95f, 0.0f, 0.13f, 0.17f, 0.13f);
    glPushMatrix(); glTranslatef(0, 1.07f, 0); cylY(0.05f, 0.09f, 0.14f, 16); glPopMatrix();
    glPopMatrix();
}

/* golden teapot on a pedestal, rotating for ever */
void exhibit()
{
    glPushMatrix();
    glTranslatef(3.6f, 0, -4.0f);
    useTex(texMarble);
    setMaterial(0.95f, 0.95f, 0.92f, 80.0f, 0.5f);
    glPushMatrix(); glTranslatef(-0.45f, 0, -0.45f);      box(0.9f, 0.12f, 0.9f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.35f, 0.12f, -0.35f);  box(0.7f, 0.88f, 0.7f); glPopMatrix();

    glPushMatrix();
    glTranslatef(0, 1.30f, 0);
    glRotatef(exhibitAngle, 0, 1, 0);                         // continuous rotation
    noTex();
    setMaterial(0.95f, 0.75f, 0.20f, 110.0f, 1.0f);
    glutSolidTeapot(0.30);
    glPopMatrix();

    glPushMatrix();                                           // counter-rotating ring
    glTranslatef(0, 1.30f, 0);
    glRotatef(-exhibitAngle * 1.6f, 1, 0.3f, 0);
    setMaterial(0.75f, 0.75f, 0.85f, 100.0f, 0.9f);
    glutSolidTorus(0.02, 0.55, 12, 40);
    glPopMatrix();
    glPopMatrix();
}

/* ceiling fan: rod + hub + 3 blades that spin about Y */
void fan()
{
    glPushMatrix();
    glTranslatef(0, ROOM_H, -1.5f);
    if (!shadowPass)
    {
        noTex();
        setMaterial(0.45f, 0.45f, 0.48f, 40.0f, 0.6f);
        glPushMatrix(); glTranslatef(-0.04f, -0.85f, -0.04f); box(0.08f, 0.85f, 0.08f); glPopMatrix();
        setMaterial(0.85f, 0.68f, 0.05f, 60.0f, 0.8f);
        glPushMatrix(); glTranslatef(0, -1.0f, 0); cylY(0.24f, 0.24f, 0.16f, 28); glPopMatrix();
    }
    glTranslatef(0, -0.92f, 0);
    for (int i = 0; i < 3; i++)
    {
        glPushMatrix();
        glRotatef(fanAngle + i * 120.0f, 0, 1, 0);           // continuous rotation
        glTranslatef(0.2f, 0, -0.15f);
        noTex();
        setMaterial(0.05f, 0.15f, 0.90f, 100.0f, 0.9f);
        box(1.6f, 0.03f, 0.3f);
        glPopMatrix();
    }
    glPopMatrix();
}

/* picture frame: gold frame + textured canvas, hung on a wall */
void painting(float x, float y, float z, float ry, GLuint tex, float scale)
{
    const float W = 1.8f, H = 1.3f, T = 0.07f, B = 0.10f;
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(ry, 0, 1, 0);
    glScalef(scale, scale, scale);

    noTex();
    setMaterial(0.75f, 0.58f, 0.12f, 60.0f, 0.9f);
    glPushMatrix(); glTranslatef(-W / 2, -H / 2, 0);        box(W, B, T); glPopMatrix();
    glPushMatrix(); glTranslatef(-W / 2, H / 2 - B, 0);     box(W, B, T); glPopMatrix();
    glPushMatrix(); glTranslatef(-W / 2, -H / 2, 0);        box(B, H, T); glPopMatrix();
    glPushMatrix(); glTranslatef(W / 2 - B, -H / 2, 0);     box(B, H, T); glPopMatrix();

    useTex(tex);
    setMaterial(1.0f, 1.0f, 1.0f, 10.0f, 0.1f);
    glNormal3f(0, 0, 1);
    glBegin(GL_QUADS);
    tv(0, 0, -W / 2 + B, -H / 2 + B, T * 0.6f);
    tv(1, 0,  W / 2 - B, -H / 2 + B, T * 0.6f);
    tv(1, 1,  W / 2 - B,  H / 2 - B, T * 0.6f);
    tv(0, 1, -W / 2 + B,  H / 2 - B, T * 0.6f);
    glEnd();
    glPopMatrix();
}

/* =====================================================================
   SHADOWS  (planar projection on the floor, drawn once per pixel with
   the stencil buffer, clipped to the room)
   ===================================================================== */
void shadowMatrix(GLfloat m[16], const GLfloat plane[4], const GLfloat light[4])
{
    GLfloat dot = plane[0] * light[0] + plane[1] * light[1] + plane[2] * light[2] + plane[3] * light[3];
    m[0]  = dot - light[0] * plane[0]; m[4]  = 0 - light[0] * plane[1]; m[8]  = 0 - light[0] * plane[2]; m[12] = 0 - light[0] * plane[3];
    m[1]  = 0 - light[1] * plane[0];   m[5]  = dot - light[1] * plane[1]; m[9]  = 0 - light[1] * plane[2]; m[13] = 0 - light[1] * plane[3];
    m[2]  = 0 - light[2] * plane[0];   m[6]  = 0 - light[2] * plane[1]; m[10] = dot - light[2] * plane[2]; m[14] = 0 - light[2] * plane[3];
    m[3]  = 0 - light[3] * plane[0];   m[7]  = 0 - light[3] * plane[1]; m[11] = 0 - light[3] * plane[2]; m[15] = dot - light[3] * plane[3];
}

void shadowCasters()
{
    statue();
    table();
    chair( 0.0f, -0.6f, 180);
    chair( 0.0f, -2.4f,   0);
    chair(-2.4f, -1.5f,  90);
    chair( 2.4f, -1.5f, -90);
    exhibit();
    fan();
    pillar(-5.3f, -5.3f);
    pillar( 5.3f, -5.3f);
    pillar(-5.3f,  5.3f);
    pillar( 5.3f,  5.3f);
}

void drawShadows()
{
    if (!light0On && !light1On) return;

    float h = ROOM_S / 2;
    GLfloat plane[4] = { 0, 1, 0, -0.012f };          // slightly above the floor
    GLfloat dir[4]   = { 0.5f, 1.0f, -0.3f, 0.0f };   // light coming through the window
    GLfloat sm[16];
    shadowMatrix(sm, plane, dir);

    GLdouble c0[4] = {  1, 0,  0, h };
    GLdouble c1[4] = { -1, 0,  0, h };
    GLdouble c2[4] = {  0, 0,  1, h };
    GLdouble c3[4] = {  0, 0, -1, h };
    glClipPlane(GL_CLIP_PLANE0, c0);
    glClipPlane(GL_CLIP_PLANE1, c1);
    glClipPlane(GL_CLIP_PLANE2, c2);
    glClipPlane(GL_CLIP_PLANE3, c3);
    glEnable(GL_CLIP_PLANE0); glEnable(GL_CLIP_PLANE1);
    glEnable(GL_CLIP_PLANE2); glEnable(GL_CLIP_PLANE3);

    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    glDepthMask(GL_FALSE);
    glColor4f(0.0f, 0.0f, 0.0f, 0.45f);

    shadowPass = true;
    glPushMatrix();
    glMultMatrixf(sm);
    shadowCasters();
    glPopMatrix();
    shadowPass = false;

    glDepthMask(GL_TRUE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CLIP_PLANE0); glDisable(GL_CLIP_PLANE1);
    glDisable(GL_CLIP_PLANE2); glDisable(GL_CLIP_PLANE3);
    glEnable(GL_LIGHTING);
}

/* =====================================================================
   WHOLE SCENE
   ===================================================================== */
void museum()
{
    float h = ROOM_S / 2;

    outdoors();                       // everything outside (sun light)

    lightsInterior();                 // everything inside (lamp + spot)
    roomFloor();
    roomCeiling();
    roomWalls();
    door();
    windowFrame();

    pillar(-5.3f, -5.3f);
    pillar( 5.3f, -5.3f);
    pillar(-5.3f,  5.3f);
    pillar( 5.3f,  5.3f);

    statue();
    table();
    chair( 0.0f, -0.6f, 180);         // 4 chairs around the table
    chair( 0.0f, -2.4f,   0);
    chair(-2.4f, -1.5f,  90);
    chair( 2.4f, -1.5f, -90);
    exhibit();
    fan();

    painting( 2.5f, 2.0f, -h + 0.01f,  0, texStars,  paintingScale);   // back wall (keyboard scale)
    painting(-h + 0.01f, 2.0f, -1.0f, 90, texMosaic, 1.0f);            // left wall (mouse colour)

    drawShadows();
    windowGlass();
}

/* light positions are given in the room's own coordinates, so the
   lights rotate/move together with the room */
void setLightPositions()
{
    glLightfv(GL_LIGHT0, GL_POSITION, lightPos0);

    GLfloat p1[4] = { statX, 3.8f, statZ + 1.0f, 1.0f };
    GLfloat d1[3] = { 0.0f, -0.937f, -0.347f };
    glLightfv(GL_LIGHT1, GL_POSITION, p1);
    glLightfv(GL_LIGHT1, GL_SPOT_DIRECTION, d1);

    GLfloat sun[4] = { 0.35f, 1.0f, 0.7f, 0.0f };
    glLightfv(GL_LIGHT2, GL_POSITION, sun);
}

/* =====================================================================
   CAMERA / COLLISION
   ===================================================================== */
bool blockedAt(double x, double z)
{
    const double h = ROOM_S / 2, m = 0.35;
    if (!(fabs(x) < h + WALL_T + m && fabs(z) < h + WALL_T + m)) return false;   // outside the building
    if (fabs(x) < h - m && fabs(z) < h - m) return false;                         // inside the room
    // in the wall zone: only the open door can be crossed
    if (z > h - m && fabs(x - DOOR_X) < DOOR_W * 0.5 - 0.2 && doorAngle > 50.0f) return false;
    return true;
}

void moveBy(double dx, double dz)
{
    double nx = ex + dx, nz = ez + dz;
    if (!blockedAt(nx, nz))      { ex = nx; ez = nz; }
    else if (!blockedAt(nx, ez))   ex = nx;
    else if (!blockedAt(ex, nz))   ez = nz;
    if (ex >  60) ex =  60;
    if (ex < -60) ex = -60;
    if (ez >  60) ez =  60;
    if (ez < -60) ez = -60;
}

/* =====================================================================
   GLUT CALLBACKS
   ===================================================================== */
static void resize(int width, int height)
{
    if (height == 0) height = 1;
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(60.0, (double)width / (double)height, 0.2, 400.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void display(void)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    drawSkyGradient();

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // VIEWING coordinate transformation (camera)
    double dx = cos(pitch) * sin(yaw), dy = sin(pitch), dz = -cos(pitch) * cos(yaw);
    gluLookAt(ex, ey, ez, ex + dx, ey + dy, ez + dz, 0, 1, 0);

    // OBJECT coordinate transformation: rotate the whole scene about its centre
    glPushMatrix();
    glTranslated(0, 2, 0);
    glRotatef(degreeX, 1, 0, 0);
    glRotatef(degreeY, 0, 1, 0);
    glRotatef(degreeZ, 0, 0, 1);
    glTranslated(0, -2, 0);

    setLightPositions();
    museum();
    glPopMatrix();

    drawHUD();
    glutSwapBuffers();
}

/* ~60 frames per second: fan, exhibit and door animation */
static void timer(int v)
{
    fanAngle += fanSpeed;
    if (fanAngle > 360) fanAngle -= 360;
    exhibitAngle += 1.0f;
    if (exhibitAngle > 360) exhibitAngle -= 360;

    if (doorAngle < doorTarget)      doorAngle = mn(doorAngle + 1.5f, doorTarget);
    else if (doorAngle > doorTarget) doorAngle = mx(doorAngle - 1.5f, doorTarget);

    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

static void key(unsigned char k, int x, int y)
{
    switch (k)
    {
        case 27: case 'q': exit(0); break;

        // rotate the whole scene
        case 'a': degreeX += 2.5f; break;
        case 'b': degreeX -= 2.5f; break;
        case 'c': degreeY += 2.5f; break;
        case 'd': degreeY -= 2.5f; break;
        case 'e': degreeZ += 2.5f; break;
        case 'f': degreeZ -= 2.5f; break;

        // move the camera along the world axes
        case 'x': moveBy( 0.3, 0); break;
        case 'X': moveBy(-0.3, 0); break;
        case 'y': ey += 0.3; if (ey > 3.6) ey = 3.6; break;
        case 'Y': ey -= 0.3; if (ey < 0.4) ey = 0.4; break;
        case 'z': moveBy(0, -0.4); break;
        case 'Z': moveBy(0,  0.4); break;

        case 'r':
            ex = START_X; ey = START_Y; ez = START_Z;
            yaw = 0; pitch = 0;
            degreeX = degreeY = degreeZ = 0;
            break;

        // scale
        case 'k': statueScale = mn(1.5f, statueScale + 0.05f); break;
        case 'l': statueScale = mx(0.3f, statueScale - 0.05f); break;
        case 'n': paintingScale += 0.05f; break;
        case 'm': paintingScale = mx(0.4f, paintingScale - 0.05f); break;

        // translate the statue
        case 'g': statX = mn( 4.5f, statX + 0.2f); break;
        case 'G': statX = mx(-4.5f, statX - 0.2f); break;
        case 'v': statZ = mn( 4.5f, statZ + 0.2f); break;
        case 'V': statZ = mx(-4.5f, statZ - 0.2f); break;

        // fan speed
        case '+': case '=': fanSpeed += 0.5f; break;
        case '-': fanSpeed = mx(0.0f, fanSpeed - 0.5f); break;

        // door
        case 'o': doorTarget = (doorTarget == 0) ? 90.0f : 0.0f; break;

        // lights
        case '0': light0On = false; break;
        case '1': light0On = true;  break;
        case '8': light1On = false; break;
        case '7': light1On = true;  break;
        case '2': sunOn = false; break;
        case '3': sunOn = true;  break;
    }
    glutPostRedisplay();
}

static void specialKey(int k, int x, int y)
{
    double fx = sin(yaw), fz = -cos(yaw);
    switch (k)
    {
        case GLUT_KEY_UP:        moveBy( fx * 0.35,  fz * 0.35); break;
        case GLUT_KEY_DOWN:      moveBy(-fx * 0.35, -fz * 0.35); break;
        case GLUT_KEY_LEFT:      yaw -= 0.06; break;
        case GLUT_KEY_RIGHT:     yaw += 0.06; break;
        case GLUT_KEY_PAGE_UP:   pitch += 0.05; if (pitch >  1.2) pitch =  1.2; break;
        case GLUT_KEY_PAGE_DOWN: pitch -= 0.05; if (pitch < -1.2) pitch = -1.2; break;
    }
    glutPostRedisplay();
}

static void mouse(int button, int state, int x, int y)
{
    if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN)
    {
        paintingColorIdx = (paintingColorIdx + 1) % 3;      // change colours of the left painting
        buildMosaic(paintingColorIdx);
        glutPostRedisplay();
    }
    if (button == GLUT_RIGHT_BUTTON)
    {
        dragging = (state == GLUT_DOWN);
        lastMX = x; lastMY = y;
    }
}

static void motion(int x, int y)
{
    if (!dragging) return;
    yaw   += (x - lastMX) * 0.005;
    pitch -= (y - lastMY) * 0.005;
    if (pitch >  1.2) pitch =  1.2;
    if (pitch < -1.2) pitch = -1.2;
    lastMX = x; lastMY = y;
    glutPostRedisplay();
}

/* =====================================================================
   LIGHTING : 3 light sources
   ===================================================================== */
void initLighting()
{
    glEnable(GL_LIGHTING);
    GLfloat gAmb[] = { 0.28f, 0.28f, 0.30f, 1.0f };
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, gAmb);

    // LIGHT0 : white point light (ceiling lamp)
    GLfloat a0[] = { 0.30f, 0.30f, 0.30f, 1.0f };
    GLfloat d0[] = { 1.00f, 1.00f, 0.95f, 1.0f };
    GLfloat s0[] = { 0.80f, 0.80f, 0.80f, 1.0f };
    glLightfv(GL_LIGHT0, GL_AMBIENT,  a0);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  d0);
    glLightfv(GL_LIGHT0, GL_SPECULAR, s0);
    glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION,  1.0f);
    glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION,    0.03f);
    glLightf(GL_LIGHT0, GL_QUADRATIC_ATTENUATION, 0.004f);

    // LIGHT1 : yellow spot light above the statue
    GLfloat a1[] = { 0.10f, 0.10f, 0.00f, 1.0f };
    GLfloat d1[] = { 1.00f, 0.90f, 0.50f, 1.0f };
    GLfloat s1[] = { 1.00f, 1.00f, 0.30f, 1.0f };
    glLightfv(GL_LIGHT1, GL_AMBIENT,  a1);
    glLightfv(GL_LIGHT1, GL_DIFFUSE,  d1);
    glLightfv(GL_LIGHT1, GL_SPECULAR, s1);
    glLightf(GL_LIGHT1, GL_SPOT_CUTOFF,   38.0f);
    glLightf(GL_LIGHT1, GL_SPOT_EXPONENT,  8.0f);
    glLightf(GL_LIGHT1, GL_CONSTANT_ATTENUATION,  1.0f);
    glLightf(GL_LIGHT1, GL_LINEAR_ATTENUATION,    0.03f);
    glLightf(GL_LIGHT1, GL_QUADRATIC_ATTENUATION, 0.005f);

    // LIGHT2 : sun (directional) for the outdoors
    GLfloat a2[] = { 0.25f, 0.25f, 0.30f, 1.0f };
    GLfloat d2[] = { 0.80f, 0.76f, 0.66f, 1.0f };
    GLfloat s2[] = { 0.30f, 0.30f, 0.30f, 1.0f };
    glLightfv(GL_LIGHT2, GL_AMBIENT,  a2);
    glLightfv(GL_LIGHT2, GL_DIFFUSE,  d2);
    glLightfv(GL_LIGHT2, GL_SPECULAR, s2);
}

int main(int argc, char *argv[])
{
    glutInit(&argc, argv);
    glutInitWindowSize(1000, 680);
    glutInitWindowPosition(10, 10);
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH | GLUT_STENCIL);
    glutCreateWindow("3D Museum - OpenGL/GLUT");

    glutReshapeFunc(resize);
    glutDisplayFunc(display);
    glutKeyboardFunc(key);
    glutSpecialFunc(specialKey);
    glutMouseFunc(mouse);
    glutMotionFunc(motion);
    glutTimerFunc(16, timer, 0);

    glClearColor(0.5f, 0.7f, 0.95f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_NORMALIZE);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    gq = gluNewQuadric();
    gluQuadricNormals(gq, GLU_SMOOTH);
    gluQuadricTexture(gq, GL_TRUE);

    buildTextures();
    initLighting();

    printf("3D MUSEUM - CONTROLS\n");
    printf(" Up/Down arrows : walk      Left/Right : turn      PgUp/PgDn : look up/down\n");
    printf(" right-mouse drag : look around\n");
    printf(" x/X y/Y z/Z : move camera along X/Y/Z      r : reset camera\n");
    printf(" o : open / close door\n");
    printf(" a/b c/d e/f : rotate whole scene about X/Y/Z\n");
    printf(" k/l : scale statue    n/m : scale back painting    g/G v/V : move statue\n");
    printf(" +/- : fan speed\n");
    printf(" 0/1 lamp off/on    8/7 spot off/on    2/3 sun off/on\n");
    printf(" left mouse click : change colours of the left painting\n");

    glutMainLoop();
    return EXIT_SUCCESS;
}
