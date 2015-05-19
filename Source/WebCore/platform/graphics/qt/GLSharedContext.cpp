#include "config.h"
#include "GLSharedContext.h"
#include <stdio.h>

namespace WebCore {

void GLSharedContext::setContext(QOpenGLContext *context) {
    m_context = context;
    m_surface = m_context->surface();
}

QOpenGLContext* GLSharedContext::context(bool forceCreation)
{
    if (!m_context && forceCreation)
        initialize();
    return m_context;
}

QSurface* GLSharedContext::surface()
{
    if (!m_surface)
        initialize();
    return m_surface;
}


void GLSharedContext::makeCurrent() {
    if (!m_context)
        initialize();

    if (QOpenGLContext::currentContext() != m_context) {
        m_context->makeCurrent(m_surface);
    }
}

void GLSharedContext::initialize()
{
    QOffscreenSurface *surface = new QOffscreenSurface();
    surface->create();
    m_surface = static_cast<QSurface*>(surface);
    m_context = new QOpenGLContext();
    m_context->create();
    makeCurrent();
}

QSurface* GLSharedContext::m_surface = 0;
QOpenGLContext* GLSharedContext::m_context = 0;
}
