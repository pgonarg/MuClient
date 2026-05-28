#include "stdafx.h"
#include "ShadowMap.h"
#include <GL/glu.h>
#include <cmath>

// Depth-pass flag (global, mirrors g_UseShaderLighting). Defined here.
bool g_ShadowDepthPass = false;

namespace SEASON3B {

ShadowMap* g_ShadowMap = nullptr;

// Column-major 4x4 multiply: out = A * B (so out*v == A*(B*v)).
static void Mat4Mul(float* out, const float* A, const float* B)
{
    float r[16];
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            r[col * 4 + row] =
                A[0 * 4 + row] * B[col * 4 + 0] +
                A[1 * 4 + row] * B[col * 4 + 1] +
                A[2 * 4 + row] * B[col * 4 + 2] +
                A[3 * 4 + row] * B[col * 4 + 3];
        }
    }
    for (int i = 0; i < 16; ++i) out[i] = r[i];
}

// Rigid-body inverse of a view matrix (orthonormal rotation + translation).
static void InvertRigid(const float* m, float* out)
{
    // Upper 3x3 transpose.
    out[0]  = m[0]; out[1]  = m[4]; out[2]  = m[8];  out[3]  = 0.f;
    out[4]  = m[1]; out[5]  = m[5]; out[6]  = m[9];  out[7]  = 0.f;
    out[8]  = m[2]; out[9]  = m[6]; out[10] = m[10]; out[11] = 0.f;
    // -R^T * t
    const float tx = m[12], ty = m[13], tz = m[14];
    out[12] = -(m[0] * tx + m[1] * ty + m[2]  * tz);
    out[13] = -(m[4] * tx + m[5] * ty + m[6]  * tz);
    out[14] = -(m[8] * tx + m[9] * ty + m[10] * tz);
    out[15] = 1.f;
}

ShadowMap::ShadowMap()
    : m_Fbo(0), m_DepthTex(0), m_Size(0), m_bValid(false)
    , m_HalfExtent(1800.f), m_Distance(2000.f), m_Near(50.f), m_Far(4500.f)
{
    // Default sun: from the upper-front-right, travelling down into the scene.
    SetSunDir(0.9f, 0.6f, -1.6f);
}

ShadowMap::~ShadowMap()
{
    Shutdown();
}

void ShadowMap::SetSunDir(float x, float y, float z)
{
    float len = std::sqrt(x * x + y * y + z * z);
    if (len < 1e-5f) { x = 0.f; y = 0.f; z = -1.f; len = 1.f; }
    m_SunDir[0] = x / len; m_SunDir[1] = y / len; m_SunDir[2] = z / len;
}

bool ShadowMap::Initialize(int size)
{
    if (m_bValid) return true;
    m_Size = size;

    glGenTextures(1, &m_DepthTex);
    glBindTexture(GL_TEXTURE_2D, m_DepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    // Outside the map = depth 1.0 (far) = never in shadow.
    const float border[4] = { 1.f, 1.f, 1.f, 1.f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    // Sample depth as luminance for fixed-function debug visualization.
    glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_LUMINANCE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &m_Fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_Fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_DepthTex, 0);
    glDrawBuffer(GL_NONE);   // depth-only FBO
    glReadBuffer(GL_NONE);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        g_ErrorReport.Write(L"> ShadowMap FBO incomplete (0x%X).\r\n", status);
        Shutdown();
        return false;
    }

    m_bValid = true;
    g_ErrorReport.Write(L"> ShadowMap initialized (%dx%d).\r\n", size, size);
    return true;
}

void ShadowMap::Shutdown()
{
    if (m_Fbo)      { glDeleteFramebuffers(1, &m_Fbo); m_Fbo = 0; }
    if (m_DepthTex) { glDeleteTextures(1, &m_DepthTex); m_DepthTex = 0; }
    m_bValid = false;
}

void ShadowMap::BeginDepthPass(const float* focus)
{
    if (!m_bValid || !focus) return;

    float eye[3] = {
        focus[0] - m_SunDir[0] * m_Distance,
        focus[1] - m_SunDir[1] * m_Distance,
        focus[2] - m_SunDir[2] * m_Distance,
    };
    // Pick an up vector not parallel to the view direction.
    float up[3] = { 0.f, 0.f, 1.f };
    if (std::fabs(m_SunDir[2]) > 0.95f) { up[0] = 0.f; up[1] = 1.f; up[2] = 0.f; }

    // Remember the currently bound framebuffer (Bloom wraps the scene in its own
    // FBO) so we can restore it instead of assuming the default framebuffer.
    m_PrevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_PrevFbo);
    glGetIntegerv(GL_VIEWPORT, m_PrevViewport);  // restore this exact viewport after

    glBindFramebuffer(GL_FRAMEBUFFER, m_Fbo);
    glViewport(0, 0, m_Size, m_Size);

    // CRITICAL: enable depth writes and a sane depth func BEFORE clearing, or a
    // leftover disabled depth mask (from a prior transparent pass) silently makes
    // glClear a no-op and the caster writes fail the stale depth test.
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_SCISSOR_TEST);
    glClearDepth(1.0);
    glClear(GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(-m_HalfExtent, m_HalfExtent, -m_HalfExtent, m_HalfExtent, m_Near, m_Far);
    glGetFloatv(GL_PROJECTION_MATRIX, m_LightProj);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    gluLookAt(eye[0], eye[1], eye[2], focus[0], focus[1], focus[2], up[0], up[1], up[2]);
    glGetFloatv(GL_MODELVIEW_MATRIX, m_LightView);

    // Depth-only: mask color. Polygon offset reduces self-shadow acne.
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.5f, 4.0f);

    g_ShadowDepthPass = true;
}

void ShadowMap::EndDepthPass(int restoreW, int restoreH)
{
    if (!m_bValid) return;

    g_ShadowDepthPass = false;

    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)m_PrevFbo);  // restore Bloom's FBO if any
    // Restore the exact prior viewport (handles HiDPI / Bloom-FBO viewports that
    // differ from the logical window size). restoreW/H are intentionally unused.
    (void)restoreW; (void)restoreH;
    glViewport(m_PrevViewport[0], m_PrevViewport[1], m_PrevViewport[2], m_PrevViewport[3]);
}

void ShadowMap::BindForReading(GLenum textureUnit)
{
    if (!m_bValid) return;
    glActiveTexture(textureUnit);
    glBindTexture(GL_TEXTURE_2D, m_DepthTex);
    glActiveTexture(GL_TEXTURE0);
}

void ShadowMap::RenderDebugQuad(int sw, int sh)
{
    if (!m_bValid) return;

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, sw, sh, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m_DepthTex);
    glColor3f(1.f, 1.f, 1.f);

    const int s = 256, x = sw - s - 10, y = 10;
    glBegin(GL_QUADS);
    glTexCoord2f(0.f, 1.f); glVertex2i(x,     y);
    glTexCoord2f(1.f, 1.f); glVertex2i(x + s, y);
    glTexCoord2f(1.f, 0.f); glVertex2i(x + s, y + s);
    glTexCoord2f(0.f, 0.f); glVertex2i(x,     y + s);
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

void ShadowMap::ComputeLightFromView(const float* camView, float* out16) const
{
    float invView[16];
    InvertRigid(camView, invView);
    float tmp[16];
    Mat4Mul(tmp, m_LightView, invView);   // LightView * inverse(camView)
    Mat4Mul(out16, m_LightProj, tmp);      // LightProj * (above)
}

}  // namespace SEASON3B
