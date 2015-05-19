#include "config.h"
#include "GraphicsSurface.h"

#if USE(GRAPHICS_SURFACE)

#include "NotImplemented.h"
#include "TextureMapperGL.h"
#include <QGuiApplication>
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <QOpenGLFramebufferObject>
#include <QOpenGLPaintDevice>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <private/qopenglpaintengine_p.h>
#include "GLSharedContext.h"

namespace WebCore {

#define STRINGIFY(...) #__VA_ARGS__

static PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = 0;
static PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = 0;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC eglImageTargetTexture2DOES = 0;

static GLuint loadShader(GLenum type, const GLchar *shaderSrc)
{
    GLuint shader;
    GLint compiled;

    shader = glCreateShader(type);
    if (!shader)
        return 0;

    glShaderSource(shader, 1, &shaderSrc, 0);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

class ShaderProgram {
public:
    static ShaderProgram* instance() {
        if (!m_instance) {
            m_instance = new ShaderProgram();
        }
        return m_instance;
    }

    GLint program() { return m_program; }
    GLuint vertexAttr() { return m_vertexAttr; }
    GLuint textureCoordAttr() { return m_textureCoordAttr; }
    GLuint textureUniform() { return m_textureUniform; }

private:
    void initialize() {
        GLchar vShaderStr[] =
            STRINGIFY(
                attribute highp vec2 vertex;
                attribute highp vec2 textureCoordinates;
                varying highp vec2 textureCoords;
                void main(void)
                {
                    gl_Position = vec4(vertex, 0.0, 1.0);
                    textureCoords = textureCoordinates;
                }
            );

        GLchar fShaderStr[] =
            STRINGIFY(
                varying highp vec2 textureCoords;
                uniform sampler2D texture;
                void main(void)
                {
                    gl_FragColor = texture2D(texture, textureCoords);
                }
            );

        GLuint vertexShader;
        GLuint fragmentShader;
        GLint linked;

        vertexShader = loadShader(GL_VERTEX_SHADER, vShaderStr);
        fragmentShader = loadShader(GL_FRAGMENT_SHADER, fShaderStr);
        if (!vertexShader || !fragmentShader)
            return;

        m_program = glCreateProgram();
        if (!m_program)
            return;

        glAttachShader(m_program, vertexShader);
        glAttachShader(m_program, fragmentShader);

        glLinkProgram(m_program);
        glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
        if (!linked) {
            glDeleteProgram(m_program);
            m_program = 0;
        }

        m_vertexAttr = glGetAttribLocation(m_program, "vertex");
        m_textureCoordAttr = glGetAttribLocation(m_program, "textureCoordinates");
        m_textureUniform = glGetAttribLocation(m_program, "texture");
    }

    ShaderProgram()
        : m_program(0)
        , m_vertexAttr(0)
        , m_textureCoordAttr(0)
        , m_textureUniform(0)
    {
        initialize();
    }

    static ShaderProgram *m_instance;
    GLint m_program;
    GLuint m_vertexAttr;
    GLuint m_textureCoordAttr;
    GLuint m_textureUniform;
};
ShaderProgram* ShaderProgram::m_instance = NULL;

class GraphicsSurfacePaintDevice : public QOpenGLPaintDevice
{
public:
    GraphicsSurfacePaintDevice(IntSize size, QOpenGLContext *context, QSurface *surface, QOpenGLFramebufferObject *fbo)
    : QOpenGLPaintDevice(size)
    , m_context(context)
    , m_surface(surface)
    , m_fbo(fbo)
    {
        setPaintFlipped(true);
    }

    virtual ~GraphicsSurfacePaintDevice() { }

    void ensureActiveTarget()
    {
        if (QOpenGLContext::currentContext() != m_context)
            m_context->makeCurrent(m_surface);
        m_fbo->bind();
    }
private:
    QOpenGLContext *m_context;
    QSurface *m_surface;
    QOpenGLFramebufferObject *m_fbo;
};

class TileGLShared
{
public:
    static void setSize(IntSize& size) {
        m_size = size;
    }

    static QOpenGLFramebufferObject* fbo() {
        if (!m_fbo) {
            m_fbo = new QOpenGLFramebufferObject(m_size, QOpenGLFramebufferObject::CombinedDepthStencil, GL_TEXTURE_2D, GL_RGBA);
        }
        return m_fbo;
    }

    static GraphicsSurfacePaintDevice* paintDevice() {
        if (!m_paintDevice) {
            m_paintDevice = new GraphicsSurfacePaintDevice(m_size, GLSharedContext::context(), GLSharedContext::surface(), m_fbo);
        }
        return m_paintDevice;
    }

private:
    static IntSize m_size;
    static QOpenGLFramebufferObject *m_fbo;
    static GraphicsSurfacePaintDevice *m_paintDevice;
};
IntSize TileGLShared::m_size;
QOpenGLFramebufferObject *TileGLShared::m_fbo = NULL;
GraphicsSurfacePaintDevice* TileGLShared::m_paintDevice = NULL;

struct GraphicsSurfacePrivate {
    GraphicsSurfacePrivate(IntSize size, const PlatformGraphicsContext3D shareContext, GraphicsSurface::Flags flags)
        : m_isReceiver(false)
        , m_size(size)
        , m_surface(0)
        , m_context(0)
        , m_eglImage(EGL_NO_IMAGE_KHR)
        , m_foreignEglImage(EGL_NO_IMAGE_KHR)
        , m_texture(0)
        , m_previousContext(0)
        , m_fbo(0)
        , m_flags(flags)
        , m_prevFBO(0)
        , m_prevSrcRGB(0)
        , m_prevSrcAlpha(0)
        , m_prevDstRGB(0)
        , m_prevDstAlpha(0)
    {
        // we don't need to copy video frames, we already have an EGLImage
        if (m_flags & GraphicsSurface::IsVideo) {
            m_eglImage = (EGLImageKHR)1;
            return;
        }

        // we use the shared gl context to copy the canvas content
        if (m_flags & GraphicsSurface::IsCanvas) {
            m_context = GLSharedContext::context();
            m_surface = GLSharedContext::surface();
            makeCurrent();
        }

        // for tiles we use the shared gl context as well
        if (m_flags & GraphicsSurface::SupportsCopyToTexture) {
            TileGLShared::setSize(size);
            m_context = GLSharedContext::context();
            m_surface = GLSharedContext::surface();
            makeCurrent();
        }

        // for webgl we need to create a new gl context sharing with the webgl one
        if (m_flags & GraphicsSurface::IsWebGL) {
            QOffscreenSurface *surface = new QOffscreenSurface;
            surface->create();
            m_surface = static_cast<QSurface*>(surface);

            m_context = new QOpenGLContext;
            m_context->setShareContext(shareContext);
            m_context->create();
            makeCurrent();
        }


        glGenTextures(1, &m_texture);
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.width(), size.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        m_eglImage = eglCreateImageKHR(eglGetCurrentDisplay(), eglGetCurrentContext(), EGL_GL_TEXTURE_2D_KHR, (EGLClientBuffer)(m_texture), NULL);
        if (m_eglImage == EGL_NO_IMAGE_KHR)
            return;

        if (m_flags & (GraphicsSurface::IsCanvas | GraphicsSurface::IsWebGL)) {
            saveContextStatus();
            glGenFramebuffers(1, &m_fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
            restoreContextStatus();
        }

        doneCurrent();
    }

    GraphicsSurfacePrivate(uint32_t imageId, IntSize size, GraphicsSurface::Flags flags)
        : m_isReceiver(true)
        , m_size(size)
        , m_surface(0)
        , m_context(0)
        , m_eglImage((EGLImageKHR)imageId)
        , m_foreignEglImage(EGL_NO_IMAGE_KHR)
        , m_texture(0)
        , m_previousContext(0)
        , m_fbo(0)
        , m_flags(flags)
        , m_prevFBO(0)
        , m_prevSrcRGB(0)
        , m_prevSrcAlpha(0)
        , m_prevDstRGB(0)
        , m_prevDstAlpha(0)
    {
    }

    ~GraphicsSurfacePrivate()
    {
        if (!m_isReceiver) {
            if (m_flags & GraphicsSurface::IsVideo)
                return;

            makeCurrent();
            eglDestroyImageKHR(eglGetCurrentDisplay(), m_eglImage);
            glDeleteTextures(1, &m_texture);

            if (m_flags & (GraphicsSurface::IsCanvas | GraphicsSurface::IsWebGL)) {
                glDeleteFramebuffers(1, &m_fbo);
            }

            doneCurrent();

            if (m_flags & GraphicsSurface::IsWebGL) {
                delete m_context;
                static_cast<QOffscreenSurface*>(m_surface)->destroy();
                delete m_surface;
            }
        } else {
            if (m_texture)
                glDeleteTextures(1, &m_texture);
        }

    }

    void copyFromTexture(uint32_t texture, const IntRect& sourceRect)
    {
        makeCurrent();

        saveContextStatus();
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glBlendFunc(GL_ONE, GL_ZERO);
        glViewport(0, 0, m_size.width(), m_size.height());
        paintWithShader(texture, sourceRect, IntPoint(0, 0), m_size, true); 
        restoreContextStatus();

        doneCurrent();
    }

    void saveEGLImage(uint32_t image)
    {
        m_foreignEglImage = (EGLImageKHR)image;
    }

    uint32_t textureId()
    {
        if (!m_texture) {
            glGenTextures(1, &m_texture);
            glBindTexture(GL_TEXTURE_2D, m_texture);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            if (!(m_flags & GraphicsSurface::IsVideo))
                eglImageTargetTexture2DOES(GL_TEXTURE_2D, m_eglImage);
        }

        if (m_flags & GraphicsSurface::IsVideo) {
            glBindTexture(GL_TEXTURE_2D, m_texture);
            eglImageTargetTexture2DOES(GL_TEXTURE_2D, m_foreignEglImage);
        }

        return m_texture;
    }

    void makeCurrent()
    {
        if (QOpenGLContext::currentContext() != m_context) {
            m_previousContext = QOpenGLContext::currentContext();
            m_context->makeCurrent(m_surface);
        }
    }

    void doneCurrent()
    {
        if (m_previousContext) {
            m_previousContext->makeCurrent(m_previousContext->surface());
            m_previousContext = 0;
        }
    }

    IntSize size() const
    {
        return m_size;
    }

    void copyToGLTexture(uint32_t target, uint32_t id, const IntRect& targetRect, const IntPoint& offset, const IntSize& targetSize)
    {
        GLuint fbo;
        uint32_t origin = textureId();

        saveContextStatus();

        glBlendFunc(GL_ONE, GL_ZERO);
        glViewport(0, 0, targetSize.width(), targetSize.height());
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, id, 0);

        paintWithShader(origin, targetRect, offset, targetSize);

        glDeleteFramebuffers(1, &fbo);
        restoreContextStatus();
    }

    void paintWithShader(uint32_t texture, const IntRect& targetRect, const IntPoint& offset, const IntSize& targetSize, bool flip = false)
    {
        ShaderProgram *program = ShaderProgram::instance();

        glUseProgram(program->program());

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, texture);

        GLfloat xUnitsPerPixel = 2./targetSize.width();
        GLfloat yUnitsPerPixel = 2./targetSize.height();

        GLfloat xStart = -1 + (xUnitsPerPixel * targetRect.x());
        GLfloat xEnd =  -1 + (xUnitsPerPixel * (targetRect.width() + targetRect.x()));
        GLfloat yStart = -1 + (yUnitsPerPixel * targetRect.y());
        GLfloat yEnd =  -1 + (yUnitsPerPixel * (targetRect.height() + targetRect.y()));

        GLfloat afVertices[] = {
            xStart, yStart,
            xEnd, yStart,
            xStart, yEnd,
            xEnd, yEnd
        };
        glVertexAttribPointer(program->vertexAttr(), 2, GL_FLOAT, GL_FALSE, 0, afVertices);

        xUnitsPerPixel = 1./m_size.width();
        yUnitsPerPixel = 1./m_size.height();

        xStart = xUnitsPerPixel * offset.x();
        xEnd = xUnitsPerPixel * (offset.x() + targetRect.width());
        yStart = yUnitsPerPixel * offset.y();
        yEnd =  yUnitsPerPixel * (offset.y() + targetRect.height());

        if (!flip) {
            GLfloat aftextureCoordinates[] = {
                xStart, yStart,
                xEnd, yStart,
                xStart, yEnd,
                xEnd, yEnd
            };
            glVertexAttribPointer(program->textureCoordAttr(), 2, GL_FLOAT, GL_FALSE, 0, aftextureCoordinates);
        } else {
            GLfloat aftextureCoordinates[] = {
                xStart, yEnd,
                xEnd, yEnd,
                xStart, yStart,
                xEnd, yStart
            };
            glVertexAttribPointer(program->textureCoordAttr(), 2, GL_FLOAT, GL_FALSE, 0, aftextureCoordinates);
        }

        glUniform1i(program->textureUniform(), 0);

        glEnableVertexAttribArray(program->vertexAttr());
        glEnableVertexAttribArray(program->textureCoordAttr());

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glDisableVertexAttribArray(program->vertexAttr());
        glDisableVertexAttribArray(program->textureCoordAttr());

        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void saveContextStatus()
    {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_prevFBO);
        glGetIntegerv(GL_VIEWPORT, m_prevViewport);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &m_prevSrcAlpha);
        glGetIntegerv(GL_BLEND_SRC_RGB, &m_prevSrcRGB);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &m_prevDstAlpha);
        glGetIntegerv(GL_BLEND_DST_RGB, &m_prevDstRGB);
    }

    void restoreContextStatus()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_prevFBO);
        glViewport(m_prevViewport[0], m_prevViewport[1], m_prevViewport[2], m_prevViewport[3]);
        glBlendFuncSeparate(m_prevSrcRGB, m_prevDstRGB, m_prevSrcAlpha, m_prevDstAlpha);
    }

    bool m_isReceiver;
    IntSize m_size;
    QSurface *m_surface;
    QOpenGLContext *m_context;
    EGLImageKHR m_eglImage;
    EGLImageKHR m_foreignEglImage;
    GLuint m_texture;
    QOpenGLContext *m_previousContext;
    GLuint m_fbo;
    GraphicsSurface::Flags m_flags;
    GLint m_prevFBO;
    GLint m_prevSrcRGB;
    GLint m_prevSrcAlpha;
    GLint m_prevDstRGB;
    GLint m_prevDstAlpha;
    GLint m_prevViewport[4];
};

static bool resolveGLMethods()
{
    static bool resolved = false;

    if (resolved)
        return true;

    eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
    eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress("eglDestroyImageKHR");
    eglImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");

    resolved = eglCreateImageKHR && eglDestroyImageKHR && eglImageTargetTexture2DOES;

    return resolved;
}

GraphicsSurfaceToken GraphicsSurface::platformExport()
{
    return GraphicsSurfaceToken(*((uint32_t*)&m_private->m_eglImage));
}

uint32_t GraphicsSurface::platformGetTextureID()
{
    return m_private->textureId();
}

void GraphicsSurface::platformCopyToGLTexture(uint32_t target, uint32_t id, const IntRect& targetRect, const IntPoint& offset, const IntSize& targetSize)
{
    m_private->copyToGLTexture(target, id, targetRect, offset, targetSize);
}

void GraphicsSurface::platformCopyFromTexture(uint32_t texture, const IntRect& sourceRect)
{
    if (flags() & IsVideo)
        m_private->saveEGLImage(texture);
    else
        m_private->copyFromTexture(texture, sourceRect);
}


void GraphicsSurface::platformPaintToTextureMapper(TextureMapper* textureMapper, const FloatRect& targetRect, const TransformationMatrix& transform, float opacity)
{
    TextureMapperGL* texMapGL = static_cast<TextureMapperGL*>(textureMapper);
    texMapGL->drawTexture(platformGetTextureID(), TextureMapperGL::ShouldBlend, m_private->size(), targetRect, transform, opacity);
}

uint32_t GraphicsSurface::platformFrontBuffer() const
{
    if (flags() & IsVideo)
        return *((uint32_t*)&m_private->m_foreignEglImage);
    return 0;
}

uint32_t GraphicsSurface::platformSwapBuffers()
{
    return 0;
}

IntSize GraphicsSurface::platformSize() const
{
    return m_private->size();
}

PassRefPtr<GraphicsSurface> GraphicsSurface::platformCreate(const IntSize& size, Flags flags, const PlatformGraphicsContext3D shareContext)
{
    if (flags & SupportsCopyToTexture || flags & SupportsSingleBuffered)
        return PassRefPtr<GraphicsSurface>();

    RefPtr<GraphicsSurface> surface = adoptRef(new GraphicsSurface(size, flags));

    if (!resolveGLMethods())
        return PassRefPtr<GraphicsSurface>();
    surface->m_private = new GraphicsSurfacePrivate(size, shareContext, flags);

    return surface;
}

PassRefPtr<GraphicsSurface> GraphicsSurface::platformImport(const IntSize& size, Flags flags, const GraphicsSurfaceToken& token)
{
    if (flags & SupportsCopyToTexture || flags & SupportsSingleBuffered)
        return PassRefPtr<GraphicsSurface>();

    RefPtr<GraphicsSurface> surface = adoptRef(new GraphicsSurface(size, flags));

    if (!resolveGLMethods())
        return PassRefPtr<GraphicsSurface>();
    surface->m_private = new GraphicsSurfacePrivate(token.frontBufferHandle, size, flags);

    return surface;
}

char* GraphicsSurface::platformLock(const IntRect&, int* /*outputStride*/, LockOptions)
{
    return 0;
}

void GraphicsSurface::platformUnlock()
{
}

void GraphicsSurface::platformDestroy()
{
    delete m_private;
    m_private = 0;
}

PassOwnPtr<GraphicsContext> GraphicsSurface::platformBeginPaint(const IntSize&, char*, int)
{
    m_private->makeCurrent();

    glBindTexture(GL_TEXTURE_2D, TileGLShared::fbo()->texture());
    eglImageTargetTexture2DOES(GL_TEXTURE_2D, m_private->m_eglImage);
    QPainter *painter = new QPainter(TileGLShared::paintDevice());
    m_private->doneCurrent();

    OwnPtr<GraphicsContext> graphicsContext = adoptPtr(new GraphicsContext(painter));
    graphicsContext->takeOwnershipOfPlatformContext();

    return graphicsContext.release();
}

PassRefPtr<Image> GraphicsSurface::createReadOnlyImage(const IntRect& rect)
{
    notImplemented();
    return 0;
}

}
#endif
