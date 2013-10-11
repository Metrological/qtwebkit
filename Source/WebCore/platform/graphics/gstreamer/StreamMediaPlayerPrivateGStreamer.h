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

#ifndef StreamMediaPlayerPrivateGStreamer_h
#define StreamMediaPlayerPrivateGStreamer_h

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#include "MediaPlayerPrivateGStreamerBase.h"
#include "MediaStream.h"
#include <gst/gst.h>

namespace WebCore {

class URL;
class MediaStreamPrivate;
class MediaStreamSourceGStreamer;
class HTMLMediaSource;

class StreamMediaPlayerPrivateGStreamer : public MediaPlayerPrivateGStreamerBase, private MediaStreamSource::Observer {
    friend void mediaPlayerPrivateRepaintCallback(WebKitVideoSink*, GstBuffer*, StreamMediaPlayerPrivateGStreamer*);
public:
    ~StreamMediaPlayerPrivateGStreamer();
    static void registerMediaEngine(MediaEngineRegistrar);

    virtual void load(const String&);
#if ENABLE(MEDIA_SOURCE)
    virtual void load(const String&, MediaSourcePrivateClient*) { }
#endif
    virtual void cancelLoad() { }

    virtual void prepareToPlay() { }
    void play();
    void pause();

    bool hasVideo() const;
    bool hasAudio() const;

    virtual float duration() const { return 0; }

    virtual float currentTime() const;
    virtual void seek(float) { }
    virtual bool seeking() const { return false; }

    virtual void setRate(float) { }
    virtual void setPreservesPitch(bool) { }
    virtual bool paused() const { return m_paused; }

    virtual bool hasClosedCaptions() const { return false; }
    virtual void setClosedCaptionsVisible(bool) { };

    virtual float maxTimeSeekable() const { return 0; }
    virtual std::unique_ptr<PlatformTimeRanges> buffered() const { return PlatformTimeRanges::create(); }
    bool didLoadingProgress() const;

    virtual unsigned totalBytes() const { return 0; }
    virtual unsigned bytesLoaded() const { return 0; }

    virtual bool canLoadPoster() const { return false; }
    virtual void setPoster(const String&) { }

    virtual bool isLiveStream() const { return true; }

    gboolean handleMessage(GstMessage*);

    // MediaStreamSource::Observer implementation
    virtual void sourceReadyStateChanged() override final;
    virtual void sourceMutedChanged() override final;
    virtual void sourceEnabledChanged() override final;
    virtual bool observerIsEnabled() override final;

protected:
    virtual GstElement* createVideoSink();
    virtual GstElement* videoSink() const { return m_videoSinkBin.get(); }

private:
    StreamMediaPlayerPrivateGStreamer(MediaPlayer*);

    static PassOwnPtr<MediaPlayerPrivateInterface> create(MediaPlayer*);

    static void getSupportedTypes(HashSet<String>&);
    static MediaPlayer::SupportsType supportsType(const MediaEngineSupportParameters&);
    static bool isAvailable();
    void createGSTAudioSinkBin();
    bool connectToGSTLiveStream(MediaStream*);
    void loadingFailed(MediaPlayer::NetworkState error);
    bool internalLoad();
    void stop();
    virtual GstElement* audioSink() const { return m_audioSinkBin.get(); }

private:
    bool m_paused;
    bool m_stopped;
    RefPtr<MediaStreamSourceGStreamer> m_videoSource;
    RefPtr<MediaStreamSourceGStreamer> m_audioSource;
    GRefPtr<GstElement> m_audioSinkBin;
    GRefPtr<GstElement> m_videoSinkBin;
    RefPtr<MediaStream> m_stream;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#endif // StreamMediaPlayerPrivateGStreamer_h
