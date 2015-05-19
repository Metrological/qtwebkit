#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QOpenGLContext>

#ifndef GLSharedContext_h
#define GLSharedContext_h

namespace WebCore {

class GLSharedContext {
public:
    static void setContext(QOpenGLContext *context);
    static QOpenGLContext* context(bool forceCreation = true);
    static QSurface* surface();
    static void makeCurrent();

private:
    static void initialize();

    static QSurface *m_surface;
    static QOpenGLContext *m_context;
};

}

#endif
