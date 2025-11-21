#include <GL/glut.h>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif

#define MAX_BULLETS 60
#define MAX_PARTICLES 100
#define MAX_ENEMIES 4

// Window
int WIN_W = 1024, WIN_H = 768;
bool cursorCaptured = true;
bool ignoreWarp = false;

// Timing
float lastTime = 0.0f;

// Camera
struct Vec3 { float x, y, z; };
Vec3 camPos = { 0.0f, 1.6f, 3.0f };
float yaw = -90.0f, pitch = 0.0f;
Vec3 camFront = { 0.0f, 0.0f, -1.0f };
Vec3 camUp = { 0.0f, 1.0f, 0.0f };
Vec3 camRight = { 1.0f, 0.0f, 0.0f };
bool keyDown[256];
float moveSpeed = 5.0f;
float mouseSensitivity = 0.12f;
float verticalVelocity = 0.0f;
bool onGround = true;

// HUD & Player
int score = 0;
int bulletsLeft = 30;
int playerHealth = 100;
bool reloading = false;
float reloadTimer = 0.0f;
const float reloadTime = 1.5f;
bool justFired = false;
float damageFlash = 0.0f;
bool gameOver = false;   // ✅ track game-over state

// Environment static props
struct Rock { Vec3 pos; float scale; };
Rock rocks[30];
bool envInitialized = false;

// Systems
struct Enemy {
    Vec3 pos;
    Vec3 dir;
    float size;
    float health;
    bool active;
    float flashTimer;

    // AI
    float moveSpeed;
    float shootCooldown;
    const float maxShootCooldown = 2.0f;
    bool canSeePlayer;
    float lastSeenTime;
    Vec3 lastSeenPos;

    // Death & respawn
    float deathTimer; // >0 = dead, counting down to respawn
};

Enemy enemies[MAX_ENEMIES];

struct Bullet {
    Vec3 pos;
    Vec3 dir;
    bool active;
    float life;
    int owner; // 0 = player, 1 = enemy
};
Bullet bullets[MAX_BULLETS];

struct Particle {
    Vec3 pos;
    Vec3 vel;
    float life;
    bool active;
};
Particle particles[MAX_PARTICLES];

static GLuint skyTex = 0;

// ---------------------- SOUND HELPERS -------------------------
void playShootSound() {
#ifdef _WIN32
    Beep(1200, 40); // high, short
#endif
}

void playReloadSound() {
#ifdef _WIN32
    Beep(800, 70); // mid, a bit longer
#endif
}

void playHitSound() {
#ifdef _WIN32
    Beep(600, 80); // lower, short
#endif
}

void playGameOverSound() {
#ifdef _WIN32
    Beep(400, 200); // distinct, longer
#endif
}
// --------------------------------------------------------------

// Utility
float nowSeconds() { return glutGet(GLUT_ELAPSED_TIME) * 0.001f; }
void vecNormalize(Vec3& v) { float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); if (l > 1e-6f) { v.x /= l; v.y /= l; v.z /= l; } }
Vec3 vecCross(const Vec3& a, const Vec3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
Vec3 vecScale(const Vec3& a, float s) { return { a.x * s, a.y * s, a.z * s }; }
Vec3 vecAdd(const Vec3& a, const Vec3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
Vec3 vecSub(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }

// Camera
void updateCameraVectors() {
    float yr = yaw * 3.14159265f / 180.0f;
    float pr = pitch * 3.14159265f / 180.0f;
    Vec3 f{ cosf(yr) * cosf(pr), sinf(pr), sinf(yr) * cosf(pr) };
    vecNormalize(f); camFront = f;
    camRight = vecCross(camFront, Vec3{ 0,1,0 }); vecNormalize(camRight);
    camUp = vecCross(camRight, camFront); vecNormalize(camUp);
}
void applyView() {
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(camPos.x, camPos.y, camPos.z,
        camPos.x + camFront.x, camPos.y + camFront.y, camPos.z + camFront.z,
        camUp.x, camUp.y, camUp.z);
}

// Sky texture
void makeSkyTexture() {
    const int TEX_SIZE = 256;
    static bool done = false;
    if (done) return;
    unsigned char pixels[TEX_SIZE][TEX_SIZE][3];
    for (int y = 0; y < TEX_SIZE; ++y) {
        float t = y / (float)(TEX_SIZE - 1);
        float r, g, b;
        if (t < 0.5f) {
            float s = t * 2.0f;
            r = 0.1f + 0.3f * s;
            g = 0.2f + 0.4f * s;
            b = 0.5f + 0.4f * s;
        }
        else {
            float s = (t - 0.5f) * 2.0f;
            r = 0.4f + 0.5f * s;
            g = 0.6f + 0.3f * s;
            b = 0.9f + 0.05f * s;
        }
        unsigned char R = (unsigned char)(r * 255);
        unsigned char G = (unsigned char)(g * 255);
        unsigned char B = (unsigned char)(b * 255);
        for (int x = 0; x < TEX_SIZE; ++x) {
            pixels[y][x][0] = R;
            pixels[y][x][1] = G;
            pixels[y][x][2] = B;
        }
    }
    glGenTextures(1, &skyTex);
    glBindTexture(GL_TEXTURE_2D, skyTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TEX_SIZE, TEX_SIZE, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    done = true;
}

// Terrain height
float terrainHeight(float x, float z) {
    return 2.0f + 1.5f * sinf(x * 0.05f) * cosf(z * 0.07f) + 0.8f * sinf((x + z) * 0.1f);
}

// Draw helpers
void drawRock(const Vec3& pos, float scale) {
    glColor3f(0.35f, 0.30f, 0.25f);
    glPushMatrix();
    glTranslatef(pos.x, pos.y, pos.z);
    glScalef(scale, scale * 0.7f, scale);
    glutSolidSphere(1.0f, 8, 8);
    glPopMatrix();
}

void drawGun() {
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glTranslatef(0.3f, -0.2f, -0.5f);
    glRotatef(-5.0f, 1, 0, 0);
    glRotatef(8.0f, 0, 0, 1);

    // Barrel — using glutSolidCone (safe for all GLUT)
    glColor3f(0.15f, 0.15f, 0.15f);
    glPushMatrix();
    glTranslatef(0, 0, -0.6f);
    glRotatef(90, 1, 0, 0); // point along Z
    glutSolidCone(0.06f, 0.8f, 8, 4); // ✅ safe
    glPopMatrix();

    // Body
    glPushMatrix();
    glTranslatef(0.0f, -0.15f, -0.3f);
    glScalef(0.1f, 0.3f, 0.4f);
    glutSolidCube(1.0f);
    glPopMatrix();

    // Stock
    glColor3f(0.2f, 0.15f, 0.1f);
    glPushMatrix();
    glTranslatef(0.0f, -0.05f, 0.1f);
    glScalef(0.08f, 0.1f, 0.3f);
    glutSolidCube(1.0f);
    glPopMatrix();

    // Magazine
    glColor3f(0.1f, 0.1f, 0.1f);
    glPushMatrix();
    glTranslatef(0.0f, -0.3f, -0.3f);
    glScalef(0.06f, 0.2f, 0.1f);
    glutSolidCube(1.0f);
    glPopMatrix();

    // Muzzle flash
    static float lastFireTime = -10.0f;
    if (justFired) {
        lastFireTime = lastTime;
        justFired = false;
    }
    if (lastTime - lastFireTime < 0.08f) {
        float alpha = 1.0f - (lastTime - lastFireTime) / 0.08f;
        glColor4f(1.0f, 0.7f, 0.2f, alpha * 0.9f);
        glPushMatrix();
        glTranslatef(0, 0, -0.95f);
        float size = 0.1f + 0.2f * sinf(lastTime * 200.0f);
        glutSolidSphere(size, 6, 6);
        glPopMatrix();
    }

    glPopMatrix();
}

// Draw
void drawSkydome() {
    const float sunAngle = 0.8f;
    float sunX = cosf(sunAngle) * 120.0f;
    float sunY = 80.0f + sinf(sunAngle) * 60.0f;
    float sunZ = sinf(sunAngle) * 120.0f;

    glPushMatrix();
    glTranslatef(camPos.x, 0, camPos.z);
    glDisable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    makeSkyTexture();
    glBindTexture(GL_TEXTURE_2D, skyTex);
    glColor3f(1, 1, 1);

    const int seg = 32;
    const float R = 150.0f;
    for (int i = 0; i < seg; ++i) {
        float theta1 = (i / float(seg)) * 3.14159f * 0.5f;
        float theta2 = ((i + 1) / float(seg)) * 3.14159f * 0.5f;
        glBegin(GL_TRIANGLE_STRIP);
        for (int j = 0; j <= seg * 2; ++j) {
            float phi = (j / float(seg * 2)) * 6.28318f;
            for (int t = 0; t < 2; ++t) {
                float th = (t == 0 ? theta1 : theta2);
                float x = R * cosf(th) * cosf(phi);
                float y = R * sinf(th);
                float z = R * cosf(th) * sinf(phi);
                float v = th / (3.14159f * 0.5f);
                float u = phi / 6.28318f;
                glTexCoord2f(u, v);
                glVertex3f(x, y, z);
            }
        }
        glEnd();
    }

    glColor3f(1, 0.95f, 0.7f);
    glPushMatrix();
    glTranslatef(sunX, sunY, sunZ);
    glutSolidSphere(6.0f, 16, 16);
    glPopMatrix();

    glDisable(GL_TEXTURE_2D);
    glEnable(GL_LIGHTING);
    glPopMatrix();
}

void drawFloor() {
    const float step = 2.0f;
    const float half = 100.0f;

    glDisable(GL_LIGHTING);
    glColor3f(0.2f, 0.5f, 0.2f);

    glBegin(GL_TRIANGLES);
    for (float x = -half; x < half; x += step) {
        for (float z = -half; z < half; z += step) {
            float h00 = terrainHeight(x, z);
            float h10 = terrainHeight(x + step, z);
            float h01 = terrainHeight(x, z + step);
            float h11 = terrainHeight(x + step, z + step);

            glVertex3f(x, h00, z);
            glVertex3f(x + step, h10, z);
            glVertex3f(x, h01, z + step);

            glVertex3f(x + step, h10, z);
            glVertex3f(x + step, h11, z + step);
            glVertex3f(x, h01, z + step);
        }
    }
    glEnd();
    glEnable(GL_LIGHTING);
}

void drawEnvironment() {
    for (int i = 0; i < 30; ++i) {
        drawRock(rocks[i].pos, rocks[i].scale);
    }

    // Trees
    glColor3f(0.4f, 0.25f, 0.1f);
    glPushMatrix(); glTranslatef(-8.0f, terrainHeight(-8, 12) + 1.0f, 12.0f); glRotatef(-90, 1, 0, 0); glutSolidCone(0.4f, 3.0f, 8, 8); glPopMatrix();
    glColor3f(0.1f, 0.5f, 0.1f);
    glPushMatrix(); glTranslatef(-8.0f, terrainHeight(-8, 12) + 3.0f, 12.0f); glutSolidCone(1.8f, 3.5f, 8, 8); glPopMatrix();

    glColor3f(0.4f, 0.25f, 0.1f);
    glPushMatrix(); glTranslatef(15.0f, terrainHeight(15, 5) + 1.0f, 5.0f); glRotatef(-90, 1, 0, 0); glutSolidCone(0.4f, 3.0f, 8, 8); glPopMatrix();
    glColor3f(0.1f, 0.5f, 0.1f);
    glPushMatrix(); glTranslatef(15.0f, terrainHeight(15, 5) + 3.0f, 5.0f); glutSolidCone(1.8f, 3.5f, 8, 8); glPopMatrix();

    // Crates
    glColor3f(0.7f, 0.3f, 0.2f);
    glPushMatrix(); glTranslatef(2, terrainHeight(2, -2) + 0.5f, -2); glutSolidCube(1); glPopMatrix();
    glPushMatrix(); glTranslatef(-3, terrainHeight(-3, 4) + 0.5f, 4); glutSolidCube(1); glPopMatrix();

    // Fence
    glColor3f(0.3f, 0.3f, 0.3f);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    for (float x = -15; x <= 15; x += 0.5f) {
        float h = terrainHeight(x, -12);
        glVertex3f(x, h + 1.0f, -12);
        glVertex3f(x, h + 1.5f, -12);
    }
    glEnd();
    glColor3f(0.2f, 0.15f, 0.1f);
    for (float x = -15; x <= 15; x += 5.0f) {
        float h = terrainHeight(x, -12);
        glPushMatrix();
        glTranslatef(x, h + 0.8f, -12);
        glScalef(0.3f, 1.2f, 0.3f);
        glutSolidCube(1);
        glPopMatrix();
    }

    // Building
    glColor3f(0.55f, 0.55f, 0.55f);
    glPushMatrix(); glTranslatef(12.0f, terrainHeight(12, -8) + 4.0f, -8.0f); glScalef(6.0f, 8.0f, 6.0f); glutSolidCube(1); glPopMatrix();
    glColor3f(0.2f, 0.15f, 0.1f);
    glPushMatrix(); glTranslatef(12.0f, terrainHeight(12, -8) + 1.0f, -5.0f); glScalef(1.2f, 2.0f, 0.1f); glutSolidCube(1); glPopMatrix();
}

void drawEnemy(Enemy& e) {
    if (e.deathTimer > 0.0f) return; // ✅ skip dead enemies

    glPushMatrix();
    glTranslatef(e.pos.x, e.pos.y, e.pos.z);

    if (e.flashTimer > 0.0f) {
        float f = (sinf(e.flashTimer * 50.0f) + 1.0f) * 0.5f;
        glColor3f(1.0f, f * 0.5f, f * 0.5f);
    }
    else {
        glColor3f(0.8f, 0.3f, 0.3f);
    }

    // Head
    glPushMatrix();
    glTranslatef(0, 1.5f, 0);
    glutSolidSphere(0.2f, 8, 8);
    glPopMatrix();

    // Body
    glColor3f(0.2f, 0.2f, 0.6f);
    glPushMatrix();
    glTranslatef(0, 0.9f, 0);
    glScalef(0.4f, 0.8f, 0.3f);
    glutSolidCube(1.0f);
    glPopMatrix();

    // Arms
    glColor3f(0.8f, 0.3f, 0.3f);
    glPushMatrix();
    glTranslatef(0.3f, 1.1f, 0);
    glScalef(0.2f, 0.6f, 0.2f);
    glutSolidCube(1.0f);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(-0.3f, 1.1f, 0);
    glScalef(0.2f, 0.6f, 0.2f);
    glutSolidCube(1.0f);
    glPopMatrix();

    // Legs
    glColor3f(0.1f, 0.1f, 0.4f);
    glPushMatrix();
    glTranslatef(0.15f, 0.3f, 0);
    glScalef(0.2f, 0.6f, 0.2f);
    glutSolidCube(1.0f);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(-0.15f, 0.3f, 0);
    glScalef(0.2f, 0.6f, 0.2f);
    glutSolidCube(1.0f);
    glPopMatrix();

    // Gun
    glColor3f(0.1f, 0.1f, 0.1f);
    glPushMatrix();
    glTranslatef(0.4f, 1.0f, 0);
    glRotatef(-20, 0, 0, 1);
    glScalef(0.05f, 0.3f, 0.05f);
    glutSolidCube(1.0f);
    glPopMatrix();

    glPopMatrix();
}

void drawBullet(Bullet& b) {
    glPushMatrix();
    glTranslatef(b.pos.x, b.pos.y, b.pos.z);
    glColor3f(b.owner == 0 ? 1.0f : 1.0f, b.owner == 0 ? 1.0f : 0.3f, b.owner == 0 ? 0.0f : 0.3f);
    glutSolidSphere(0.05f, 6, 6);
    glPopMatrix();
}

void drawParticle(Particle& p) {
    glPushMatrix();
    glTranslatef(p.pos.x, p.pos.y, p.pos.z);
    glColor4f(1.0f, 0.5f, 0.0f, p.life > 0.2f ? 1.0f : p.life * 5.0f);
    glutSolidSphere(0.05f + p.life * 0.1f, 4, 4);
    glPopMatrix();
}

void drawHUD() {
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); gluOrtho2D(0, WIN_W, 0, WIN_H);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity(); glDisable(GL_DEPTH_TEST);

    // Crosshair
    float cx = WIN_W * 0.5f, cy = WIN_H * 0.5f, len = 10.0f;
    glColor3f(1, 1, 1); glLineWidth(2.0f);
    glBegin(GL_LINES);
    glVertex2f(cx - len, cy); glVertex2f(cx + len, cy);
    glVertex2f(cx, cy - len); glVertex2f(cx, cy + len);
    glEnd();

    // Text
    char buf[128];
    glColor3f(1, 1, 1);
    sprintf(buf, "Score: %d", score);
    glRasterPos2f(10, WIN_H - 20);
    for (char* p = buf; *p; p++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *p);

    if (reloading) strcpy(buf, "Reloading...");
    else sprintf(buf, "Ammo: %d/30", bulletsLeft);
    glRasterPos2f(10, WIN_H - 40);
    for (char* p = buf; *p; p++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *p);

    sprintf(buf, "Health: %d", playerHealth);
    glRasterPos2f(10, WIN_H - 60);
    for (char* p = buf; *p; p++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *p);

    // ✅ Game Over overlay text
    if (gameOver) {
        const char* msg1 = "GAME OVER";
        const char* msg2 = "Better Luck Next Time!";
        const char* msg3 = "Press ESC to release mouse";

        float centerX = WIN_W * 0.5f;
        float centerY = WIN_H * 0.5f;

        glColor3f(1.0f, 0.2f, 0.2f);
        // Centered simple text: move raster pos back by approx. 9px per character
        glRasterPos2f(centerX - strlen(msg1) * 9.0f * 0.5f, centerY + 20);
        for (const char* p = msg1; *p; ++p)
            glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *p);

        glColor3f(1.0f, 1.0f, 1.0f);
        glRasterPos2f(centerX - strlen(msg2) * 9.0f * 0.5f, centerY - 5);
        for (const char* p = msg2; *p; ++p)
            glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *p);

        glColor3f(0.8f, 0.8f, 0.8f);
        glRasterPos2f(centerX - strlen(msg3) * 7.0f * 0.5f, centerY - 30);
        for (const char* p = msg3; *p; ++p)
            glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *p);
    }

    glEnable(GL_DEPTH_TEST);
    glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
}

// Helpers
bool canSee(const Vec3& from, const Vec3& to) {
    Vec3 diff = vecSub(to, from);
    float distSq = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;
    if (distSq > 625.0f) return false; // 25^2

    float dy = to.y - from.y;
    float horizDist = sqrtf(diff.x * diff.x + diff.z * diff.z);
    if (horizDist < 0.1f) return true;
    float pitchToPlayer = fabs(atan2f(dy, horizDist)) * 180.0f / 3.14159f;
    return pitchToPlayer < 60.0f;
}

void fireBullet() {
    if (reloading || bulletsLeft <= 0 || gameOver) return; // ✅ no shooting after game over
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) {
            bullets[i].active = true;
            bullets[i].pos = camPos;
            bullets[i].dir = camFront;
            bullets[i].life = 3.0f;
            bullets[i].owner = 0;
            bulletsLeft--;
            justFired = true;
            playShootSound();        // ✅ sound on shoot
            break;
        }
    }
}

void spawnParticle(const Vec3& pos) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].active) {
            particles[i].active = true;
            particles[i].pos = pos;
            particles[i].vel = {
                ((rand() % 100) / 50.0f - 1.0f) * 1.5f,
                ((rand() % 100) / 50.0f + 0.5f) * 1.5f,
                ((rand() % 100) / 50.0f - 1.0f) * 1.5f
            };
            particles[i].life = 0.8f + ((rand() % 100) / 200.0f);
            break;
        }
    }
}

void initEnemies() {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemies[i].active = true;
        enemies[i].pos = {
            ((rand() % 40) - 20) * 0.8f,
            0,
            ((rand() % 40) - 20) * 0.8f
        };
        enemies[i].pos.y = terrainHeight(enemies[i].pos.x, enemies[i].pos.z) + 1.6f; // ✅ +1.6f = player height
        float ang = (rand() % 360) * 3.14159f / 180.0f;
        enemies[i].dir = { cosf(ang), 0, sinf(ang) };
        enemies[i].health = 100.0f;
        enemies[i].size = 0.4f;
        enemies[i].flashTimer = 0.0f;
        enemies[i].moveSpeed = 0.8f;
        enemies[i].shootCooldown = (rand() % 1000) / 500.0f;
        enemies[i].canSeePlayer = false;
        enemies[i].lastSeenTime = 0.0f;
        enemies[i].deathTimer = 0.0f; // alive
    }
}

void initEnvironment() {
    if (envInitialized) return;
    for (int i = 0; i < 30; ++i) {
        float x, z;
        do {
            x = -80 + (rand() % 160);
            z = -80 + (rand() % 160);
        } while (x * x + z * z < 100);
        float h = terrainHeight(x, z);
        rocks[i].pos = { x, h + 0.5f, z };
        rocks[i].scale = 0.6f + (rand() % 40) / 100.0f;
    }
    envInitialized = true;
}

// GLUT callbacks
void reshape(int w, int h) { WIN_W = w; WIN_H = h; glViewport(0, 0, w, h); }
void passiveMouse(int x, int y) {
    if (!cursorCaptured || gameOver) return;  // ✅ do not rotate camera after game over
    if (ignoreWarp) { ignoreWarp = false; return; }
    int cx = WIN_W / 2, cy = WIN_H / 2;
    float dx = float(x - cx), dy = float(cy - y);
    yaw += dx * mouseSensitivity; pitch += dy * mouseSensitivity;
    if (pitch > 89) pitch = 89; if (pitch < -89) pitch = -89;
    updateCameraVectors();
    ignoreWarp = true; glutWarpPointer(cx, cy);
}
void keyboardDown(unsigned char key, int x, int y) {
    keyDown[key] = true;
    if (key == 27) {
        cursorCaptured = !cursorCaptured;
        if (cursorCaptured) { glutSetCursor(GLUT_CURSOR_NONE); int cx = WIN_W / 2, cy = WIN_H / 2; ignoreWarp = true; glutWarpPointer(cx, cy); }
        else glutSetCursor(GLUT_CURSOR_INHERIT);
    }
    if (key == 'r' || key == 'R') {
        if (!reloading && bulletsLeft < 30 && !gameOver) {
            reloading = true;
            reloadTimer = reloadTime;
            playReloadSound();       // ✅ sound on reload
        }
    }
}
void keyboardUp(unsigned char key, int x, int y) { keyDown[key] = false; }
void specialDown(int key, int x, int y) { (void)key; (void)x; (void)y; }
void mouseClick(int button, int state, int x, int y) {
    if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) fireBullet();
}

// Display
void display() {
    float t = nowSeconds(), dt = (lastTime == 0 ? 0.016f : t - lastTime); lastTime = t;

    // Stop game simulation after game over (but still render)
    if (!gameOver) {
        // Camera movement
        float speed = moveSpeed * dt;
        if (keyDown['w'] || keyDown['W']) camPos = vecAdd(camPos, vecScale(camFront, speed));
        if (keyDown['s'] || keyDown['S']) camPos = vecSub(camPos, vecScale(camFront, speed));
        if (keyDown['a'] || keyDown['A']) camPos = vecSub(camPos, vecScale(camRight, speed));
        if (keyDown['d'] || keyDown['D']) camPos = vecAdd(camPos, vecScale(camRight, speed));
        int mod = glutGetModifiers(); moveSpeed = (mod & GLUT_ACTIVE_SHIFT) ? 9.0f : 5.0f;

        // Jump & gravity
        if (keyDown[' ']) {
            if (onGround) {
                verticalVelocity = 5.0f;
                onGround = false;
            }
        }
        verticalVelocity -= 9.81f * dt;
        camPos.y += verticalVelocity * dt;
        float groundY = terrainHeight(camPos.x, camPos.z) + 1.6f;
        if (camPos.y <= groundY) {
            camPos.y = groundY;
            verticalVelocity = 0;
            onGround = true;
        }

        // Reload
        if (reloading) {
            reloadTimer -= dt;
            if (reloadTimer <= 0) {
                reloading = false;
                bulletsLeft = 30;
            }
        }

        // Bullets
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (bullets[i].active) {
                bullets[i].pos = vecAdd(bullets[i].pos, vecScale(bullets[i].dir, 15.0f * dt));
                bullets[i].life -= dt;
                if (bullets[i].life <= 0) bullets[i].active = false;
            }
        }

        // Particles
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (particles[i].active) {
                particles[i].pos = vecAdd(particles[i].pos, vecScale(particles[i].vel, dt));
                particles[i].vel.y -= 2.0f * dt;
                particles[i].life -= dt;
                if (particles[i].life <= 0) particles[i].active = false;
            }
        }

        // Enemies AI & Update
        for (int i = 0; i < MAX_ENEMIES; i++) {
            Enemy& e = enemies[i];
            if (!e.active || e.deathTimer > 0.0f) continue;

            if (e.flashTimer > 0) e.flashTimer -= dt;

            // Vision
            e.canSeePlayer = canSee(e.pos, camPos);
            if (e.canSeePlayer) {
                e.lastSeenTime = t;
                e.lastSeenPos = camPos;
            }

            // Turn/move toward last seen
            if (t - e.lastSeenTime < 3.0f) {
                Vec3 toTarget = vecSub(e.lastSeenPos, e.pos);
                toTarget.y = 0;
                float len = sqrtf(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
                if (len > 0.1f) {
                    toTarget.x /= len;
                    toTarget.z /= len;
                    e.dir.x = e.dir.x * 0.94f + toTarget.x * 0.06f;
                    e.dir.z = e.dir.z * 0.94f + toTarget.z * 0.06f;
                    vecNormalize(e.dir);
                }
            }
            else {
                if ((rand() % 200) == 0) {
                    float ang = (rand() % 360) * 3.14159f / 180.0f;
                    e.dir = { cosf(ang), 0, sinf(ang) };
                }
            }

            // Move (sync to terrain)
            if ((rand() % 100) < 20) {
                float nx = e.pos.x + e.dir.x * e.moveSpeed * dt * 0.8f;
                float nz = e.pos.z + e.dir.z * e.moveSpeed * dt * 0.8f;
                float ny = terrainHeight(nx, nz) + 1.6f; // ✅ +1.6f = player foot height
                if (fabs(ny - e.pos.y) < 1.0f) { // gentle slope
                    e.pos.x = nx;
                    e.pos.z = nz;
                    e.pos.y = ny;
                }
            }

            // Shooting
            e.shootCooldown -= dt;
            if (e.canSeePlayer && e.shootCooldown <= 0.0f) {
                for (int bi = 0; bi < MAX_BULLETS; bi++) {
                    if (!bullets[bi].active) {
                        bullets[bi].active = true;
                        bullets[bi].pos = e.pos;
                        bullets[bi].pos.y += 1.4f;
                        bullets[bi].dir = vecSub(camPos, bullets[bi].pos);
                        vecNormalize(bullets[bi].dir);
                        bullets[bi].life = 3.0f;
                        bullets[bi].owner = 1;
                        e.shootCooldown = e.maxShootCooldown * (0.7f + (rand() % 60) / 100.0f);
                        Vec3 muzzle = e.pos;
                        muzzle.x += e.dir.x * 0.4f;
                        muzzle.z += e.dir.z * 0.4f;
                        muzzle.y += 1.4f;
                        spawnParticle(muzzle);
                        break;
                    }
                }
            }
        }

        // Handle enemy death & respawn
        for (int i = 0; i < MAX_ENEMIES; i++) {
            Enemy& e = enemies[i];
            if (e.deathTimer > 0.0f) {
                e.deathTimer -= dt;
                if (e.deathTimer <= 0.0f) {
                    // Respawn
                    e.pos.x = ((rand() % 40) - 20) * 0.8f;
                    e.pos.z = ((rand() % 40) - 20) * 0.8f;
                    e.pos.y = terrainHeight(e.pos.x, e.pos.z) + 1.6f; // ✅ correct height
                    e.health = 100.0f;
                    e.active = true;
                    e.flashTimer = 0.0f;
                    e.shootCooldown = (rand() % 1000) / 500.0f;
                }
            }
        }

        // Collisions
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (!bullets[i].active) continue;

            // Enemy bullet → player
            if (bullets[i].owner == 1) {
                Vec3 toPlayer = vecSub(camPos, bullets[i].pos);
                float distHoriz = sqrtf(toPlayer.x * toPlayer.x + toPlayer.z * toPlayer.z);
                if (distHoriz < 0.4f && fabs(toPlayer.y) < 0.8f) {
                    bullets[i].active = false;
                    playerHealth -= 25;
                    damageFlash = 0.4f;
                    playHitSound();    // ✅ sound when hit
                    if (playerHealth <= 0 && !gameOver) {
                        playerHealth = 0;
                        gameOver = true;       // ✅ flag game over
                        playGameOverSound();   // ✅ sound for game over
                        printf("\nGAME OVER! Final Score: %d\n", score);
                        printf("Better Luck Next Time!\n");
                    }
                }
            }

            // Player bullet → enemy
            if (bullets[i].owner == 0) {
                for (int j = 0; j < MAX_ENEMIES; j++) {
                    Enemy& e = enemies[j];
                    if (!e.active || e.deathTimer > 0.0f) continue;
                    Vec3 diff = vecSub(bullets[i].pos, e.pos);
                    float distHoriz = sqrtf(diff.x * diff.x + diff.z * diff.z);
                    if (distHoriz < e.size && fabs(diff.y) < 1.2f) {
                        bullets[i].active = false;
                        e.flashTimer = 0.25f;
                        spawnParticle(e.pos);
                        e.health -= 34.0f;
                        if (e.health <= 0 && e.deathTimer <= 0.0f) {
                            e.deathTimer = 2.0f; // ✅ die for 2 seconds
                            score += 100;
                        }
                    }
                }
            }
        }
    } // end if !gameOver

    // Render
    glClearColor(0.5f, 0.7f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_FOG);
    GLfloat fogColor[4] = { 0.5f, 0.7f, 1.0f, 1.0f };
    glFogi(GL_FOG_MODE, GL_EXP2);
    glFogfv(GL_FOG_COLOR, fogColor);
    glFogf(GL_FOG_DENSITY, 0.005f);
    glHint(GL_FOG_HINT, GL_NICEST);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(70.0f, (double)WIN_W / (double)WIN_H, 0.1f, 300.0f);
    updateCameraVectors();
    applyView();

    glEnable(GL_LIGHTING); glEnable(GL_LIGHT0);
    const float sunAngle = 0.8f;
    float lightpos[4] = {
        cosf(sunAngle) * 120.0f,
        80.0f + sinf(sunAngle) * 60.0f,
        sinf(sunAngle) * 120.0f,
        0
    };
    float lightcol[4] = { 0.9f, 0.85f, 0.75f, 1 };
    glLightfv(GL_LIGHT0, GL_POSITION, lightpos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightcol);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    drawSkydome();
    drawFloor();
    drawEnvironment();
    for (int i = 0; i < MAX_ENEMIES; i++)
        drawEnemy(enemies[i]); // draws only if alive
    for (int i = 0; i < MAX_BULLETS; i++)
        if (bullets[i].active) drawBullet(bullets[i]);
    for (int i = 0; i < MAX_PARTICLES; i++)
        if (particles[i].active) drawParticle(particles[i]);

    glDisable(GL_LIGHTING);
    glDisable(GL_COLOR_MATERIAL);
    glDisable(GL_FOG);

    // Damage flash
    if (damageFlash > 0.0f) {
        glDisable(GL_DEPTH_TEST);
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); gluOrtho2D(0, 1, 0, 1);
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        glColor4f(1.0f, 0.2f, 0.2f, damageFlash * 0.8f);
        glBegin(GL_QUADS);
        glVertex2f(0, 0); glVertex2f(1, 0); glVertex2f(1, 1); glVertex2f(0, 1);
        glEnd();
        glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
        glEnable(GL_DEPTH_TEST);
        damageFlash -= dt;
    }

    drawHUD();

    // Gun
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    if (!gameOver) // ✅ don't draw gun after death (optional, remove if you want gun visible)
        drawGun();
    glEnable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST);

    glutSwapBuffers();
    // Keep rendering even on game over so overlay stays visible
    glutPostRedisplay();
}

// Main
int main(int argc, char** argv) {
    srand((unsigned)time(NULL));
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(WIN_W, WIN_H);
    glutCreateWindow("FPS OpenGL - Fixed Enemies & Gun");

    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);

    updateCameraVectors();
    initEnemies();
    initEnvironment();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutPassiveMotionFunc(passiveMouse);
    glutKeyboardFunc(keyboardDown);
    glutKeyboardUpFunc(keyboardUp);
    glutSpecialFunc(specialDown);
    glutMouseFunc(mouseClick);

    if (cursorCaptured) {
        glutSetCursor(GLUT_CURSOR_NONE);
        int cx = WIN_W / 2, cy = WIN_H / 2;
        ignoreWarp = true;
        glutWarpPointer(cx, cy);
    }

    lastTime = nowSeconds();
    printf("Controls: WASD-move, Mouse LMB-shoot, SHIFT-run, R-reload, ESC-toggle cursor\n");
    glutMainLoop();
    return 0;
}