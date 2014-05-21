/*
 *  Copyright (C) 2012 Collabora Ltd. All rights reserved.
 *  Copyright (C) 2013 Igalia S.L. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */


#include "config.h"

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#include "StreamMediaPlayerPrivateGStreamer.h"

#include "CentralPipelineUnit.h"
#include "GStreamerUtilities.h"
#include "GstMediaStream.h"
#include "MediaPlayer.h"
#include "MediaStreamPrivate.h"
#include "MediaStreamRegistry.h"
#include "MediaStreamSourceGStreamer.h"
#include "KURL.h"
#include <gst/audio/streamvolume.h>
#include <wtf/text/CString.h>

GST_DEBUG_CATEGORY_EXTERN(webkit_media_stream_debug);
#define GST_CAT_DEFAULT webkit_media_stream_debug

namespace WebCore {

static gboolean streamMediaPlayerPrivateMessageCallback(GstBus*, GstMessage* message, StreamMediaPlayerPrivateGStreamer* player)
{
    return player->handleMessage(message);
}

StreamMediaPlayerPrivateGStreamer::StreamMediaPlayerPrivateGStreamer(MediaPlayer* player)
    : MediaPlayerPrivateGStreamerBase(player)
    , m_paused(true)
    , m_stopped(true)
{
    if (initializeGStreamer()) {
        LOG_MEDIA_MESSAGE("Creating Stream media player");

        createVideoSink();
        createGSTAudioSinkBin();
    }
}

StreamMediaPlayerPrivateGStreamer::~StreamMediaPlayerPrivateGStreamer()
{
    LOG_MEDIA_MESSAGE("Destructing");

    stop();

    CentralPipelineUnit* cpu = m_stream->privateStream()->centralPipelineUnit();
    GRefPtr<GstBus> bus = adoptGRef(gst_pipeline_get_bus(GST_PIPELINE(cpu->pipeline())));
    g_signal_handlers_disconnect_by_func(bus.get(), reinterpret_cast<gpointer>(streamMediaPlayerPrivateMessageCallback), this);
    gst_bus_remove_signal_watch(bus.get());
}

void StreamMediaPlayerPrivateGStreamer::play()
{
    LOG_MEDIA_MESSAGE("Play");

    if (!m_stream || !m_stream->active()) {
        m_readyState = MediaPlayer::HaveNothing;
        loadingFailed(MediaPlayer::Empty);
        return;
    }

    m_paused = false;
    internalLoad();
}

void StreamMediaPlayerPrivateGStreamer::pause()
{
    LOG_MEDIA_MESSAGE("Pause");
    m_paused = true;
    stop();
}

bool StreamMediaPlayerPrivateGStreamer::hasVideo() const
{
    return m_videoSource;
}

bool StreamMediaPlayerPrivateGStreamer::hasAudio() const
{
    return m_audioSource;
}

float StreamMediaPlayerPrivateGStreamer::currentTime() const
{
    return m_stream->privateStream()->centralPipelineUnit()->currentTime();
}

void StreamMediaPlayerPrivateGStreamer::load(const String &url)
{
    if (!initializeGStreamer())
        return;

    LOG_MEDIA_MESSAGE("Loading %s", url.utf8().data());

    m_stream = static_cast<MediaStream*>(MediaStreamRegistry::registry().lookup(url));
    if (!m_stream || !m_stream->active()) {
        loadingFailed(MediaPlayer::NetworkError);
        return;
    }

    m_readyState = MediaPlayer::HaveNothing;
    m_networkState = MediaPlayer::Loading;
    m_player->networkStateChanged();
    m_player->readyStateChanged();

    if (!internalLoad())
        return;

    // If the stream contains video, wait for first video frame before setting
    // HaveEnoughData
    if (!hasVideo())
        m_readyState = MediaPlayer::HaveEnoughData;

    m_player->readyStateChanged();
}

void StreamMediaPlayerPrivateGStreamer::loadingFailed(MediaPlayer::NetworkState error)
{

    if (m_networkState != error) {
        m_networkState = error;
        m_player->networkStateChanged();
    }
    if (m_readyState != MediaPlayer::HaveNothing) {
        m_readyState = MediaPlayer::HaveNothing;
        m_player->readyStateChanged();
    }
}

bool StreamMediaPlayerPrivateGStreamer::didLoadingProgress() const
{
    return true;
}

bool StreamMediaPlayerPrivateGStreamer::internalLoad()
{
    if (m_stopped) {
        m_stopped = false;
        if (!m_stream || !m_stream->active()) {
            loadingFailed(MediaPlayer::NetworkError);
            return false;
        }
        return connectToGSTLiveStream(m_stream.get());
    }
    return false;
}

void StreamMediaPlayerPrivateGStreamer::stop()
{
    if (!m_stopped) {
        m_stopped = true;
        CentralPipelineUnit* cpu = m_stream->privateStream()->centralPipelineUnit();
        if (m_audioSource) {
            LOG_MEDIA_MESSAGE("Stop: disconnecting audio");
            cpu->disconnectFromSource(m_audioSource, m_audioSinkBin.get());
        }
        if (m_videoSource) {
            LOG_MEDIA_MESSAGE("Stop: disconnecting video");
            cpu->disconnectFromSource(m_videoSource, m_videoSinkBin.get());
        }
        m_audioSource = 0;
        m_videoSource = 0;
    }
}


PassOwnPtr<MediaPlayerPrivateInterface> StreamMediaPlayerPrivateGStreamer::create(MediaPlayer* player)
{
    return adoptPtr(new StreamMediaPlayerPrivateGStreamer(player));
}

void StreamMediaPlayerPrivateGStreamer::registerMediaEngine(MediaEngineRegistrar registrar)
{
    if (isAvailable())
        registrar(create, getSupportedTypes, supportsType, 0, 0, 0);
}

void StreamMediaPlayerPrivateGStreamer::getSupportedTypes(HashSet<String>& types)
{
    // FIXME
}

MediaPlayer::SupportsType StreamMediaPlayerPrivateGStreamer::supportsType(const String& type, const String& codecs, const KURL&)
{
    // FIXME
    return MediaPlayer::IsNotSupported;
}

bool StreamMediaPlayerPrivateGStreamer::isAvailable()
{
    return initializeGStreamer();
}

void StreamMediaPlayerPrivateGStreamer::createGSTAudioSinkBin()
{
    ASSERT(!m_audioSinkBin);
    LOG_MEDIA_MESSAGE("Creating audio sink");
    m_audioSinkBin = gst_bin_new(0);

    GRefPtr<GstElement> volume = gst_element_factory_make("volume", "volume");
    setStreamVolumeElement(GST_STREAM_VOLUME(volume.get()));

    GstElement* audioSink = gst_element_factory_make("fakesink", 0);
    gst_bin_add_many(GST_BIN(m_audioSinkBin.get()), volume.get(), audioSink, NULL);
    gst_element_link_many(volume.get(), audioSink, NULL);

    GRefPtr<GstPad> pad = adoptGRef(gst_element_get_static_pad(volume.get(), "sink"));
    gst_element_add_pad(m_audioSinkBin.get(), gst_ghost_pad_new("sink", pad.get()));
}

void StreamMediaPlayerPrivateGStreamer::sourceReadyStateChanged()
{
    LOG_MEDIA_MESSAGE("Source state changed");

    if (!m_stream || !m_stream->active())
        stop();

    CentralPipelineUnit* cpu = m_stream->privateStream()->centralPipelineUnit();

    // check if the source should be ended
    if (m_audioSource) {
        Vector<RefPtr<MediaStreamTrack> > audioTracks = m_stream->getAudioTracks();
        RefPtr<MediaStreamTrack> audioTrack;
        for (size_t i = 0; i < audioTracks.size(); ++i) {
            audioTrack = audioTracks[i];
            MediaStreamSourceGStreamer* source = reinterpret_cast<MediaStreamSourceGStreamer*>(audioTrack->source());
            if (!audioTrack->enabled() && source == m_audioSource) {
                cpu->disconnectFromSource(m_audioSource, m_audioSinkBin.get());
                m_audioSource = 0;
                break;
            }
        }
    }

    // Same check for video
    if (m_videoSource) {
        Vector<RefPtr<MediaStreamTrack> > videoTracks = m_stream->getVideoTracks();
        RefPtr<MediaStreamTrack> videoTrack;
        for (size_t i = 0; i < videoTracks.size(); ++i) {
            videoTrack = videoTracks[i];
            MediaStreamSourceGStreamer* source = reinterpret_cast<MediaStreamSourceGStreamer*>(videoTrack->source());
            if (!videoTrack->enabled() && source == m_videoSource) {
                cpu->disconnectFromSource(m_videoSource, m_videoSinkBin.get());
                m_videoSource = 0;
                break;
            }
        }
    }
}

void StreamMediaPlayerPrivateGStreamer::sourceMutedChanged()
{
    LOG_MEDIA_MESSAGE("Source muted state changed");
}

void StreamMediaPlayerPrivateGStreamer::sourceEnabledChanged()
{
    LOG_MEDIA_MESSAGE("Source enabled state changed");
}

bool StreamMediaPlayerPrivateGStreamer::observerIsEnabled()
{
    return true;
}

bool StreamMediaPlayerPrivateGStreamer::connectToGSTLiveStream(MediaStream* stream)
{
    LOG_MEDIA_MESSAGE("Connecting to live stream, descriptor: %p", stream);
    if (!stream)
        return false;

    CentralPipelineUnit* cpu = stream->privateStream()->centralPipelineUnit();
    GRefPtr<GstBus> bus = adoptGRef(gst_pipeline_get_bus(GST_PIPELINE(cpu->pipeline())));
    gst_bus_add_signal_watch(bus.get());
    g_signal_connect(bus.get(), "message", G_CALLBACK(streamMediaPlayerPrivateMessageCallback), this);

    // FIXME: Error handling.. this could fail.. and this method never returns false.


    if (m_audioSource) {
        cpu->disconnectFromSource(m_audioSource, m_audioSinkBin.get());
        m_audioSource = 0;
    }

    if (m_videoSource) {
        cpu->disconnectFromSource(m_videoSource, m_videoSinkBin.get());
        m_videoSource = 0;
    }

    Vector<RefPtr<MediaStreamTrack> > audioTracks = stream->getAudioTracks();
    Vector<RefPtr<MediaStreamTrack> > videoTracks = stream->getVideoTracks();
    RefPtr<MediaStreamTrack> audioTrack;
    RefPtr<MediaStreamTrack> videoTrack;
    LOG_MEDIA_MESSAGE("Stream descriptor has %zd audio streams and %zd video streams", audioTracks.size(), videoTracks.size());
    for (unsigned i = 0; i < audioTracks.size(); ++i) {
        audioTrack = audioTracks[i];
        if (!audioTrack->enabled()) {
            LOG_MEDIA_MESSAGE("Track %s disabled", audioTrack->label().ascii().data());
            continue;
        }

        MediaStreamSourceGStreamer* source = reinterpret_cast<MediaStreamSourceGStreamer*>(audioTrack->source());
        if (source->type() == MediaStreamSource::Audio) {
            LOG_MEDIA_MESSAGE("Audio track %s type is %s", source->id().utf8().data(), source->streamType() == 0 ? "local":"remote");
            if (cpu->connectToSource(source, "", m_audioSinkBin.get())) {
                m_audioSource = source;
                source->addObserver(this);
                break;
            }
        }
    }

    for (unsigned i = 0; i < videoTracks.size(); ++i) {
        videoTrack = videoTracks[i];
        if (!videoTrack->enabled()) {
            LOG_MEDIA_MESSAGE("Track %s disabled", videoTrack->label().ascii().data());
            continue;
        }

        MediaStreamSourceGStreamer* source = reinterpret_cast<MediaStreamSourceGStreamer*>(videoTrack->source());
        if (source->type() == MediaStreamSource::Video) {
            LOG_MEDIA_MESSAGE("Video track %s type is %s", source->id().utf8().data(), source->streamType() == 0 ? "local":"remote");
            if (cpu->connectToSource(source, "", m_videoSinkBin.get())) {
                m_videoSource = source;
                source->addObserver(this);
                break;
            }
        }
    }

    return true;
}

gboolean StreamMediaPlayerPrivateGStreamer::handleMessage(GstMessage* message)
{
    if (message->src == GST_OBJECT(m_videoSinkBin.get())) {
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_STATE_CHANGED) {
            GstState oldState, newState;
            gst_message_parse_state_changed(message, &oldState, &newState, 0);
            if (newState == GST_STATE_PLAYING) {
                m_readyState = MediaPlayer::HaveEnoughData;
                m_player->readyStateChanged();
            }
        }
    }
    return TRUE;
}

GstElement* StreamMediaPlayerPrivateGStreamer::createVideoSink()
{
    GstElement* sink = MediaPlayerPrivateGStreamerBase::createVideoSink();
    m_videoSinkBin = gst_bin_new(0);
    GstElement* videoconvert = gst_element_factory_make("videoconvert", "streamplayervideoconvert");
    gst_bin_add_many(GST_BIN(m_videoSinkBin.get()), videoconvert, sink, NULL);
    gst_element_link(videoconvert, sink);
    GRefPtr<GstPad> pad = adoptGRef(gst_element_get_static_pad(videoconvert, "sink"));
    gst_element_add_pad(m_videoSinkBin.get(), gst_ghost_pad_new("sink", pad.get()));

    return m_videoSinkBin.get();
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
