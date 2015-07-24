/*
 * Copyright (C) 2007, 2009 Apple Inc.  All rights reserved.
 * Copyright (C) 2007 Collabora Ltd.  All rights reserved.
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 * Copyright (C) 2009 Gustavo Noronha Silva <gns@gnome.org>
 * Copyright (C) 2009, 2010 Igalia S.L
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "MediaPlayerPrivateGStreamerBase.h"

#if ENABLE(VIDEO) && USE(GSTREAMER)

#include "ColorSpace.h"
#include "GStreamerUtilities.h"
#include "GraphicsContext.h"
#include "GraphicsTypes.h"
#include "ImageGStreamer.h"
#include "ImageOrientation.h"
#include "IntRect.h"
#include "MediaPlayer.h"
#include "NotImplemented.h"
#include "VideoSinkGStreamer.h"
#include "WebKitWebSourceGStreamer.h"
#include <gst/gst.h>
#include <wtf/glib/GMutexLocker.h>
#include <wtf/text/CString.h>

#include <gst/audio/streamvolume.h>
#include <gst/video/gstvideometa.h>

#if USE(GSTREAMER_GL)
#define GST_USE_UNSTABLE_API
#include <gst/gl/gstglmemory.h>
#undef GST_USE_UNSTABLE_API
#endif

#if GST_CHECK_VERSION(1, 1, 0) && USE(TEXTURE_MAPPER_GL)
//#include "BitmapTextureGL.h"
//#include "BitmapTexturePool.h"
#include "TextureMapperGL.h"
#endif

#define WL_EGL_PLATFORM

#if USE(OPENGL_ES_2)
#if GST_CHECK_VERSION(1, 3, 0)
#define GST_USE_UNSTABLE_API
#include <gst/gl/egl/gsteglimagememory.h>
#undef GST_USE_UNSTABLE_API
#endif
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif

#include <EGL/egl.h>

struct _EGLDetails {
    EGLDisplay display;
    EGLContext context;
    EGLSurface draw;
    EGLSurface read;
};

#if USE(GSTREAMER_GL)
#include "GLContext.h"

#define GST_USE_UNSTABLE_API
#include <gst/gl/gl.h>
#undef GST_USE_UNSTABLE_API

#if USE(GLX)
#include "GLContextGLX.h"
#include <gst/gl/x11/gstgldisplay_x11.h>
#elif USE(EGL)
#include "GLContextEGL.h"
#include <gst/gl/egl/gstgldisplay_egl.h>
#endif

#if PLATFORM(X11)
#include "PlatformDisplayX11.h"
#elif PLATFORM(WAYLAND)
#include "PlatformDisplayWayland.h"
#endif

// gstglapi.h may include eglplatform.h and it includes X.h, which
// defines None, breaking MediaPlayer::None enum
#if PLATFORM(X11) && GST_GL_HAVE_PLATFORM_EGL
#undef None
#endif
#endif // USE(GSTREAMER_GL)

GST_DEBUG_CATEGORY(webkit_media_player_debug);
#define GST_CAT_DEFAULT webkit_media_player_debug

using namespace std;

namespace WebCore {

static int greatestCommonDivisor(int a, int b)
{
    while (b) {
        int temp = a;
        a = b;
        b = temp % b;
    }

    return ABS(a);
}

static void mediaPlayerPrivateVolumeChangedCallback(GObject*, GParamSpec*, MediaPlayerPrivateGStreamerBase* player)
{
    // This is called when m_volumeElement receives the notify::volume signal.
    LOG_MEDIA_MESSAGE("Volume changed to: %f", player->volume());
    player->volumeChanged();
}

static void mediaPlayerPrivateMuteChangedCallback(GObject*, GParamSpec*, MediaPlayerPrivateGStreamerBase* player)
{
    // This is called when m_volumeElement receives the notify::mute signal.
    player->muteChanged();
}

static void mediaPlayerPrivateRepaintCallback(WebKitVideoSink*, GstSample* sample, MediaPlayerPrivateGStreamerBase* playerPrivate)
{
    playerPrivate->triggerRepaint(sample);
}

static void mediaPlayerPrivateDrainCallback(WebKitVideoSink*, MediaPlayerPrivateGStreamerBase* playerPrivate)
{
    playerPrivate->triggerDrain();
}

#if USE(GSTREAMER_GL)
static gboolean mediaPlayerPrivateDrawCallback(GstElement*, GstContext*, GstSample* sample, MediaPlayerPrivateGStreamerBase* playerPrivate)
{
    playerPrivate->triggerRepaint(sample);
    return TRUE;
}
#endif

static void mediaPlayerPrivateNeedContextMessageCallback(GstBus*, GstMessage* message, MediaPlayerPrivateGStreamerBase* player)
{
    player->handleNeedContextMessage(message);
}

MediaPlayerPrivateGStreamerBase::MediaPlayerPrivateGStreamerBase(MediaPlayer* player)
    : m_player(player)
    , m_fpsSink(0)
    , m_readyState(MediaPlayer::HaveNothing)
    , m_networkState(MediaPlayer::Empty)
    , m_isEndReached(false)
    , m_sample(0)
    , m_volumeTimerHandler("[WebKit] MediaPlayerPrivateGStreamerBase::volumeChanged", std::function<void()>(std::bind(&MediaPlayerPrivateGStreamerBase::notifyPlayerOfVolumeChange, this)))
    , m_muteTimerHandler("[WebKit] MediaPlayerPrivateGStreamerBase::muteChanged", std::function<void()>(std::bind(&MediaPlayerPrivateGStreamerBase::notifyPlayerOfMute, this)))
    , m_repaintHandler(0)
    , m_volumeSignalHandler(0)
    , m_muteSignalHandler(0)
    , m_usingFallbackVideoSink(false)
{
    g_mutex_init(&m_sampleMutex);
#if USE(GSTREAMER_GL)
    g_cond_init(&m_drawCondition);
    g_mutex_init(&m_drawMutex);
#endif
#if USE(COORDINATED_GRAPHICS_THREADED)
    m_platformLayerProxy = adoptRef(new TextureMapperPlatformLayerProxy());
    g_cond_init(&m_updateCondition);
    g_mutex_init(&m_updateMutex);
#endif
}

MediaPlayerPrivateGStreamerBase::~MediaPlayerPrivateGStreamerBase()
{
    if (m_repaintHandler) {
        g_signal_handler_disconnect(m_videoSink.get(), m_repaintHandler);
        m_repaintHandler = 0;
    }

    if (m_drainHandler)
        g_signal_handler_disconnect(m_videoSink.get(), m_drainHandler);
    m_drainHandler = 0;

    g_mutex_clear(&m_sampleMutex);

    m_player = 0;

    if (m_volumeSignalHandler) {
        g_signal_handler_disconnect(m_volumeElement.get(), m_volumeSignalHandler);
        m_volumeSignalHandler = 0;
    }

    if (m_muteSignalHandler) {
        g_signal_handler_disconnect(m_volumeElement.get(), m_muteSignalHandler);
        m_muteSignalHandler = 0;
    }

#if USE(GSTREAMER_GL)
    g_cond_clear(&m_drawCondition);
    g_mutex_clear(&m_drawMutex);
#endif

#if USE(COORDINATED_GRAPHICS_THREADED)
    g_cond_clear(&m_updateCondition);
    g_mutex_clear(&m_updateMutex);
#endif

    if (m_pipeline) {
        GRefPtr<GstBus> bus = adoptGRef(gst_pipeline_get_bus(GST_PIPELINE(m_pipeline.get())));
        ASSERT(bus);
        g_signal_handlers_disconnect_by_func(bus.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateNeedContextMessageCallback), this);
        gst_bus_disable_sync_message_emission(bus.get());
        m_pipeline.clear();
    }

#if USE(TEXTURE_MAPPER_GL) && !USE(COORDINATED_GRAPHICS)
    if (client())
        client()->platformLayerWillBeDestroyed();
#endif
}

void MediaPlayerPrivateGStreamerBase::setPipeline(GstElement* pipeline)
{
    m_pipeline = pipeline;

    GRefPtr<GstBus> bus = adoptGRef(gst_pipeline_get_bus(GST_PIPELINE(m_pipeline.get())));
    gst_bus_enable_sync_message_emission(bus.get());
    g_signal_connect(bus.get(), "sync-message::need-context", G_CALLBACK(mediaPlayerPrivateNeedContextMessageCallback), this);
}

void MediaPlayerPrivateGStreamerBase::handleNeedContextMessage(GstMessage* message)
{
#if USE(GSTREAMER_GL)
    const gchar* contextType;
    gst_message_parse_context_type(message, &contextType);

    if (!ensureGstGLContext())
        return;

    if (!g_strcmp0(contextType, GST_GL_DISPLAY_CONTEXT_TYPE)) {
        GstContext* displayContext = gst_context_new(GST_GL_DISPLAY_CONTEXT_TYPE, TRUE);
        gst_context_set_gl_display(displayContext, m_glDisplay.get());
        gst_element_set_context(GST_ELEMENT(message->src), displayContext);
        return;
    }

    if (!g_strcmp0(contextType, "gst.gl.app_context")) {
        GstContext* appContext = gst_context_new("gst.gl.app_context", TRUE);
        GstStructure* structure = gst_context_writable_structure(appContext);
        gst_structure_set(structure, "context", GST_GL_TYPE_CONTEXT, m_glContext.get(), nullptr);
        gst_element_set_context(GST_ELEMENT(message->src), appContext);
        return;
    }
#else
    UNUSED_PARAM(message);
#endif // USE(GSTREAMER_GL)
}

#if USE(GSTREAMER_GL)
bool MediaPlayerPrivateGStreamerBase::ensureGstGLContext()
{
    if (m_glContext)
        return true;

    if (!m_glDisplay) {
        const auto& sharedDisplay = PlatformDisplay::sharedDisplay();
#if PLATFORM(X11)
        m_glDisplay = GST_GL_DISPLAY(gst_gl_display_x11_new_with_display(downcast<PlatformDisplayX11>(sharedDisplay).native()));
#elif PLATFORM(WAYLAND)
        m_glDisplay = GST_GL_DISPLAY(gst_gl_display_egl_new_with_egl_display(downcast<PlatformDisplayWayland>(sharedDisplay).native()));
#endif
    }

    GLContext* webkitContext = GLContext::sharingContext();
    // EGL and GLX are mutually exclusive, no need for ifdefs here.
    GstGLPlatform glPlatform = webkitContext->isEGLContext() ? GST_GL_PLATFORM_EGL : GST_GL_PLATFORM_GLX;

#if USE(OPENGL_ES_2)
    GstGLAPI glAPI = GST_GL_API_GLES2;
#elif USE(OPENGL)
    GstGLAPI glAPI = GST_GL_API_OPENGL;
#else
    ASSERT_NOT_REACHED();
#endif

    PlatformGraphicsContext3D contextHandle = webkitContext->platformContext();
    if (!contextHandle)
        return false;

    m_glContext = gst_gl_context_new_wrapped(m_glDisplay.get(), reinterpret_cast<guintptr>(contextHandle), glPlatform, glAPI);

    return true;
}
#endif // USE(GSTREAMER_GL)

// Returns the size of the video
IntSize MediaPlayerPrivateGStreamerBase::naturalSize() const
{
    if (!hasVideo())
        return IntSize();

    if (!m_videoSize.isEmpty())
        return m_videoSize;

    WebCore::GMutexLocker lock(&m_sampleMutex);

    GRefPtr<GstCaps> caps;
    // We may not have enough data available for the video sink yet,
    // but the demuxer might haver it already.
    if (!GST_IS_SAMPLE(m_sample.get())) {
#if ENABLE(MEDIA_SOURCE)
        caps = currentDemuxerCaps();
#else
        return IntSize();
#endif
    }

    if (GST_IS_SAMPLE(m_sample.get()) && !caps)
        caps = gst_sample_get_caps(m_sample.get());

    if (!caps)
        return IntSize();

    // TODO: handle possible clean aperture data. See
    // https://bugzilla.gnome.org/show_bug.cgi?id=596571
    // TODO: handle possible transformation matrix. See
    // https://bugzilla.gnome.org/show_bug.cgi?id=596326

    // Get the video PAR and original size, if this fails the
    // video-sink has likely not yet negotiated its caps.
    int pixelAspectRatioNumerator, pixelAspectRatioDenominator, stride;
    IntSize originalSize;
    GstVideoFormat format;
    if (!getVideoSizeAndFormatFromCaps(caps.get(), originalSize, format, pixelAspectRatioNumerator, pixelAspectRatioDenominator, stride))
        return IntSize();

    LOG_MEDIA_MESSAGE("Original video size: %dx%d", originalSize.width(), originalSize.height());
    LOG_MEDIA_MESSAGE("Pixel aspect ratio: %d/%d", pixelAspectRatioNumerator, pixelAspectRatioDenominator);

    // Calculate DAR based on PAR and video size.
    int displayWidth = originalSize.width() * pixelAspectRatioNumerator;
    int displayHeight = originalSize.height() * pixelAspectRatioDenominator;

    // Divide display width and height by their GCD to avoid possible overflows.
    int displayAspectRatioGCD = greatestCommonDivisor(displayWidth, displayHeight);
    displayWidth /= displayAspectRatioGCD;
    displayHeight /= displayAspectRatioGCD;

    // Apply DAR to original video size. This is the same behavior as in xvimagesink's setcaps function.
    guint64 width = 0, height = 0;
    if (!(originalSize.height() % displayHeight)) {
        LOG_MEDIA_MESSAGE("Keeping video original height");
        width = gst_util_uint64_scale_int(originalSize.height(), displayWidth, displayHeight);
        height = static_cast<guint64>(originalSize.height());
    } else if (!(originalSize.width() % displayWidth)) {
        LOG_MEDIA_MESSAGE("Keeping video original width");
        height = gst_util_uint64_scale_int(originalSize.width(), displayHeight, displayWidth);
        width = static_cast<guint64>(originalSize.width());
    } else {
        LOG_MEDIA_MESSAGE("Approximating while keeping original video height");
        width = gst_util_uint64_scale_int(originalSize.height(), displayWidth, displayHeight);
        height = static_cast<guint64>(originalSize.height());
    }

    LOG_MEDIA_MESSAGE("Natural size: %" G_GUINT64_FORMAT "x%" G_GUINT64_FORMAT, width, height);
    m_videoSize = IntSize(static_cast<int>(width), static_cast<int>(height));
    return m_videoSize;
}

void MediaPlayerPrivateGStreamerBase::setVolume(float volume)
{
    if (!m_volumeElement)
        return;

    LOG_MEDIA_MESSAGE("Setting volume: %f", volume);
    gst_stream_volume_set_volume(m_volumeElement.get(), GST_STREAM_VOLUME_FORMAT_CUBIC, static_cast<double>(volume));
}

float MediaPlayerPrivateGStreamerBase::volume() const
{
    if (!m_volumeElement)
        return 0;

    return gst_stream_volume_get_volume(m_volumeElement.get(), GST_STREAM_VOLUME_FORMAT_CUBIC);
}


void MediaPlayerPrivateGStreamerBase::notifyPlayerOfVolumeChange()
{
    if (!m_player || !m_volumeElement)
        return;
    double volume;
    volume = gst_stream_volume_get_volume(m_volumeElement.get(), GST_STREAM_VOLUME_FORMAT_CUBIC);
    // get_volume() can return values superior to 1.0 if the user
    // applies software user gain via third party application (GNOME
    // volume control for instance).
    volume = CLAMP(volume, 0.0, 1.0);
    m_player->volumeChanged(static_cast<float>(volume));
}

void MediaPlayerPrivateGStreamerBase::volumeChanged()
{
    m_volumeTimerHandler.schedule();
}

MediaPlayer::NetworkState MediaPlayerPrivateGStreamerBase::networkState() const
{
    return m_networkState;
}

MediaPlayer::ReadyState MediaPlayerPrivateGStreamerBase::readyState() const
{
    return m_readyState;
}

void MediaPlayerPrivateGStreamerBase::sizeChanged()
{
    notImplemented();
}

void MediaPlayerPrivateGStreamerBase::setMuted(bool muted)
{
    if (!m_volumeElement)
        return;

    g_object_set(m_volumeElement.get(), "mute", muted, NULL);
}

bool MediaPlayerPrivateGStreamerBase::muted() const
{
    if (!m_volumeElement)
        return false;

    bool muted;
    g_object_get(m_volumeElement.get(), "mute", &muted, NULL);
    return muted;
}

void MediaPlayerPrivateGStreamerBase::notifyPlayerOfMute()
{
    if (!m_player || !m_volumeElement)
        return;

    gboolean muted;
    g_object_get(m_volumeElement.get(), "mute", &muted, NULL);
    m_player->muteChanged(static_cast<bool>(muted));
}

void MediaPlayerPrivateGStreamerBase::muteChanged()
{
    m_muteTimerHandler.schedule();
}

#if USE(TEXTURE_MAPPER_GL) && !USE(COORDINATED_GRAPHICS_MULTIPROCESS)
PassRefPtr<BitmapTexture> MediaPlayerPrivateGStreamerBase::updateTexture(TextureMapper* textureMapper)
{
    WebCore::GMutexLocker lock(&m_sampleMutex);
    if (!GST_IS_SAMPLE(m_sample.get()))
        return nullptr;

    GstCaps* caps = gst_sample_get_caps(m_sample.get());
    if (!caps)
        return nullptr;

    GstVideoInfo videoInfo;
    gst_video_info_init(&videoInfo);
    if (!gst_video_info_from_caps(&videoInfo, caps))
        return nullptr;

    IntSize size = IntSize(GST_VIDEO_INFO_WIDTH(&videoInfo), GST_VIDEO_INFO_HEIGHT(&videoInfo));
    //RefPtr<BitmapTexture> texture = textureMapper->acquireTextureFromPool(size, GST_VIDEO_INFO_HAS_ALPHA(&videoInfo));
    RefPtr<BitmapTexture> texture = textureMapper->acquireTextureFromPool(size/*, GST_VIDEO_INFO_HAS_ALPHA(&videoInfo)*/);
    GstBuffer* buffer = gst_sample_get_buffer(m_sample.get());

#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 1, 2)
    GstMemory *mem;
    if (gst_buffer_n_memory (buffer) >= 1) {
        if ((mem = gst_buffer_peek_memory (buffer, 0)) && gst_is_egl_image_memory (mem)) {
            guint n, i;

            n = gst_buffer_n_memory (buffer);

            n = 1; // FIXME
            const BitmapTextureGL* textureGL = static_cast<const BitmapTextureGL*>(texture.get()); // FIXME

            for (i = 0; i < n; i++) {
                mem = gst_buffer_peek_memory (buffer, i);

                g_assert (gst_is_egl_image_memory (mem));

                if (i == 0)
                    glActiveTexture (GL_TEXTURE0);
                else if (i == 1)
                    glActiveTexture (GL_TEXTURE1);
                else if (i == 2)
                    glActiveTexture (GL_TEXTURE2);

                glBindTexture (GL_TEXTURE_2D, textureGL->id()); // FIXME
                glEGLImageTargetTexture2DOES (GL_TEXTURE_2D,
                    gst_egl_image_memory_get_image (mem));

                m_orientation = gst_egl_image_memory_get_orientation (mem);
                if (m_orientation != GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL
                    && m_orientation != GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_FLIP) {
                    LOG_ERROR("MediaPlayerPrivateGStreamerBase::updateTexture: invalid GstEGLImage orientation");
                }
            }

            return texture;
        }
    }

    return 0;
#else
#if GST_CHECK_VERSION(1, 1, 0)
    GstVideoGLTextureUploadMeta* meta;
    if ((meta = gst_buffer_get_video_gl_texture_upload_meta(buffer))) {
        if (meta->n_textures == 1) { // BRGx & BGRA formats use only one texture.
            const BitmapTextureGL* textureGL = static_cast<const BitmapTextureGL*>(texture.get());
            guint ids[4] = { textureGL->id(), 0, 0, 0 };

            if (gst_video_gl_texture_upload_meta_upload(meta, ids))
                return texture;
        }
    }
#endif

    // Right now the TextureMapper only supports chromas with one plane
    ASSERT(GST_VIDEO_INFO_N_PLANES(&videoInfo) == 1);

    GstVideoFrame videoFrame;
    if (!gst_video_frame_map(&videoFrame, &videoInfo, buffer, GST_MAP_READ))
        return nullptr;

    int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&videoFrame, 0);
    const void* srcData = GST_VIDEO_FRAME_PLANE_DATA(&videoFrame, 0);
    texture->updateContents(srcData, WebCore::IntRect(WebCore::IntPoint(0, 0), size), WebCore::IntPoint(0, 0), stride, BitmapTexture::UpdateCannotModifyOriginalImageData);
    gst_video_frame_unmap(&videoFrame);
    return texture;
#endif
    return 0;
}

void MediaPlayerPrivateGStreamerBase::updateTexture(BitmapTextureGL& texture, GstVideoInfo& videoInfo)
{
    IntSize size = IntSize(GST_VIDEO_INFO_WIDTH(&videoInfo), GST_VIDEO_INFO_HEIGHT(&videoInfo));
    GstBuffer* buffer = gst_sample_get_buffer(m_sample.get());

#if USE(OPENGL_ES_2) && GST_CHECK_VERSION(1, 1, 2)
    GstMemory *mem;
    if (gst_buffer_n_memory (buffer) >= 1) {
        if ((mem = gst_buffer_peek_memory (buffer, 0)) && gst_is_egl_image_memory (mem)) {
            guint n, i;

            n = gst_buffer_n_memory (buffer);

            n = 1; // FIXME

            for (i = 0; i < n; i++) {
                mem = gst_buffer_peek_memory (buffer, i);

                g_assert (gst_is_egl_image_memory (mem));

                if (i == 0)
                    glActiveTexture (GL_TEXTURE0);
                else if (i == 1)
                    glActiveTexture (GL_TEXTURE1);
                else if (i == 2)
                    glActiveTexture (GL_TEXTURE2);

                glBindTexture (GL_TEXTURE_2D, texture.id());
                glEGLImageTargetTexture2DOES (GL_TEXTURE_2D,
                    gst_egl_image_memory_get_image (mem));

                m_orientation = gst_egl_image_memory_get_orientation (mem);
                if (m_orientation != GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL
                    && m_orientation != GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_FLIP) {
                    LOG_ERROR("MediaPlayerPrivateGStreamerBase::updateTexture: invalid GstEGLImage orientation");
                }
            }

            return;
        }
    }

    return;
#endif
#if GST_CHECK_VERSION(1, 1, 0)
    GstVideoGLTextureUploadMeta* meta;
    if ((meta = gst_buffer_get_video_gl_texture_upload_meta(buffer))) {
        if (meta->n_textures == 1) { // BRGx & BGRA formats use only one texture.
            guint ids[4] = { texture.id(), 0, 0, 0 };

            if (gst_video_gl_texture_upload_meta_upload(meta, ids))
                return;
        }
    }
#endif

    // Right now the TextureMapper only supports chromas with one plane
    ASSERT(GST_VIDEO_INFO_N_PLANES(&videoInfo) == 1);

    GstVideoFrame videoFrame;
    if (!gst_video_frame_map(&videoFrame, &videoInfo, buffer, GST_MAP_READ))
        return;

    int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&videoFrame, 0);
    const void* srcData = GST_VIDEO_FRAME_PLANE_DATA(&videoFrame, 0);
    texture.updateContents(srcData, WebCore::IntRect(WebCore::IntPoint(0, 0), size), WebCore::IntPoint(0, 0), stride, BitmapTexture::UpdateCannotModifyOriginalImageData);
    gst_video_frame_unmap(&videoFrame);
}
#endif

#if USE(COORDINATED_GRAPHICS_THREADED)
void MediaPlayerPrivateGStreamerBase::updateOnCompositorThread()
{
    WTF::GMutexLocker<GMutex> lock(m_updateMutex);

    {
        WTF::GMutexLocker<GMutex> lock(m_sampleMutex);
        GRefPtr<GstCaps> caps;
        if (!GST_IS_SAMPLE(m_sample.get())) {
#if ENABLE(MEDIA_SOURCE)
            caps = currentDemuxerCaps();
#else
            g_cond_signal(&m_updateCondition);
            return;
#endif
        }

        if (!caps)
            caps = gst_sample_get_caps(m_sample.get());

        if (UNLIKELY(!caps)) {
            g_cond_signal(&m_updateCondition);
            return;
        }

        GstVideoInfo videoInfo;
        gst_video_info_init(&videoInfo);
        if (UNLIKELY(!gst_video_info_from_caps(&videoInfo, caps.get()))) {
            g_cond_signal(&m_updateCondition);
            return;
        }

        if (!m_platformLayerProxy->hasTargetLayer()) {
            g_cond_signal(&m_updateCondition);
            return;
        }

        IntSize size = IntSize(GST_VIDEO_INFO_WIDTH(&videoInfo), GST_VIDEO_INFO_HEIGHT(&videoInfo));

        unique_ptr<TextureMapperPlatformLayerBuffer> buffer = m_platformLayerProxy->getAvailableBuffer(size);
        if (UNLIKELY(!buffer)) {
            if (UNLIKELY(!m_context3D))
                m_context3D = GraphicsContext3D::create(GraphicsContext3D::Attributes(), nullptr, GraphicsContext3D::RenderToCurrentGLContext);

            RefPtr<BitmapTexture> texture = adoptRef(new BitmapTextureGL(m_context3D));
            texture->reset(size, GST_VIDEO_INFO_HAS_ALPHA(&videoInfo));
            buffer = std::make_unique<TextureMapperPlatformLayerBuffer>(WTF::move(texture));
        }

        updateTexture(buffer->textureGL(), videoInfo);
        {
            MutexLocker locker(m_platformLayerProxy->mutex());
            m_platformLayerProxy->pushNextBuffer(locker, WTF::move(buffer));
            m_platformLayerProxy->requestUpdate(locker);
        }
    }

    g_cond_signal(&m_updateCondition);
}
#endif

void MediaPlayerPrivateGStreamerBase::triggerRepaint(GstSample* sample)
{
    {
        WebCore::GMutexLocker lock(&m_sampleMutex);
        m_sample = sample;
    }

#if USE(COORDINATED_GRAPHICS_THREADED)
    {
        WTF::GMutexLocker<GMutex> lock(m_updateMutex);
        if (!m_platformLayerProxy->scheduleUpdateOnCompositorThread([this] { this->updateOnCompositorThread(); }))
            return;
        g_cond_wait(&m_updateCondition, &m_updateMutex);
    }
    return;
#endif

#if USE(GSTREAMER_GL)
    {
        ASSERT(!isMainThread());

        WTF::GMutexLocker<GMutex> lock(m_drawMutex);

        m_drawTimerHandler.schedule("[WebKit] video render", [this] {
            // Rendering should be done from the main thread
            // because this is where the GL APIs were initialized.
            WTF::GMutexLocker<GMutex> lock(m_drawMutex);
#if USE(TEXTURE_MAPPER_GL) && !USE(COORDINATED_GRAPHICS)
            if (supportsAcceleratedRendering() && m_player->client().mediaPlayerRenderingCanBeAccelerated(m_player) && client())
                client()->setPlatformLayerNeedsDisplay();
            g_cond_signal(&m_drawCondition);
#endif
        });
        g_cond_wait(&m_drawCondition, &m_drawMutex);
    }

#elif USE(TEXTURE_MAPPER_GL) && !USE(COORDINATED_GRAPHICS)
    if (supportsAcceleratedRendering() && m_player->client().mediaPlayerRenderingCanBeAccelerated(m_player) && client()) {
        client()->setPlatformLayerNeedsDisplay();
        return;
    }
#endif

    m_player->repaint();
}

void MediaPlayerPrivateGStreamerBase::triggerDrain()
{
    WebCore::GMutexLocker lock(&m_sampleMutex);
    m_videoSize.setWidth(0);
    m_videoSize.setHeight(0);
    m_sample = nullptr;
}

void MediaPlayerPrivateGStreamerBase::setSize(const IntSize& size)
{
    m_size = size;
}

void MediaPlayerPrivateGStreamerBase::paint(GraphicsContext* context, const FloatRect& rect)
{
#if USE(COORDINATED_GRAPHICS_THREADED)
        return;
#elif USE(TEXTURE_MAPPER_GL) && !USE(COORDINATED_GRAPHICS)
    if (client())
        return;
#endif

    if (context->paintingDisabled())
        return;

    if (!m_player->visible())
        return;

    WebCore::GMutexLocker lock(&m_sampleMutex);
    if (!GST_IS_SAMPLE(m_sample.get()))
        return;

    RefPtr<ImageGStreamer> gstImage = ImageGStreamer::createImage(m_sample.get());
    if (!gstImage)
        return;

    context->drawImage(reinterpret_cast<Image*>(gstImage->image().get()), ColorSpaceSRGB,
        rect, gstImage->rect(), CompositeCopy);
}

#if USE(TEXTURE_MAPPER_GL) && !USE(COORDINATED_GRAPHICS)
void MediaPlayerPrivateGStreamerBase::paintToTextureMapper(TextureMapper* textureMapper, const FloatRect& targetRect, const TransformationMatrix& modelViewMatrix, float opacity)
{
    if (!m_player->visible())
        return;

    if (m_usingFallbackVideoSink) {
        if (RefPtr<BitmapTexture> texture = updateTexture(textureMapper))
            textureMapper->drawTexture(*texture.get(), targetRect, modelViewMatrix, opacity);
        return;
    }

#if USE(GSTREAMER_GL)
    if (!GST_IS_SAMPLE(m_sample.get()))
        return;

    GstCaps* caps = gst_sample_get_caps(m_sample.get());
    if (!caps)
        return;

    GstVideoInfo videoInfo;
    gst_video_info_init(&videoInfo);
    if (!gst_video_info_from_caps(&videoInfo, caps))
        return;

    GstBuffer* buffer = gst_sample_get_buffer(m_sample.get());
    GstVideoFrame videoFrame;
    if (!gst_video_frame_map(&videoFrame, &videoInfo, buffer, static_cast<GstMapFlags>(GST_MAP_READ | GST_MAP_GL)))
        return;

    unsigned textureID = *reinterpret_cast<unsigned*>(videoFrame.data[0]);
    BitmapTexture::Flags flags = BitmapTexture::NoFlag;
    if (GST_VIDEO_INFO_HAS_ALPHA(&videoInfo))
        flags |= BitmapTexture::SupportsAlpha;

    IntSize size = IntSize(GST_VIDEO_INFO_WIDTH(&videoInfo), GST_VIDEO_INFO_HEIGHT(&videoInfo));
    TextureMapperGL* textureMapperGL = reinterpret_cast<TextureMapperGL*>(textureMapper);
    textureMapperGL->drawTexture(textureID, flags, size, targetRect, matrix, opacity);
    gst_video_frame_unmap(&videoFrame);
#endif
}
#endif

bool MediaPlayerPrivateGStreamerBase::supportsFullscreen() const
{
    return true;
}

PlatformMedia MediaPlayerPrivateGStreamerBase::platformMedia() const
{
    return NoPlatformMedia;
}

MediaPlayer::MovieLoadType MediaPlayerPrivateGStreamerBase::movieLoadType() const
{
    if (m_readyState == MediaPlayer::HaveNothing)
        return MediaPlayer::Unknown;

    if (isLiveStream())
        return MediaPlayer::LiveStream;

    return MediaPlayer::Download;
}

GstElement* MediaPlayerPrivateGStreamerBase::createVideoSink()
{
    GstElement* videoSink = nullptr;
#if USE(GSTREAMER_GL)
    if (webkitGstCheckVersion(1, 5, 0)) {
        m_videoSink = gst_element_factory_make("glimagesink", nullptr);
        if (m_videoSink) {
            m_repaintHandler = g_signal_connect(m_videoSink.get(), "client-draw", G_CALLBACK(mediaPlayerPrivateDrawCallback), this);
            videoSink = m_videoSink.get();
        }
    }
#endif

    if (!m_videoSink) {
        m_usingFallbackVideoSink = true;
        m_videoSink = webkitVideoSinkNew();
        m_repaintHandler = g_signal_connect(m_videoSink.get(), "repaint-requested", G_CALLBACK(mediaPlayerPrivateRepaintCallback), this);
        m_drainHandler = g_signal_connect(m_videoSink.get(), "drain", G_CALLBACK(mediaPlayerPrivateDrainCallback), this);
    }

    m_fpsSink = gst_element_factory_make("fpsdisplaysink", "sink");
    if (m_fpsSink) {
        g_object_set(m_fpsSink.get(), "silent", TRUE , nullptr);

        // Turn off text overlay unless logging is enabled.
#if LOG_DISABLED
        g_object_set(m_fpsSink.get(), "text-overlay", FALSE , nullptr);
#else
        //if (!isLogChannelEnabled("Media"))
        //    g_object_set(m_fpsSink.get(), "text-overlay", FALSE , nullptr);
#endif // LOG_DISABLED

        if (g_object_class_find_property(G_OBJECT_GET_CLASS(m_fpsSink.get()), "video-sink")) {
            g_object_set(m_fpsSink.get(), "video-sink", m_videoSink.get(), nullptr);
            videoSink = m_fpsSink.get();
        } else
            m_fpsSink = nullptr;
    }

    if (!m_fpsSink)
        videoSink = m_videoSink.get();

    ASSERT(videoSink);

    return videoSink;
}

void MediaPlayerPrivateGStreamerBase::setStreamVolumeElement(GstStreamVolume* volume)
{
    ASSERT(!m_volumeElement);
    m_volumeElement = volume;

    // We don't set the initial volume because we trust the sink to keep it for us. See
    // https://bugs.webkit.org/show_bug.cgi?id=118974 for more information.
    if (!m_player->platformVolumeConfigurationRequired()) {
        LOG_MEDIA_MESSAGE("Setting stream volume to %f", m_player->volume());
        g_object_set(m_volumeElement.get(), "volume", m_player->volume(), NULL);
    } else
        LOG_MEDIA_MESSAGE("Not setting stream volume, trusting system one");

    LOG_MEDIA_MESSAGE("Setting stream muted %d",  m_player->muted());
    g_object_set(m_volumeElement.get(), "mute", m_player->muted(), NULL);

    m_volumeSignalHandler = g_signal_connect(m_volumeElement.get(), "notify::volume", G_CALLBACK(mediaPlayerPrivateVolumeChangedCallback), this);
    m_muteSignalHandler = g_signal_connect(m_volumeElement.get(), "notify::mute", G_CALLBACK(mediaPlayerPrivateMuteChangedCallback), this);
}

unsigned MediaPlayerPrivateGStreamerBase::decodedFrameCount() const
{
    guint64 decodedFrames = 0;
    if (m_fpsSink)
        g_object_get(m_fpsSink.get(), "frames-rendered", &decodedFrames, NULL);
    return static_cast<unsigned>(decodedFrames);
}

unsigned MediaPlayerPrivateGStreamerBase::droppedFrameCount() const
{
    guint64 framesDropped = 0;
    if (m_fpsSink)
        g_object_get(m_fpsSink.get(), "frames-dropped", &framesDropped, NULL);
    return static_cast<unsigned>(framesDropped);
}

unsigned MediaPlayerPrivateGStreamerBase::audioDecodedByteCount() const
{
    GstQuery* query = gst_query_new_position(GST_FORMAT_BYTES);
    gint64 position = 0;

    if (audioSink() && gst_element_query(audioSink(), query))
        gst_query_parse_position(query, 0, &position);

    gst_query_unref(query);
    return static_cast<unsigned>(position);
}

unsigned MediaPlayerPrivateGStreamerBase::videoDecodedByteCount() const
{
    GstQuery* query = gst_query_new_position(GST_FORMAT_BYTES);
    gint64 position = 0;

    if (gst_element_query(m_videoSink.get(), query))
        gst_query_parse_position(query, 0, &position);

    gst_query_unref(query);
    return static_cast<unsigned>(position);
}

}

#endif // USE(GSTREAMER)
