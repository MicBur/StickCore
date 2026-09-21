// ---------------------------------------------------------------------------
//  StickCore  –  StitchGLWidget.cpp
// ---------------------------------------------------------------------------
#include "gl/StitchGLWidget.h"

#include <QOpenGLShaderProgram>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QSurfaceFormat>
#include <cmath>

namespace stick {

// ---------------------------------------------------------------------------
//  Shader sources (GLSL 3.30 core)
// ---------------------------------------------------------------------------
static const char* kThreadVert = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in mat4 aModel;   // occupies 2,3,4,5
layout(location=6) in vec3 aColor;
uniform mat4 uView;
uniform mat4 uProj;
out vec3 vNormal;
out vec3 vFrag;
out vec3 vColor;
void main() {
    vec4 world = aModel * vec4(aPos, 1.0);
    vFrag   = world.xyz;
    vNormal = normalize(mat3(aModel) * aNormal);   // uniform scale on x/y
    vColor  = aColor;
    gl_Position = uProj * uView * world;
}
)";

static const char* kThreadFrag = R"(#version 330 core
in vec3 vNormal;
in vec3 vFrag;
in vec3 vColor;
out vec4 FragColor;
uniform vec3 uLightPos;
uniform vec3 uViewPos;
void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightPos - vFrag);
    vec3 V = normalize(uViewPos - vFrag);
    vec3 H = normalize(L + V);
    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 56.0);   // glossy polyester sheen
    vec3 ambient  = 0.28 * vColor;
    vec3 diffuse  = 0.80 * diff * vColor;
    vec3 specular = 0.65 * spec * vec3(1.0);
    FragColor = vec4(ambient + diffuse + specular, 1.0);
}
)";

static const char* kFabricVert = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
uniform mat4 uView;
uniform mat4 uProj;
out vec2 vUV;
out vec3 vFrag;
void main() {
    vFrag = aPos;
    vUV   = aUV;
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)";

// Procedural woven-linen normal mapping: two orthogonal sinusoidal ridges
// perturb the surface normal, then Blinn-Phong lights the weave.
static const char* kFabricFrag = R"(#version 330 core
in vec2 vUV;
in vec3 vFrag;
out vec4 FragColor;
uniform vec3 uLightPos;
uniform vec3 uViewPos;
uniform float uFreq;
void main() {
    vec2 p = vUV * uFreq;
    float bumpX = cos(p.x * 6.2831853);
    float bumpY = cos(p.y * 6.2831853);
    vec3 N = normalize(vec3(bumpX * 0.35, bumpY * 0.35, 1.0));
    vec3 L = normalize(uLightPos - vFrag);
    vec3 V = normalize(uViewPos - vFrag);
    vec3 H = normalize(L + V);
    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 8.0);
    vec3 base = vec3(0.86, 0.83, 0.74);           // unbleached linen
    float weave = 0.5 + 0.5 * (sin(p.x*6.2831853) * sin(p.y*6.2831853));
    base *= mix(0.82, 1.0, weave);
    vec3 col = base * (0.35 + 0.75 * diff) + spec * 0.08;
    FragColor = vec4(col, 1.0);
}
)";

static const char* kHoopVert = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
uniform mat4 uView;
uniform mat4 uProj;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)";

static const char* kHoopFrag = R"(#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main() {
    FragColor = vColor;
}
)";

// ---------------------------------------------------------------------------
StitchGLWidget::StitchGLWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    setFormat(fmt);

    connect(&m_timer, &QTimer::timeout, this, &StitchGLWidget::onTick);
    m_timer.setInterval(16);   // ~60 Hz
}

StitchGLWidget::~StitchGLWidget()
{
    makeCurrent();
    if (m_vbo)     glDeleteBuffers(1, &m_vbo);
    if (m_ebo)     glDeleteBuffers(1, &m_ebo);
    if (m_instVbo) glDeleteBuffers(1, &m_instVbo);
    if (m_vao)     glDeleteVertexArrays(1, &m_vao);
    if (m_fabVbo)  glDeleteBuffers(1, &m_fabVbo);
    if (m_fabVao)  glDeleteVertexArrays(1, &m_fabVao);
    if (m_hoopVbo) glDeleteBuffers(1, &m_hoopVbo);
    if (m_hoopVao) glDeleteVertexArrays(1, &m_hoopVao);
    if (m_gridVbo) glDeleteBuffers(1, &m_gridVbo);
    if (m_gridVao) glDeleteVertexArrays(1, &m_gridVao);
    delete m_threadProg;
    delete m_fabricProg;
    delete m_hoopProg;
    doneCurrent();
}

// ---------------------------------------------------------------------------
void StitchGLWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.12f, 0.13f, 0.16f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);

    m_threadProg = new QOpenGLShaderProgram(this);
    m_threadProg->addShaderFromSourceCode(QOpenGLShader::Vertex,   kThreadVert);
    m_threadProg->addShaderFromSourceCode(QOpenGLShader::Fragment, kThreadFrag);
    m_threadProg->link();

    m_fabricProg = new QOpenGLShaderProgram(this);
    m_fabricProg->addShaderFromSourceCode(QOpenGLShader::Vertex,   kFabricVert);
    m_fabricProg->addShaderFromSourceCode(QOpenGLShader::Fragment, kFabricFrag);
    m_fabricProg->link();

    m_hoopProg = new QOpenGLShaderProgram(this);
    m_hoopProg->addShaderFromSourceCode(QOpenGLShader::Vertex,   kHoopVert);
    m_hoopProg->addShaderFromSourceCode(QOpenGLShader::Fragment, kHoopFrag);
    m_hoopProg->link();

    buildCylinderMesh(14);
    buildFabric();
    buildHoopGeometry();
    rebuildInstances();      // CPU build from any sequence set before init
    uploadInstances();       // context is current here
    m_ready = true;
}

// Upload the CPU instance data to the GPU. Assumes the GL context is current.
void StitchGLWidget::uploadInstances()
{
    if (!m_instVbo) return;
    glBindBuffer(GL_ARRAY_BUFFER, m_instVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 GLsizeiptr(m_instanceData.size() * sizeof(float)),
                 m_instanceData.empty() ? nullptr : m_instanceData.data(),
                 GL_DYNAMIC_DRAW);
}

// ---------------------------------------------------------------------------
void StitchGLWidget::buildCylinderMesh(int sides)
{
    // Unit cylinder along +Z, radius 1, from z=0 to z=1. Per-vertex: pos+normal.
    std::vector<float> verts;
    std::vector<unsigned int> idx;
    for (int i = 0; i <= sides; ++i) {
        const float a = 2.0f * float(M_PI) * i / sides;
        const float cx = std::cos(a), sy = std::sin(a);
        // bottom ring (z=0)
        verts.insert(verts.end(), { cx, sy, 0.0f, cx, sy, 0.0f });
        // top ring (z=1)
        verts.insert(verts.end(), { cx, sy, 1.0f, cx, sy, 0.0f });
    }
    for (int i = 0; i < sides; ++i) {
        const unsigned int b0 = 2u * i, t0 = b0 + 1, b1 = b0 + 2, t1 = b0 + 3;
        idx.insert(idx.end(), { b0, b1, t1,  b0, t1, t0 });
    }
    m_indexCount = static_cast<int>(idx.size());

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &m_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);

    const GLsizei stride = 6 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));

    // Instance buffer: mat4 (loc 2..5) + vec3 color (loc 6), stride 19 floats.
    glGenBuffers(1, &m_instVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_instVbo);
    const GLsizei istride = 19 * sizeof(float);
    for (int c = 0; c < 4; ++c) {
        glEnableVertexAttribArray(2 + c);
        glVertexAttribPointer(2 + c, 4, GL_FLOAT, GL_FALSE, istride,
                              (void*)(sizeof(float) * (4 * c)));
        glVertexAttribDivisor(2 + c, 1);
    }
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, istride, (void*)(sizeof(float) * 16));
    glVertexAttribDivisor(6, 1);

    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
void StitchGLWidget::buildFabric()
{
    // A single large quad on the z=-0.3 plane; sized generously and re-centred
    // per design in paintGL via the model-less world coords. UVs 0..1.
    const float e = 500.0f;   // half-extent in mm (plenty for any hoop)
    const float z = -0.3f;
    const float quad[] = {
        //   x     y     z     u     v
        -e, -e, z, 0.0f, 0.0f,
         e, -e, z, 1.0f, 0.0f,
         e,  e, z, 1.0f, 1.0f,
        -e, -e, z, 0.0f, 0.0f,
         e,  e, z, 1.0f, 1.0f,
        -e,  e, z, 0.0f, 1.0f
    };
    glGenVertexArrays(1, &m_fabVao);
    glBindVertexArray(m_fabVao);
    glGenBuffers(1, &m_fabVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_fabVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    const GLsizei stride = 5 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
void StitchGLWidget::buildHoopGeometry()
{
    if (!m_ready) return;

    struct Vertex { float x, y, z, r, g, b, a; };
    std::vector<Vertex> gridVerts;
    std::vector<Vertex> hoopVerts;

    const double hw = m_hoop.widthMm * 0.5;
    const double hh = m_hoop.heightMm * 0.5;

    // --- 1. Grid lines & axes (GL_LINES) -----------------------------------
    const float zGrid = -0.22f;
    const int startX = -int(std::floor(hw / 10.0)) * 10;
    const int endX   =  int(std::floor(hw / 10.0)) * 10;
    const int startY = -int(std::floor(hh / 10.0)) * 10;
    const int endY   =  int(std::floor(hh / 10.0)) * 10;

    for (int x = startX; x <= endX; x += 10) {
        if (x == 0) continue;
        const bool major = (x % 50 == 0);
        const float alpha = major ? 0.32f : 0.12f;
        const float col = major ? 0.55f : 0.40f;
        gridVerts.push_back({ float(x), float(-hh), zGrid, col, col, col + 0.08f, alpha });
        gridVerts.push_back({ float(x), float( hh), zGrid, col, col, col + 0.08f, alpha });
    }

    for (int y = startY; y <= endY; y += 10) {
        if (y == 0) continue;
        const bool major = (y % 50 == 0);
        const float alpha = major ? 0.32f : 0.12f;
        const float col = major ? 0.55f : 0.40f;
        gridVerts.push_back({ float(-hw), float(y), zGrid, col, col, col + 0.08f, alpha });
        gridVerts.push_back({ float( hw), float(y), zGrid, col, col, col + 0.08f, alpha });
    }

    // Origin crosshairs (X & Y axes)
    const float zAxis = -0.16f;
    gridVerts.push_back({ float(-hw), 0.0f, zAxis, 0.18f, 0.83f, 0.75f, 0.70f });
    gridVerts.push_back({ float( hw), 0.0f, zAxis, 0.18f, 0.83f, 0.75f, 0.70f });
    gridVerts.push_back({ 0.0f, float(-hh), zAxis, 0.18f, 0.83f, 0.75f, 0.70f });
    gridVerts.push_back({ 0.0f, float( hh), zAxis, 0.18f, 0.83f, 0.75f, 0.70f });

    // Tick marks along X axis
    for (int x = startX; x <= endX; x += 10) {
        float tickH = (x % 50 == 0) ? 3.0f : 1.5f;
        gridVerts.push_back({ float(x), -tickH, zAxis, 0.18f, 0.83f, 0.75f, 0.60f });
        gridVerts.push_back({ float(x),  tickH, zAxis, 0.18f, 0.83f, 0.75f, 0.60f });
    }
    // Tick marks along Y axis
    for (int y = startY; y <= endY; y += 10) {
        float tickW = (y % 50 == 0) ? 3.0f : 1.5f;
        gridVerts.push_back({ -tickW, float(y), zAxis, 0.18f, 0.83f, 0.75f, 0.60f });
        gridVerts.push_back({  tickW, float(y), zAxis, 0.18f, 0.83f, 0.75f, 0.60f });
    }

    // Secondary machine reference hoop (A 126x110 vs B 140x200)
    double refW = 126.0, refH = 110.0;
    if (m_hoop.widthMm < 130.0) { refW = 140.0; refH = 200.0; }
    const double rhw = refW * 0.5, rhh = refH * 0.5;
    const float cr = 0.55f, cg = 0.65f, cb = 0.75f, ca = 0.28f;
    gridVerts.push_back({ float(-rhw), float(-rhh), zGrid, cr, cg, cb, ca });
    gridVerts.push_back({ float( rhw), float(-rhh), zGrid, cr, cg, cb, ca });
    gridVerts.push_back({ float( rhw), float(-rhh), zGrid, cr, cg, cb, ca });
    gridVerts.push_back({ float( rhw), float( rhh), zGrid, cr, cg, cb, ca });
    gridVerts.push_back({ float( rhw), float( rhh), zGrid, cr, cg, cb, ca });
    gridVerts.push_back({ float(-rhw), float( rhh), zGrid, cr, cg, cb, ca });
    gridVerts.push_back({ float(-rhw), float( rhh), zGrid, cr, cg, cb, ca });
    gridVerts.push_back({ float(-rhw), float(-rhh), zGrid, cr, cg, cb, ca });

    // Upload grid buffer
    m_gridVertexCount = static_cast<int>(gridVerts.size());
    if (!m_gridVao) glGenVertexArrays(1, &m_gridVao);
    if (!m_gridVbo) glGenBuffers(1, &m_gridVbo);

    glBindVertexArray(m_gridVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
    glBufferData(GL_ARRAY_BUFFER, gridVerts.size() * sizeof(Vertex), gridVerts.data(), GL_STATIC_DRAW);

    const GLsizei stride = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glBindVertexArray(0);

    // --- 2. Physical Hoop Frame Ribbon & Boundary Ring (GL_TRIANGLES) --------
    const float Ri = 18.0f;             // inner corner radius
    const float T  = 16.0f;             // hoop frame thickness
    const float Ro = Ri + T;            // outer corner radius
    const int   corners = 4;
    const int   segsPerCorner = 14;

    const bool overflow = isOverflowing();
    const float brR = overflow ? 0.96f : 0.18f;
    const float brG = overflow ? 0.25f : 0.83f;
    const float brB = overflow ? 0.38f : 0.75f;
    const float brA = 0.95f;

    struct Pt2 { float x, y; };
    std::vector<Pt2> innerLoop;
    std::vector<Pt2> outerLoop;

    const float cx[4] = { float(hw - Ri), float(-hw + Ri), float(-hw + Ri), float(hw - Ri) };
    const float cy[4] = { float(hh - Ri), float(hh - Ri), float(-hh + Ri), float(-hh + Ri) };
    const float startAng[4] = { 0.0f, float(M_PI*0.5), float(M_PI), float(M_PI*1.5) };

    for (int c = 0; c < corners; ++c) {
        for (int s = 0; s < segsPerCorner; ++s) {
            float a = startAng[c] + (float(M_PI * 0.5) * s) / float(segsPerCorner);
            float ca = std::cos(a), sa = std::sin(a);
            innerLoop.push_back({ cx[c] + Ri * ca, cy[c] + Ri * sa });
            outerLoop.push_back({ cx[c] + Ro * ca, cy[c] + Ro * sa });
        }
    }

    const int loopN = static_cast<int>(innerLoop.size());

    // Frame bezel ribbon (quads between innerLoop and outerLoop)
    for (int i = 0; i < loopN; ++i) {
        int next = (i + 1) % loopN;
        Pt2 in0 = innerLoop[i], in1 = innerLoop[next];
        Pt2 out0 = outerLoop[i], out1 = outerLoop[next];

        float rIn = 0.15f, gIn = 0.17f, bIn = 0.22f, aIn = 0.96f;
        float rOut = 0.24f, gOut = 0.27f, bOut = 0.34f, aOut = 0.96f;
        float zIn = 0.05f, zOut = 0.6f;

        // Tri 1: in0, out0, out1
        hoopVerts.push_back({ in0.x, in0.y, zIn, rIn, gIn, bIn, aIn });
        hoopVerts.push_back({ out0.x, out0.y, zOut, rOut, gOut, bOut, aOut });
        hoopVerts.push_back({ out1.x, out1.y, zOut, rOut, gOut, bOut, aOut });

        // Tri 2: in0, out1, in1
        hoopVerts.push_back({ in0.x, in0.y, zIn, rIn, gIn, bIn, aIn });
        hoopVerts.push_back({ out1.x, out1.y, zOut, rOut, gOut, bOut, aOut });
        hoopVerts.push_back({ in1.x, in1.y, zIn, rIn, gIn, bIn, aIn });

        // Inner glowing border line strip
        Pt2 bIn0 = { in0.x * 0.985f, in0.y * 0.985f };
        Pt2 bIn1 = { in1.x * 0.985f, in1.y * 0.985f };
        float zBorder = 0.08f;
        hoopVerts.push_back({ in0.x, in0.y, zBorder, brR, brG, brB, brA });
        hoopVerts.push_back({ bIn0.x, bIn0.y, zBorder, brR, brG, brB, brA });
        hoopVerts.push_back({ bIn1.x, bIn1.y, zBorder, brR, brG, brB, brA });

        hoopVerts.push_back({ in0.x, in0.y, zBorder, brR, brG, brB, brA });
        hoopVerts.push_back({ bIn1.x, bIn1.y, zBorder, brR, brG, brB, brA });
        hoopVerts.push_back({ in1.x, in1.y, zBorder, brR, brG, brB, brA });
    }

    // Top tension thumbscrew bracket (accent detail at top)
    const float scW = 18.0f, scH = 14.0f;
    const float scY = float(hh + T + 1.0f);
    const float zScrew = 0.9f;
    float scR = 0.72f, scG = 0.75f, scB = 0.82f, scA = 1.0f;

    hoopVerts.push_back({ -scW*0.5f, scY, zScrew, scR, scG, scB, scA });
    hoopVerts.push_back({  scW*0.5f, scY, zScrew, scR, scG, scB, scA });
    hoopVerts.push_back({  scW*0.5f, scY + scH, zScrew, scR*0.8f, scG*0.8f, scB*0.8f, scA });

    hoopVerts.push_back({ -scW*0.5f, scY, zScrew, scR, scG, scB, scA });
    hoopVerts.push_back({  scW*0.5f, scY + scH, zScrew, scR*0.8f, scG*0.8f, scB*0.8f, scA });
    hoopVerts.push_back({ -scW*0.5f, scY + scH, zScrew, scR*0.8f, scG*0.8f, scB*0.8f, scA });

    // Upload hoop buffer
    m_hoopVertexCount = static_cast<int>(hoopVerts.size());
    if (!m_hoopVao) glGenVertexArrays(1, &m_hoopVao);
    if (!m_hoopVbo) glGenBuffers(1, &m_hoopVbo);

    glBindVertexArray(m_hoopVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_hoopVbo);
    glBufferData(GL_ARRAY_BUFFER, hoopVerts.size() * sizeof(Vertex), hoopVerts.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
//  Turn the stitch list into per-segment cylinder transforms.
// ---------------------------------------------------------------------------
void StitchGLWidget::rebuildInstances()
{
    m_segments.clear();
    m_instanceData.clear();

    const auto& st = m_seq.stitches;
    QVector3D prevPos;
    bool havePrev = false;
    int  curColor = -1;
    QVector3D rgb(0.85f, 0.15f, 0.15f);

    auto colorFor = [&](int idx) -> QVector3D {
        if (idx >= 0 && idx < int(m_seq.palette.size())) {
            const QColor c = m_seq.palette[idx].color;
            return QVector3D(c.redF(), c.greenF(), c.blueF());
        }
        return QVector3D(0.85f, 0.15f, 0.15f);
    };

    for (const Stitch& s : st) {
        if (s.flags & SF_End) break;
        if (s.colorIdx != curColor) { curColor = s.colorIdx; rgb = colorFor(curColor); }

        const QVector3D pos(float(s.x), float(s.y), 0.0f);
        const bool travel = (s.flags & (SF_Jump | SF_Trim | SF_ColorChange | SF_Stop)) != 0;

        if (havePrev && !travel) {
            const QVector3D d = pos - prevPos;
            const float L = d.length();
            if (L > 1e-4f) {
                // Build model = translate(prev) * align(+Z -> d) * scale(r,r,L)
                QMatrix4x4 m;
                m.translate(prevPos);
                const QVector3D zAxis(0, 0, 1);
                const QVector3D dir = d / L;
                const QVector3D axis = QVector3D::crossProduct(zAxis, dir);
                const float dotv = QVector3D::dotProduct(zAxis, dir);
                if (axis.length() > 1e-5f)
                    m.rotate(qRadiansToDegrees(std::acos(qBound(-1.0f, dotv, 1.0f))),
                             axis.normalized());
                else if (dotv < 0.0f)
                    m.rotate(180.0f, QVector3D(1, 0, 0));
                m.scale(m_threadRadius, m_threadRadius, L);

                Segment seg{ m, rgb };
                m_segments.push_back(seg);

                const float* md = m.constData();          // column-major 16
                m_instanceData.insert(m_instanceData.end(), md, md + 16);
                m_instanceData.push_back(rgb.x());
                m_instanceData.push_back(rgb.y());
                m_instanceData.push_back(rgb.z());
            }
        }
        prevPos = pos;
        havePrev = true;
    }

    // Camera framing: store design bounds.
    double mnx, mny, mxx, mxy;
    if (m_seq.bounds(mnx, mny, mxx, mxy)) {
        m_designW = float(mxx - mnx);
        m_designH = float(mxy - mny);
    }

    m_visible = int(m_segments.size());   // show all until playback starts
}

// ---------------------------------------------------------------------------
void StitchGLWidget::setSequence(const StitchSequence& seq)
{
    m_seq = seq;
    rebuildInstances();
    if (m_ready) {
        makeCurrent();
        uploadInstances();
        buildHoopGeometry();
        doneCurrent();
    }
    m_visible = int(m_segments.size());
    update();
    emit playbackProgress(m_visible, int(m_segments.size()));
    emit playbackStateChanged(false);
    emit hoopOverflowChanged(isOverflowing());
}

void StitchGLWidget::setThreadRadiusMm(float r)
{
    m_threadRadius = std::max(0.02f, r);
    rebuildInstances();
    if (m_ready) { makeCurrent(); uploadInstances(); doneCurrent(); }
    update();
}

// ---------------------------------------------------------------------------
void StitchGLWidget::setHoop(double widthMm, double heightMm, const QString& name)
{
    m_hoop.widthMm  = widthMm;
    m_hoop.heightMm = heightMm;
    if (!name.isEmpty()) m_hoop.name = name;
    if (m_ready) {
        makeCurrent();
        buildHoopGeometry();
        doneCurrent();
    }
    update();
    emit hoopOverflowChanged(isOverflowing());
}

void StitchGLWidget::setHoopVisible(bool visible)
{
    if (m_hoop.visible == visible) return;
    m_hoop.visible = visible;
    update();
}

void StitchGLWidget::setMouseMode(MouseMode mode)
{
    m_mouseMode = mode;
    update();
}

void StitchGLWidget::setTopDownView()
{
    m_yaw   = 0.0f;
    m_pitch = 89.5f;
    m_center = QVector3D(0.0f, 0.0f, 0.0f);
    update();
}

void StitchGLWidget::setPerspectiveView()
{
    m_yaw   = 0.0f;
    m_pitch = 28.0f;
    m_center = QVector3D(0.0f, 0.0f, 0.0f);
    update();
}

bool StitchGLWidget::isOverflowing() const
{
    if (m_seq.empty()) return false;
    double x0, y0, x1, y1;
    if (!m_seq.bounds(x0, y0, x1, y1)) return false;
    const double hw = m_hoop.widthMm  * 0.5;
    const double hh = m_hoop.heightMm * 0.5;
    return (x0 < -hw - 0.1 || x1 > hw + 0.1 ||
            y0 < -hh - 0.1 || y1 > hh + 0.1);
}

QPointF StitchGLWidget::designCenter() const
{
    double x0, y0, x1, y1;
    if (m_seq.bounds(x0, y0, x1, y1))
        return QPointF(0.5 * (x0 + x1), 0.5 * (y0 + y1));
    return QPointF(0.0, 0.0);
}

void StitchGLWidget::moveDesign(double dxMm, double dyMm)
{
    if (m_seq.empty()) return;
    for (Stitch& s : m_seq.stitches) {
        s.x += dxMm;
        s.y += dyMm;
    }
    rebuildInstances();
    if (m_ready) {
        makeCurrent();
        uploadInstances();
        buildHoopGeometry();
        doneCurrent();
    }
    update();

    emit designMoved(dxMm, dyMm);
    double x0, y0, x1, y1;
    if (m_seq.bounds(x0, y0, x1, y1)) {
        emit designPositionChanged(0.5 * (x0 + x1), 0.5 * (y0 + y1));
    }
    emit hoopOverflowChanged(isOverflowing());
}

void StitchGLWidget::centerDesign()
{
    if (m_seq.empty()) return;
    double x0, y0, x1, y1;
    if (m_seq.bounds(x0, y0, x1, y1)) {
        const double cx = 0.5 * (x0 + x1);
        const double cy = 0.5 * (y0 + y1);
        moveDesign(-cx, -cy);
    }
}

// ---------------------------------------------------------------------------
QMatrix4x4 StitchGLWidget::currentViewMatrix() const
{
    QMatrix4x4 view;
    const float yaw = qDegreesToRadians(m_yaw);
    const float pit = qDegreesToRadians(m_pitch);
    const QVector3D eye(
        m_center.x() + m_dist * std::cos(pit) * std::sin(yaw),
        m_center.y() + m_dist * std::sin(pit),
        m_center.z() + m_dist * std::cos(pit) * std::cos(yaw));
    view.lookAt(eye, m_center, QVector3D(0, 1, 0));
    return view;
}

QPointF StitchGLWidget::unprojectToPlane(const QPoint& pos) const
{
    const int w = width();
    const int h = height() ? height() : 1;
    const float ndcX = (2.0f * pos.x()) / float(w) - 1.0f;
    const float ndcY = 1.0f - (2.0f * pos.y()) / float(h);

    QMatrix4x4 view = currentViewMatrix();
    QMatrix4x4 invVP = (m_proj * view).inverted();

    QVector4D nearPt = invVP * QVector4D(ndcX, ndcY, -1.0f, 1.0f);
    if (std::abs(nearPt.w()) > 1e-6f) nearPt /= nearPt.w();

    QVector4D farPt = invVP * QVector4D(ndcX, ndcY, 1.0f, 1.0f);
    if (std::abs(farPt.w()) > 1e-6f) farPt /= farPt.w();

    QVector3D rayOrigin = nearPt.toVector3D();
    QVector3D rayDir = (farPt.toVector3D() - rayOrigin).normalized();

    // Intersect ray with plane Z = 0: rayOrigin.z + t * rayDir.z = 0
    if (std::abs(rayDir.z()) > 1e-5f) {
        float t = -rayOrigin.z() / rayDir.z();
        QVector3D hit = rayOrigin + t * rayDir;
        return QPointF(hit.x(), hit.y());
    }
    return QPointF(0, 0);
}

// ---------------------------------------------------------------------------
void StitchGLWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
    m_proj.setToIdentity();
    m_proj.perspective(42.0f, float(w) / float(h ? h : 1), 0.5f, 4000.0f);
}

// ---------------------------------------------------------------------------
void StitchGLWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Auto-fit to the hoop and design
    if (m_autoFrame) {
        const float fovY = qDegreesToRadians(42.0f);
        const float aspect = float(width()) / float(height() ? height() : 1);
        const float tanY = std::tan(fovY * 0.5f);
        const float tanX = tanY * aspect;
        const float margin = 1.25f;
        const float frameW = std::max(m_designW, float(m_hoop.widthMm));
        const float frameH = std::max(m_designH, float(m_hoop.heightMm));
        const float distH = (frameH * 0.5f * margin) / tanY;
        const float distW = (frameW * 0.5f * margin) / tanX;
        m_dist = std::max(40.0f, std::max(distH, distW));
        m_center = QVector3D(0.0f, 0.0f, 0.0f);
        m_autoFrame = false;
    }

    QMatrix4x4 view = currentViewMatrix();
    const float yaw = qDegreesToRadians(m_yaw);
    const float pit = qDegreesToRadians(m_pitch);
    const QVector3D eye(
        m_center.x() + m_dist * std::cos(pit) * std::sin(yaw),
        m_center.y() + m_dist * std::sin(pit),
        m_center.z() + m_dist * std::cos(pit) * std::cos(yaw));
    const QVector3D lightPos = eye + QVector3D(40, 80, 60);

    // --- 1. Fabric plane ---------------------------------------------------
    m_fabricProg->bind();
    m_fabricProg->setUniformValue("uView", view);
    m_fabricProg->setUniformValue("uProj", m_proj);
    m_fabricProg->setUniformValue("uLightPos", lightPos);
    m_fabricProg->setUniformValue("uViewPos", eye);
    m_fabricProg->setUniformValue("uFreq", 220.0f);   // weave repeats
    glBindVertexArray(m_fabVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    m_fabricProg->release();

    // --- 2. Embroidery Hoop & Grid -----------------------------------------
    if (m_hoop.visible && m_hoopProg) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_hoopProg->bind();
        m_hoopProg->setUniformValue("uView", view);
        m_hoopProg->setUniformValue("uProj", m_proj);

        // Grid and axes lines
        if (m_gridVao && m_gridVertexCount > 0) {
            glBindVertexArray(m_gridVao);
            glDrawArrays(GL_LINES, 0, m_gridVertexCount);
            glBindVertexArray(0);
        }

        // Hoop physical bezel and boundary ring
        if (m_hoopVao && m_hoopVertexCount > 0) {
            glBindVertexArray(m_hoopVao);
            glDrawArrays(GL_TRIANGLES, 0, m_hoopVertexCount);
            glBindVertexArray(0);
        }

        m_hoopProg->release();
        glDisable(GL_BLEND);
    }

    // --- 3. Stitches / Thread cylinders ------------------------------------
    const int count = qBound(0, m_visible, int(m_segments.size()));
    if (count > 0) {
        m_threadProg->bind();
        m_threadProg->setUniformValue("uView", view);
        m_threadProg->setUniformValue("uProj", m_proj);
        m_threadProg->setUniformValue("uLightPos", lightPos);
        m_threadProg->setUniformValue("uViewPos", eye);
        glBindVertexArray(m_vao);
        glDrawElementsInstanced(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, 0, count);
        glBindVertexArray(0);
        m_threadProg->release();
    }
}

// ---------------------------------------------------------------------------
//  Playback
// ---------------------------------------------------------------------------
void StitchGLWidget::setVisibleSegments(int count)
{
    m_timer.stop();
    m_visible = qBound(0, count, int(m_segments.size()));
    update();
    emit playbackProgress(m_visible, int(m_segments.size()));
    emit playbackStateChanged(false);
}

void StitchGLWidget::startPlayback()
{
    if (m_segments.empty()) return;
    if (m_visible >= int(m_segments.size())) m_visible = 0;
    m_timer.start();
    emit playbackStateChanged(true);
}

void StitchGLWidget::pausePlayback()
{
    m_timer.stop();
    emit playbackStateChanged(false);
}

void StitchGLWidget::resetPlayback()
{
    m_timer.stop();
    m_visible = 0;
    update();
    emit playbackStateChanged(false);
    emit playbackProgress(0, int(m_segments.size()));
}

void StitchGLWidget::showAll()
{
    m_timer.stop();
    m_visible = int(m_segments.size());
    update();
    emit playbackStateChanged(false);
    emit playbackProgress(m_visible, int(m_segments.size()));
}

void StitchGLWidget::setPlaybackSpeed(int n) { m_speed = std::max(1, n); }

void StitchGLWidget::onTick()
{
    m_visible += m_speed;
    if (m_visible >= int(m_segments.size())) {
        m_visible = int(m_segments.size());
        m_timer.stop();
        emit playbackStateChanged(false);
    }
    update();
    emit playbackProgress(m_visible, int(m_segments.size()));
}

// ---------------------------------------------------------------------------
//  Interaction
// ---------------------------------------------------------------------------
void StitchGLWidget::mousePressEvent(QMouseEvent* e)
{
    setFocus();
    m_lastMouse = e->pos();

    if (e->button() == Qt::LeftButton) {
        const bool isMove = (m_mouseMode == MouseMode::MoveDesign) || (e->modifiers() & Qt::ShiftModifier);
        if (isMove) {
            m_draggingDesign = true;
            m_planeDragStart = unprojectToPlane(e->pos());
            setCursor(Qt::ClosedHandCursor);
        } else {
            m_orbiting = true;
            setCursor(Qt::SizeAllCursor);
        }
    } else if (e->button() == Qt::RightButton) {
        if (m_mouseMode == MouseMode::MoveDesign) {
            m_orbiting = true;
            setCursor(Qt::SizeAllCursor);
        } else {
            m_draggingDesign = true;
            m_planeDragStart = unprojectToPlane(e->pos());
            setCursor(Qt::ClosedHandCursor);
        }
    } else if (e->button() == Qt::MiddleButton) {
        m_panning = true;
        setCursor(Qt::OpenHandCursor);
    }
}

void StitchGLWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (m_draggingDesign) {
        const QPointF curPlane = unprojectToPlane(e->pos());
        const QPointF delta = curPlane - m_planeDragStart;
        m_planeDragStart = curPlane;
        if (!m_seq.empty() && (std::abs(delta.x()) > 1e-4 || std::abs(delta.y()) > 1e-4)) {
            moveDesign(delta.x(), delta.y());
        }
    } else if (m_orbiting) {
        const QPoint d = e->pos() - m_lastMouse;
        m_lastMouse = e->pos();
        m_yaw   += d.x() * 0.4f;
        m_pitch  = qBound(2.0f, m_pitch - d.y() * 0.4f, 89.5f);
        update();
    } else if (m_panning) {
        const QPoint d = e->pos() - m_lastMouse;
        m_lastMouse = e->pos();
        const float scale = m_dist * 0.0018f;
        const float radYaw = qDegreesToRadians(m_yaw);
        m_center.setX(m_center.x() - (d.x() * std::cos(radYaw)) * scale);
        m_center.setY(m_center.y() + d.y() * scale);
        m_center.setZ(m_center.z() - (d.x() * std::sin(radYaw)) * scale);
        update();
    }
}

void StitchGLWidget::mouseReleaseEvent(QMouseEvent*)
{
    m_draggingDesign = false;
    m_orbiting       = false;
    m_panning        = false;
    unsetCursor();
}

void StitchGLWidget::wheelEvent(QWheelEvent* e)
{
    const float f = std::pow(0.9f, e->angleDelta().y() / 120.0f);
    m_dist = qBound(10.0f, m_dist * f, 3000.0f);
    update();
}

void StitchGLWidget::keyPressEvent(QKeyEvent* e)
{
    double step = 1.0;
    if (e->modifiers() & Qt::ShiftModifier) step = 5.0;
    else if (e->modifiers() & Qt::AltModifier) step = 0.1;

    switch (e->key()) {
    case Qt::Key_Left:
        moveDesign(-step, 0.0);
        e->accept();
        return;
    case Qt::Key_Right:
        moveDesign(+step, 0.0);
        e->accept();
        return;
    case Qt::Key_Up:
        moveDesign(0.0, +step);
        e->accept();
        return;
    case Qt::Key_Down:
        moveDesign(0.0, -step);
        e->accept();
        return;
    case Qt::Key_H:
        setHoopVisible(!m_hoop.visible);
        e->accept();
        return;
    case Qt::Key_0:
        centerDesign();
        e->accept();
        return;
    default:
        break;
    }
    QOpenGLWidget::keyPressEvent(e);
}

} // namespace stick
