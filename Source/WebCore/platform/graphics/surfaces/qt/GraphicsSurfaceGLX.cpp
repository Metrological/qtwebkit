/*
 Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies)

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Library General Public
 License as published by the Free Software Foundation; either
 version 2 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Library General Public License for more details.

 You should have received a copy of the GNU Library General Public License
 along with this library; see the file COPYING.LIB.  If not, write to
 the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "GraphicsSurface.h"

#if USE(GRAPHICS_SURFACE)

#include "TextureMapperGL.h"
// Qt headers must be included before glx headers.
#include <QCoreApplication>
#include <QOpenGLContext>
#include <QVector>
#include <QWindow>
#include <qpa/qplatformwindow.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>

namespace WebCore {

static long X11OverrideRedirect = 1L << 9;

static PFNGLXBINDTEXIMAGEEXTPROC pGlXBindTexImageEXT = 0;
static PFNGLXRELEASETEXIMAGEEXTPROC pGlXReleaseTexImageEXT = 0;
static PFNGLBINDFRAMEBUFFERPROC pGlBindFramebuffer = 0;
static PFNGLBLITFRAMEBUFFERPROC pGlBlitFramebuffer = 0;
static PFNGLGENFRAMEBUFFERSPROC pGlGenFramebuffers = 0;
static PFNGLDELETEFRAMEBUFFERSPROC pGlDeleteFramebuffers = 0;
static PFNGLFRAMEBUFFERTEXTURE2DPROC pGlFramebufferTexture2D = 0;

class OffScreenRootWindow {
public:
    OffScreenRootWindow()
    {
        ++refCount;
    }

    QWindow* get(Display* dpy)
    {
        if (!window) {
            window = new QWindow;
            window->setGeometry(QRect(-1, -1, 1, 1));
            window->create();
            XSetWindowAttributes attributes;
            attributes.override_redirect = true;
            XChangeWindowAttributes(dpy, window->handle()->winId(), X11OverrideRedirect, &attributes);
            window->show();
        }

        return window;
    }

    ~OffScreenRootWindow()
    {
        if (!--refCount) {
            delete window;
            window = 0;
        }
    }

private:
    static int refCount;
    static QWindow* window;
};

int OffScreenRootWindow::refCount = 0;
QWindow* OffScreenRootWindow::window = 0;

static const int glxSpec[] = {
    // The specification is a set key value pairs stored in a simple array.
    GLX_LEVEL,                          0,
    GLX_DRAWABLE_TYPE,                  GLX_PIXMAP_BIT | GLX_WINDOW_BIT,
    GLX_BIND_TO_TEXTURE_TARGETS_EXT,    GLX_TEXTURE_2D_BIT_EXT,
    GLX_BIND_TO_TEXTURE_RGB_EXT,        TRUE,
    0
};

static const int glxAttributes[] = {
    GLX_TEXTURE_FORMAT_EXT,
    GLX_TEXTURE_FORMAT_RGB_EXT,
    GLX_TEXTURE_TARGET_EXT,
    GLX_TEXTURE_2D_EXT,
    0
};

struct GraphicsSurfacePrivate {
    GraphicsSurfacePrivate(const PlatformGraphicsContext3D shareContext = 0)
        : m_display(0)
        , m_xPixmap(0)
        , m_glxPixmap(0)
        , m_glContext(adoptPtr(new QOpenGLContext))
        , m_detachedContext(0)
        , m_detachedSurface(0)
        , m_textureIsYInverted(false)
        , m_hasAlpha(false)
    {
        QSurface* currentSurface = 0;
        QOpenGLContext* currentContext = QOpenGLContext::currentContext();
        if (currentContext)
            currentSurface = currentContext->surface();

        m_display = XOpenDisplay(0);
        m_glContext->setShareContext(shareContext);
        m_glContext->create();

        // The GLX implementation of QOpenGLContext will reset the current context when create is being called.
        // Therefore we have to make the previous context current again.
        if (currentContext)
            currentContext->makeCurrent(currentSurface);
    }

    ~GraphicsSurfacePrivate()
    {
        if (m_glxPixmap)
            glXDestroyPixmap(m_display, m_glxPixmap);
        m_glxPixmap = 0;

        if (m_xPixmap)
            XFreePixmap(m_display, m_xPixmap);
        m_xPixmap = 0;

        if (m_display)
            XCloseDisplay(m_display);
        m_display = 0;
    }

    uint32_t createSurface(const IntSize& size)
    {
        m_surface = adoptPtr(new QWindow(m_offScreenWindow.get(m_display)));
        m_surface->setSurfaceType(QSurface::OpenGLSurface);
        m_surface->setGeometry(0, 0, size.width(), size.height());
        m_surface->create();
        XCompositeRedirectWindow(m_display, m_surface->handle()->winId(), CompositeRedirectManual);

        // Make sure the XRender Extension is available.
        int eventBasep, errorBasep;
        if (!XRenderQueryExtension(m_display, &eventBasep, &errorBasep))
            return 0;

        m_surface->show();

        return m_surface->handle()->winId();
    }

    void createPixmap(uint32_t winId)
    {
        XWindowAttributes attr;
        XGetWindowAttributes(m_display, winId, &attr);

        XRenderPictFormat* format = XRenderFindVisualFormat(m_display, attr.visual);
        m_hasAlpha = (format->type == PictTypeDirect && format->direct.alphaMask);
        m_size = IntSize(attr.width, attr.height);

        int numberOfConfigs;
        GLXFBConfig* configs = glXChooseFBConfig(m_display, XDefaultScreen(m_display), glxSpec, &numberOfConfigs);

        m_xPixmap = XCompositeNameWindowPixmap(m_display, winId);
        m_glxPixmap = glXCreatePixmap(m_display, *configs, m_xPixmap, glxAttributes);

        uint inverted = 0;
        glXQueryDrawable(m_display, m_glxPixmap, GLX_Y_INVERTED_EXT, &inverted);
        m_textureIsYInverted = !!inverted;

        XFree(configs);
    }

    void makeCurrent()
    {
        m_detachedContext = QOpenGLContext::currentContext();
        if (m_detachedContext)
            m_detachedSurface = m_detachedContext->surface();

        if (m_surface && m_glContext)
            m_glContext->makeCurrent(m_surface.get());
    }

    void doneCurrent()
    {
        if (m_detachedContext)
            m_detachedContext->makeCurrent(m_detachedSurface);
    }

    void swapBuffers()
    {
        // If there is a xpixmap, we are on the reading side and do not want to swap any buffers.
        // The buffers are being switched on the writing side, the reading side just reads
        // whatever texture the XWindow contains.
        if (m_xPixmap)
            return;

        if (!m_surface->isVisible())
            return;

        // Creating and exposing the surface is asynchronous. Therefore we have to wait here
        // before swapping the buffers. This should only be the case for the very first frame.
        while (!m_surface->isExposed())
            QCoreApplication::processEvents();

        QOpenGLContext* glContext = QOpenGLContext::currentContext();
        if (m_surface && glContext) {
            GLint oldFBO;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);
            pGlBindFramebuffer(GL_FRAMEBUFFER, glContext->defaultFramebufferObject());
            glContext->swapBuffers(m_surface.get());
            pGlBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
        }
    }

    void copyFromTexture(uint32_t texture, const IntRect& sourceRect)
    {
        makeCurrent();
        int x = sourceRect.x();
        int y = sourceRect.y();
        int width = sourceRect.width();
        int height = sourceRect.height();

        glPushAttrib(GL_ALL_ATTRIB_BITS);
        GLint previousFBO;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFBO);

        GLuint originFBO;
        pGlGenFramebuffers(1, &originFBO);
        pGlBindFramebuffer(GL_READ_FRAMEBUFFER, originFBO);
        glBindTexture(GL_TEXTURE_2D, texture);
        pGlFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

        pGlBindFramebuffer(GL_DRAW_FRAMEBUFFER, glContext()->defaultFramebufferObject());
        pGlBlitFramebuffer(x, y, width, height, x, y, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);

        pGlFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        pGlBindFramebuffer(GL_FRAMEBUFFER, previousFBO);
        pGlDeleteFramebuffers(1, &originFBO);

        glPopAttrib();
        doneCurrent();
    }


    Display* display() const { return m_display; }

    GLXPixmap glxPixmap() const { return m_glxPixmap; }

    IntSize size() const { return m_size; }

    QOpenGLContext* glContext() { return m_glContext.get(); }

private:
    OffScreenRootWindow m_offScreenWindow;
    IntSize m_size;
    Display* m_display;
    Pixmap m_xPixmap;
    GLXPixmap m_glxPixmap;
    OwnPtr<QWindow> m_surface;
    OwnPtr<QOpenGLContext> m_glContext;
    QOpenGLContext* m_detachedContext;
    QSurface* m_detachedSurface;
    bool m_textureIsYInverted;
    bool m_hasAlpha;
};

static bool resolveGLMethods(GraphicsSurfacePrivate* p)
{
    static bool resolved = false;
    if (resolved)
        return true;

    QOpenGLContext* glContext = p->glContext();
    pGlXBindTexImageEXT = reinterpret_cast<PFNGLXBINDTEXIMAGEEXTPROC>(glContext->getProcAddress("glXBindTexImageEXT"));
    pGlXReleaseTexImageEXT = reinterpret_cast<PFNGLXRELEASETEXIMAGEEXTPROC>(glContext->getProcAddress("glXReleaseTexImageEXT"));
    pGlBindFramebuffer = reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(glContext->getProcAddress("glBindFramebuffer"));
    pGlBlitFramebuffer = reinterpret_cast<PFNGLBLITFRAMEBUFFERPROC>(glContext->getProcAddress("glBlitFramebuffer"));

    pGlGenFramebuffers = reinterpret_cast<PFNGLGENFRAMEBUFFERSPROC>(glContext->getProcAddress("glGenFramebuffers"));
    pGlDeleteFramebuffers = reinterpret_cast<PFNGLDELETEFRAMEBUFFERSPROC>(glContext->getProcAddress("glDeleteFramebuffers"));
    pGlFramebufferTexture2D = reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DPROC>(glContext->getProcAddress("glFramebufferTexture2D"));

    resolved = pGlBlitFramebuffer && pGlBindFramebuffer && pGlXBindTexImageEXT && pGlXReleaseTexImageEXT;

    return resolved;
}

GraphicsSurfaceToken GraphicsSurface::platformExport()
{
    return GraphicsSurfaceToken(m_platformSurface);
}

uint32_t GraphicsSurface::platformGetTextureID()
{
    if (!m_texture) {
        glGenTextures(1, &m_texture);
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        pGlXBindTexImageEXT(m_private->display(), m_private->glxPixmap(), GLX_FRONT_EXT, 0);
    }

    return m_texture;
}

void GraphicsSurface::platformCopyToGLTexture(uint32_t target, uint32_t id, const IntRect& targetRect, const IntPoint& offset)
{
    // This is not supported by GLX/Xcomposite.
}

void GraphicsSurface::platformCopyFromTexture(uint32_t texture, const IntRect& sourceRect)
{
    m_private->copyFromTexture(texture, sourceRect);
}


void GraphicsSurface::platformPaintToTextureMapper(TextureMapper* textureMapper, const FloatRect& targetRect, const TransformationMatrix& transform, float opacity, BitmapTexture* mask)
{
    TransformationMatrix adjustedTransform = transform;
    adjustedTransform.multiply(TransformationMatrix::rectToRect(FloatRect(FloatPoint::zero(), m_size), targetRect));
    static_cast<TextureMapperGL*>(textureMapper)->drawTexture(platformGetTextureID(), 0, m_size, targetRect, adjustedTransform, opacity, mask);
}

uint32_t GraphicsSurface::platformFrontBuffer() const
{
    return 0;
}

uint32_t GraphicsSurface::platformSwapBuffers()
{
    m_private->swapBuffers();
    return 0;
}

PassRefPtr<GraphicsSurface> GraphicsSurface::platformCreate(const IntSize& size, Flags flags, const PlatformGraphicsContext3D shareContext)
{
    // X11 does not support CopyToTexture, so we do not create a GraphicsSurface if this is requested.
    // GraphicsSurfaceGLX uses an XWindow as native surface. This one always has a front and a back buffer.
    // Therefore single buffered GraphicsSurfaces are not supported.
    if (flags & SupportsCopyToTexture || flags & SupportsSingleBuffered)
        return PassRefPtr<GraphicsSurface>();

    RefPtr<GraphicsSurface> surface = adoptRef(new GraphicsSurface(size, flags));

    surface->m_private = new GraphicsSurfacePrivate(shareContext);
    if (!resolveGLMethods(surface->m_private))
        return PassRefPtr<GraphicsSurface>();

    surface->m_platformSurface = surface->m_private->createSurface(size);

    return surface;
}

PassRefPtr<GraphicsSurface> GraphicsSurface::platformImport(const IntSize& size, Flags flags, const GraphicsSurfaceToken& token)
{
    // X11 does not support CopyToTexture, so we do not create a GraphicsSurface if this is requested.
    // GraphicsSurfaceGLX uses an XWindow as native surface. This one always has a front and a back buffer.
    // Therefore single buffered GraphicsSurfaces are not supported.
    if (flags & SupportsCopyToTexture || flags & SupportsSingleBuffered)
        return PassRefPtr<GraphicsSurface>();

    RefPtr<GraphicsSurface> surface = adoptRef(new GraphicsSurface(size, flags));

    surface->m_private = new GraphicsSurfacePrivate();
    if (!resolveGLMethods(surface->m_private))
        return PassRefPtr<GraphicsSurface>();

    surface->m_platformSurface = token.frontBufferHandle;

    surface->m_private->createPixmap(surface->m_platformSurface);
    surface->m_size = surface->m_private->size();

    return surface;
}

char* GraphicsSurface::platformLock(const IntRect& rect, int* outputStride, LockOptions lockOptions)
{
    // GraphicsSurface is currently only being used for WebGL, which does not require this locking mechanism.
    return 0;
}

void GraphicsSurface::platformUnlock()
{
    // GraphicsSurface is currently only being used for WebGL, which does not require this locking mechanism.
}

void GraphicsSurface::platformDestroy()
{
    if (m_texture) {
        pGlXReleaseTexImageEXT(m_private->display(), m_private->glxPixmap(), GLX_FRONT_EXT);
        glDeleteTextures(1, &m_texture);
    }

    delete m_private;
    m_private = 0;
}

}
#endif