/*
 * Copyright (C) 2007, 2009 Apple Inc.  All rights reserved.
 * Copyright (C) 2007 Collabora Ltd.  All rights reserved.
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 * Copyright (C) 2009 Gustavo Noronha Silva <gns@gnome.org>
 * Copyright (C) 2009, 2010, 2011, 2012, 2013 Igalia S.L
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
#include "MediaPlayerPrivateGStreamer.h"

#if ENABLE(VIDEO) && USE(GSTREAMER)

#include "GStreamerUtilities.h"
#include "GStreamerVersioning.h"
#include "KURL.h"
#include "Logging.h"
#include "MIMETypeRegistry.h"
#include "MediaPlayer.h"
#include "NotImplemented.h"
#include "SecurityOrigin.h"
#include "TimeRanges.h"
#include "WebKitWebSourceGStreamer.h"
#include <gst/gst.h>
#include <gst/pbutils/missing-plugins.h>
#include <limits>
#include <wtf/gobject/GOwnPtr.h>
#include <wtf/text/CString.h>
#include "CDMSessionGStreamer.h"

#ifdef GST_API_VERSION_1
#include <gst/audio/streamvolume.h>
#else
#include <gst/interfaces/streamvolume.h>
#endif

#if ENABLE(VIDEO_TRACK)
#include "TextCombinerGStreamer.h"
#include "TextSinkGStreamer.h"
#endif

#if ENABLE(VIDEO_TRACK) && USE(GSTREAMER_MPEGTS)
#define GST_USE_UNSTABLE_API
#include <gst/mpegts/mpegts.h>
#undef GST_USE_UNSTABLE_API
#endif

// gPercentMax is used when parsing buffering ranges with
// gst_query_parse_nth_buffering_range as there was a bug in GStreamer
// 0.10 that was using 100 instead of GST_FORMAT_PERCENT_MAX. This was
// corrected in 1.0. gst_query_parse_buffering_range worked as
// expected with GST_FORMAT_PERCENT_MAX in both cases.
#ifdef GST_API_VERSION_1
static const char* gPlaybinName = "playbin";
static const gint64 gPercentMax = GST_FORMAT_PERCENT_MAX;
#else
static const char* gPlaybinName = "playbin2";
static const gint64 gPercentMax = 100;
#endif

#if ENABLE(MEDIA_SOURCE)
#include "MediaSourceGStreamer.h"
#include "WebKitMediaSourceGStreamer.h"
#endif

GST_DEBUG_CATEGORY_EXTERN(webkit_media_player_debug);
#define GST_CAT_DEFAULT webkit_media_player_debug

using namespace std;

namespace WebCore {

static void mediaPlayerPrivateSyncMessageCallback(GstBus*, GstMessage* message, MediaPlayerPrivateGStreamer* player)
{
    player->handleSyncMessage(message);
}

static void mediaPlayerPrivateMessageCallback(GstBus*, GstMessage* message, MediaPlayerPrivateGStreamer* player)
{
    player->handleMessage(message);
}

static void mediaPlayerPrivateSourceChangedCallback(GObject*, GParamSpec*, MediaPlayerPrivateGStreamer* player)
{
    player->sourceChanged();
}

static void mediaPlayerPrivateVideoSinkCapsChangedCallback(GObject*, GParamSpec*, MediaPlayerPrivateGStreamer* player)
{
    player->videoChanged();
}

static void mediaPlayerPrivateVideoChangedCallback(GObject*, MediaPlayerPrivateGStreamer* player)
{
    player->videoChanged();
}

static void mediaPlayerPrivateAudioChangedCallback(GObject*, MediaPlayerPrivateGStreamer* player)
{
    player->audioChanged();
}

static gboolean mediaPlayerPrivateAudioChangeTimeoutCallback(MediaPlayerPrivateGStreamer* player)
{
    // This is the callback of the timeout source created in ::audioChanged.
    player->notifyPlayerOfAudio();
    return FALSE;
}

#ifdef GST_API_VERSION_1
static void setAudioStreamPropertiesCallback(GstChildProxy*, GObject* object, gchar*,
    MediaPlayerPrivateGStreamer* player)
#else
static void setAudioStreamPropertiesCallback(GstChildProxy*, GObject* object, MediaPlayerPrivateGStreamer* player)
#endif
{
    player->setAudioStreamProperties(object);
}

#if ENABLE(VIDEO_TRACK)
static void mediaPlayerPrivateTextChangedCallback(GObject*, MediaPlayerPrivateGStreamer* player)
{
    player->textChanged();
}

static GstFlowReturn mediaPlayerPrivateNewTextSampleCallback(GObject*, MediaPlayerPrivateGStreamer* player)
{
    player->newTextSample();
    return GST_FLOW_OK;
}
#endif

static gboolean mediaPlayerPrivateVideoChangeTimeoutCallback(MediaPlayerPrivateGStreamer* player)
{
    // This is the callback of the timeout source created in ::videoChanged.
    if (player)
        player->notifyPlayerOfVideo();
    return FALSE;
}

static void mediaPlayerPrivatePluginInstallerResultFunction(GstInstallPluginsReturn result, gpointer userData)
{
    MediaPlayerPrivateGStreamer* player = reinterpret_cast<MediaPlayerPrivateGStreamer*>(userData);
    player->handlePluginInstallerResult(result);
}

static GstClockTime toGstClockTime(float time)
{
    // Extract the integer part of the time (seconds) and the fractional part (microseconds). Attempt to
    // round the microseconds so no floating point precision is lost and we can perform an accurate seek.
    float seconds;
    float microSeconds = modf(time, &seconds) * 1000000;
    GTimeVal timeValue;
    timeValue.tv_sec = static_cast<glong>(seconds);
    timeValue.tv_usec = static_cast<glong>(roundf(microSeconds / 10000) * 10000);
    return GST_TIMEVAL_TO_TIME(timeValue);
}

void MediaPlayerPrivateGStreamer::setAudioStreamProperties(GObject* object)
{
    if (g_strcmp0(G_OBJECT_TYPE_NAME(object), "GstPulseSink"))
        return;

    const char* role = m_player->mediaPlayerClient() && m_player->mediaPlayerClient()->mediaPlayerIsVideo()
        ? "video" : "music";
    GstStructure* structure = gst_structure_new("stream-properties", "media.role", G_TYPE_STRING, role, NULL);
    g_object_set(object, "stream-properties", structure, NULL);
    gst_structure_free(structure);
    GOwnPtr<gchar> elementName(gst_element_get_name(GST_ELEMENT(object)));
    LOG_MEDIA_MESSAGE("Set media.role as %s at %s", role, elementName.get());
}

PassRefPtr<MediaPlayerPrivateInterface> MediaPlayerPrivateGStreamer::create(MediaPlayer* player)
{
    return adoptRef(new MediaPlayerPrivateGStreamer(player));
}

void MediaPlayerPrivateGStreamer::registerMediaEngine(MediaEngineRegistrar registrar)
{
    if (isAvailable())
#if ENABLE(ENCRYPTED_MEDIA) || ENABLE(ENCRYPTED_MEDIA_V2)
        registrar(create, getSupportedTypes, extendedSupportsType, 0, 0, 0, supportsKeySystem);
#else
        registrar(create, getSupportedTypes, supportsType, 0, 0, 0, supportsKeySystem);
#endif
}

bool initializeGStreamerAndRegisterWebKitElements()
{
    if (!initializeGStreamer())
        return false;

    GRefPtr<GstElementFactory> srcFactory = gst_element_factory_find("webkitwebsrc");
    if (!srcFactory) {
        GST_DEBUG_CATEGORY_INIT(webkit_media_player_debug, "webkitmediaplayer", 0, "WebKit media player");
        gst_element_register(0, "webkitwebsrc", GST_RANK_PRIMARY + 100, WEBKIT_TYPE_WEB_SRC);
    }

#if ENABLE(MEDIA_SOURCE)
    GRefPtr<GstElementFactory> WebKitMediaSrcFactory = gst_element_factory_find("webkitmediasrc");
    if (!WebKitMediaSrcFactory)
        gst_element_register(0, "webkitmediasrc", GST_RANK_PRIMARY + 100, WEBKIT_TYPE_MEDIA_SRC);
#endif
    return true;
}

bool MediaPlayerPrivateGStreamer::isAvailable()
{
    if (!initializeGStreamerAndRegisterWebKitElements())
        return false;

    GRefPtr<GstElementFactory> factory = gst_element_factory_find(gPlaybinName);
    return factory;
}

MediaPlayerPrivateGStreamer::MediaPlayerPrivateGStreamer(MediaPlayer* player)
    : MediaPlayerPrivateGStreamerBase(player)
    , m_source(0)
    , m_seekTime(0)
    , m_changingRate(false)
    , m_endTime(numeric_limits<float>::infinity())
    , m_isStreaming(false)
    , m_mediaLocations(0)
    , m_mediaLocationCurrentIndex(0)
    , m_resetPipeline(false)
    , m_paused(true)
    , m_playbackRatePause(false)
    , m_seeking(false)
    , m_seekIsPending(false)
    , m_timeOfOverlappingSeek(-1)
    , m_canFallBackToLastFinishedSeekPosition(false)
    , m_buffering(false)
    , m_playbackRate(1)
    , m_lastPlaybackRate(1)
    , m_errorOccured(false)
    , m_mediaDuration(0)
    , m_downloadFinished(false)
    , m_fillTimer(this, &MediaPlayerPrivateGStreamer::fillTimerFired)
    , m_maxTimeLoaded(0)
    , m_bufferingPercentage(0)
    , m_preload(player->preload())
    , m_delayingLoad(false)
    , m_mediaDurationKnown(true)
    , m_maxTimeLoadedAtLastDidLoadingProgress(0)
    , m_volumeAndMuteInitialized(false)
    , m_hasVideo(false)
    , m_hasAudio(false)
    , m_audioTimerHandler(0)
    , m_videoTimerHandler(0)
    , m_webkitAudioSink(0)
    , m_totalBytes(0)
    , m_preservesPitch(false)
    , m_requestedState(GST_STATE_VOID_PENDING)
    , m_missingPlugins(false)
    , m_pendingAsyncOperations(0)
{
}

MediaPlayerPrivateGStreamer::~MediaPlayerPrivateGStreamer()
{
#if ENABLE(ENCRYPTED_MEDIA) || ENABLE(ENCRYPTED_MEDIA_V2)
    // Potentially unblock GStreamer thread for DRM license acquisition.
    m_drmKeySemaphore.signal ();
#endif

#if ENABLE(VIDEO_TRACK)
    for (size_t i = 0; i < m_audioTracks.size(); ++i)
        m_audioTracks[i]->disconnect();

    for (size_t i = 0; i < m_textTracks.size(); ++i)
        m_textTracks[i]->disconnect();

    for (size_t i = 0; i < m_videoTracks.size(); ++i)
        m_videoTracks[i]->disconnect();
#endif

    if (m_fillTimer.isActive())
        m_fillTimer.stop();

    if (m_mediaLocations) {
        gst_structure_free(m_mediaLocations);
        m_mediaLocations = 0;
    }

    if (m_autoAudioSink)
        g_signal_handlers_disconnect_by_func(G_OBJECT(m_autoAudioSink.get()),
            reinterpret_cast<gpointer>(setAudioStreamPropertiesCallback), this);

    if (m_source && WEBKIT_IS_MEDIA_SRC(m_source.get())) {
        webkit_media_src_set_mediaplayerprivate(WEBKIT_MEDIA_SRC(m_source.get()), 0);
        g_signal_handlers_disconnect_by_func(m_source.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateVideoChangedCallback), this);
        g_signal_handlers_disconnect_by_func(m_source.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateAudioChangedCallback), this);
        g_signal_handlers_disconnect_by_func(m_source.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateTextChangedCallback), this);
    }

    if (m_playBin) {
        GRefPtr<GstBus> bus = webkitGstPipelineGetBus(GST_PIPELINE(m_playBin.get()));
        ASSERT(bus);
        g_signal_handlers_disconnect_by_func(bus.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateMessageCallback), this);
        g_signal_handlers_disconnect_by_func(bus.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateSyncMessageCallback), this);
        gst_bus_remove_signal_watch(bus.get());

        g_signal_handlers_disconnect_by_func(m_playBin.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateSourceChangedCallback), this);
        g_signal_handlers_disconnect_by_func(m_playBin.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateVideoChangedCallback), this);
        g_signal_handlers_disconnect_by_func(m_playBin.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateAudioChangedCallback), this);
#if ENABLE(VIDEO_TRACK)
        g_signal_handlers_disconnect_by_func(m_playBin.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateNewTextSampleCallback), this);
        g_signal_handlers_disconnect_by_func(m_playBin.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateTextChangedCallback), this);
#endif

        // printf("### %s: Getting current state...\n", __PRETTY_FUNCTION__); fflush(stdout);
        // GstState state, pending;
        // gst_element_get_state(m_playBin.get(), &state, &pending, GST_CLOCK_TIME_NONE);
        // printf("### %s: state=%s, pending=%s\n", __PRETTY_FUNCTION__, gst_element_state_get_name(state), gst_element_state_get_name(pending)); fflush(stdout);

        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_playBin.get()), GST_DEBUG_GRAPH_SHOW_ALL, "destructor");

        printf("### %s: Changing state to NULL...\n", __PRETTY_FUNCTION__); fflush(stdout);

        /*
        {
            GstIterator* iter = gst_bin_iterate_recurse(GST_BIN(m_pipeline));
            GValue v = G_VALUE_INIT;
            while (gst_iterator_next(iter, &v) == GST_ITERATOR_OK) {
                GstElement* element = GST_ELEMENT(g_value_get_object(&v));
                const gchar* elementName = G_OBJECT_CLASS_NAME(G_OBJECT_GET_CLASS(G_OBJECT(element)));
                if (g_str_equal(elementName, "GstOMXH264Dec-omxh264dec")) {
                    GstPad* sinkPad = gst_element_get_static_pad(element, "sink");
                    GstPad* srcPad = NULL;
                    if (sinkPad) {
                        srcPad = gst_pad_get_peer(sinkPad);
                        g_object_unref(sinkPad);
                        sinkPad = NULL;
                    }
                    if (srcPad) {
                        GstEvent* eos = gst_event_new_eos();
                        printf("### %s: Pushing EOS to video decoder sink\n", __PRETTY_FUNCTION__); fflush(stdout);
                        gst_pad_push_event(srcPad, eos);
                        printf("### %s: Pushed EOS to video decoder sink\n", __PRETTY_FUNCTION__); fflush(stdout);
                        g_object_unref(srcPad);
                    }
                }
                g_value_reset(&v);
            }
            g_value_unset(&v);
            gst_iterator_free(iter);
        }
        */

        gst_element_set_state(m_playBin.get(), GST_STATE_NULL);
        printf("### %s: State change to NULL completed\n", __PRETTY_FUNCTION__); fflush(stdout);
        m_playBin.clear();
    }

    if (m_webkitVideoSink) {
        GRefPtr<GstPad> videoSinkPad = adoptGRef(gst_element_get_static_pad(m_webkitVideoSink.get(), "sink"));
        g_signal_handlers_disconnect_by_func(videoSinkPad.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateVideoSinkCapsChangedCallback), this);
    }

    if (m_videoTimerHandler)
        g_source_remove(m_videoTimerHandler);
    m_videoTimerHandler = 0;

    if (m_audioTimerHandler)
        g_source_remove(m_audioTimerHandler);
    m_audioTimerHandler = 0;

    // Cancel pending mediaPlayerPrivateNotifyDurationChanged() delayed calls
    m_pendingAsyncOperationsLock.lock();
    while (m_pendingAsyncOperations) {
        g_source_remove(GPOINTER_TO_UINT(m_pendingAsyncOperations->data));
        m_pendingAsyncOperations = g_list_remove(m_pendingAsyncOperations, m_pendingAsyncOperations->data);
    }
    m_pendingAsyncOperationsLock.unlock();
}

void MediaPlayerPrivateGStreamer::load(const String& urlString)
{
    if (!initializeGStreamerAndRegisterWebKitElements())
        return;

    KURL kurl(KURL(), urlString);
    if (kurl.isBlankURL())
        return;

    // Clean out everything after file:// url path.
    String cleanURL(urlString);
    if (kurl.isLocalFile())
        cleanURL = cleanURL.substring(0, kurl.pathEnd());

    if (!m_playBin)
        createGSTPlayBin();

    ASSERT(m_playBin);
    
#if ENABLE(ENCRYPTED_MEDIA) || ENABLE(ENCRYPTED_MEDIA_V2)
    // Potentially unblock GStreamer thread for DRM license acquisition.
    m_drmKeySemaphore.signal ();
#endif

    m_url = KURL(KURL(), cleanURL);
    g_object_set(m_playBin.get(), "uri", cleanURL.utf8().data(), NULL);

    INFO_MEDIA_MESSAGE("Load %s", cleanURL.utf8().data());

    if (m_preload == MediaPlayer::None) {
        LOG_MEDIA_MESSAGE("Delaying load.");
        m_delayingLoad = true;
    }

    // Reset network and ready states. Those will be set properly once
    // the pipeline pre-rolled.
    m_networkState = MediaPlayer::Loading;
    m_player->networkStateChanged();
    m_readyState = MediaPlayer::HaveNothing;
    m_player->readyStateChanged();
    m_volumeAndMuteInitialized = false;

    if (!m_delayingLoad)
        commitLoad();
}

#if ENABLE(MEDIA_SOURCE)
void MediaPlayerPrivateGStreamer::load(const String& url, MediaSourcePrivateClient* mediaSource)
{
    LOG_MEDIA_MESSAGE("Trying to open a mediasource");
    String mediasourceUri = String::format("mediasource%s", url.utf8().data());
    m_mediaSource = mediaSource;
    load(mediasourceUri);
}
#endif

void MediaPlayerPrivateGStreamer::commitLoad()
{
    ASSERT(!m_delayingLoad);
    LOG_MEDIA_MESSAGE("Committing load.");

    // GStreamer needs to have the pipeline set to a paused state to
    // start providing anything useful.
    gst_element_set_state(m_playBin.get(), GST_STATE_PAUSED);

    setDownloadBuffering();
    updateStates();
}

float MediaPlayerPrivateGStreamer::playbackPosition() const
{
    if (m_isEndReached) {
        // Position queries on a null pipeline return 0. If we're at
        // the end of the stream the pipeline is null but we want to
        // report either the seek time or the duration because this is
        // what the Media element spec expects us to do.
        if (m_seeking)
            return m_seekTime;
        if (m_mediaDuration)
            return m_mediaDuration;
        return 0;
    }

    // Position is only available if no async state change is going on and the state is either paused or playing.
    gint64 position = GST_CLOCK_TIME_NONE;
    GstQuery* query= gst_query_new_position(GST_FORMAT_TIME);
    if (gst_element_query(m_playBin.get(), query))
        gst_query_parse_position(query, 0, &position);

    float result = 0.0f;
    if (static_cast<GstClockTime>(position) != GST_CLOCK_TIME_NONE) {
        result = static_cast<double>(position) / GST_SECOND;
    } else if (m_canFallBackToLastFinishedSeekPosition)
        result = m_seekTime;

    LOG_MEDIA_MESSAGE("Position %" GST_TIME_FORMAT, GST_TIME_ARGS(position));

    gst_query_unref(query);

    return result;
}

bool MediaPlayerPrivateGStreamer::changePipelineState(GstState newState)
{
    ASSERT(newState == GST_STATE_PLAYING || newState == GST_STATE_PAUSED);

    GstState currentState;
    GstState pending;

    gst_element_get_state(m_playBin.get(), &currentState, &pending, 0);
    if (currentState == newState || pending == newState) {
        LOG_MEDIA_MESSAGE("Rejected state change to %s from %s with %s pending", gst_element_state_get_name(newState),
            gst_element_state_get_name(currentState), gst_element_state_get_name(pending));
        return true;
    }

    LOG_MEDIA_MESSAGE("Changing state change to %s from %s with %s pending", gst_element_state_get_name(newState),
        gst_element_state_get_name(currentState), gst_element_state_get_name(pending));

    GstStateChangeReturn setStateResult = gst_element_set_state(m_playBin.get(), newState);
    GstState pausedOrPlaying = newState == GST_STATE_PLAYING ? GST_STATE_PAUSED : GST_STATE_PLAYING;
    if (currentState != pausedOrPlaying && setStateResult == GST_STATE_CHANGE_FAILURE) {
        loadingFailed(MediaPlayer::Empty);
        return false;
    }
    return true;
}

void MediaPlayerPrivateGStreamer::prepareToPlay()
{
    m_preload = MediaPlayer::Auto;
    if (m_delayingLoad) {
        m_delayingLoad = false;
        commitLoad();
    }
}

void MediaPlayerPrivateGStreamer::play()
{
    if (!m_playbackRate) {
        m_playbackRatePause = true;
        return;
    }

    if (changePipelineState(GST_STATE_PLAYING)) {
        m_isEndReached = false;
        m_delayingLoad = false;
        m_preload = MediaPlayer::Auto;
        setDownloadBuffering();
        LOG_MEDIA_MESSAGE("Play");
    }
}

void MediaPlayerPrivateGStreamer::pause()
{
    m_playbackRatePause = false;
    GstState currentState, pendingState;
    gst_element_get_state(m_playBin.get(), &currentState, &pendingState, 0);
    if (currentState < GST_STATE_PAUSED && pendingState <= GST_STATE_PAUSED)
        return;

    if (changePipelineState(GST_STATE_PAUSED))
        INFO_MEDIA_MESSAGE("Pause");
}

float MediaPlayerPrivateGStreamer::duration() const
{
    if (!m_playBin)
        return 0.0f;

    if (m_errorOccured)
        return 0.0f;

    // Media duration query failed already, don't attempt new useless queries.
    if (!m_mediaDurationKnown)
        return numeric_limits<float>::infinity();

    if (m_mediaDuration)
        return m_mediaDuration;

    GstFormat timeFormat = GST_FORMAT_TIME;
    gint64 timeLength = 0;

#ifdef GST_API_VERSION_1
    bool failure = !gst_element_query_duration(m_playBin.get(), timeFormat, &timeLength) || static_cast<guint64>(timeLength) == GST_CLOCK_TIME_NONE;
    if (failure && m_source)
        failure = !gst_element_query_duration(m_source.get(), timeFormat, &timeLength) || static_cast<guint64>(timeLength) == GST_CLOCK_TIME_NONE;
#else
    bool failure = !gst_element_query_duration(m_playBin.get(), &timeFormat, &timeLength) || timeFormat != GST_FORMAT_TIME || static_cast<guint64>(timeLength) == GST_CLOCK_TIME_NONE;
#endif
    if (failure && m_mediaSource)
        return m_mediaSource->duration();
    if (failure) {
        LOG_MEDIA_MESSAGE("Time duration query failed for %s", m_url.string().utf8().data());
        return numeric_limits<float>::infinity();
    }

    LOG_MEDIA_MESSAGE("Duration: %" GST_TIME_FORMAT, GST_TIME_ARGS(timeLength));

    m_mediaDuration = static_cast<double>(timeLength) / GST_SECOND;
    return m_mediaDuration;
    // FIXME: handle 3.14.9.5 properly
}

float MediaPlayerPrivateGStreamer::currentTime() const
{
    if (!m_playBin) {
        printf("### %s: (NO PLAYBIN) %f\n", __PRETTY_FUNCTION__, 0.0f); fflush(stdout);
        return 0.0f;
    }

    if (m_errorOccured) {
        printf("### %s: (ERROR) %f\n", __PRETTY_FUNCTION__, 0.0f); fflush(stdout);
        return 0.0f;
    }

    if (m_seeking) {
        printf("### %s: (SEEKING) %f\n", __PRETTY_FUNCTION__, m_seekTime); fflush(stdout);
        return m_seekTime;
    }

    // Workaround for
    // https://bugzilla.gnome.org/show_bug.cgi?id=639941 In GStreamer
    // 0.10.35 basesink reports wrong duration in case of EOS and
    // negative playback rate. There's no upstream accepted patch for
    // this bug yet, hence this temporary workaround.
    if (m_isEndReached && m_playbackRate < 0) {
        printf("### %s: (END OR NEGATIVE) %f\n", __PRETTY_FUNCTION__, 0.0f); fflush(stdout);
        return 0.0f;
    }

    float playpos = playbackPosition();
    printf("### %s: (PLAYBACK POSITION) %f\n", __PRETTY_FUNCTION__, playpos); fflush(stdout);
    return playpos;
}

void MediaPlayerPrivateGStreamer::seek(float time)
{
    printf("### %s: time=%f\n", __PRETTY_FUNCTION__, time); fflush(stdout);

    if (!m_playBin) {

        printf("### %s: No playbin, returning\n", __PRETTY_FUNCTION__); fflush(stdout);
        return;
    }

    if (m_errorOccured) {
        printf("### %s: Error occured, returning\n", __PRETTY_FUNCTION__); fflush(stdout);
        return;
    }

    INFO_MEDIA_MESSAGE("[Seek] seek attempt to %f secs", time);

    // Avoid useless seeking.
    if (time == currentTime()) {
        printf("### %s: Time equals currentTime(), returning\n", __PRETTY_FUNCTION__); fflush(stdout);
        return;
    }

    if (isLiveStream()) {
        printf("### %s: Is live stream, returning\n", __PRETTY_FUNCTION__); fflush(stdout);
        return;
    }

    GstClockTime clockTime = toGstClockTime(time);
    INFO_MEDIA_MESSAGE("[Seek] seeking to %" GST_TIME_FORMAT " (%f)", GST_TIME_ARGS(clockTime), time);

    if (m_seeking) {
        m_timeOfOverlappingSeek = time;
        if (m_seekIsPending) {
            m_seekTime = time;
            printf("### %s: Seek pending, returning\n", __PRETTY_FUNCTION__); fflush(stdout);
            return;
        }
    }

    GstState state;
    GstStateChangeReturn getStateResult = gst_element_get_state(m_playBin.get(), &state, 0, 0);
    if (getStateResult == GST_STATE_CHANGE_FAILURE || getStateResult == GST_STATE_CHANGE_NO_PREROLL) {
        LOG_MEDIA_MESSAGE("[Seek] cannot seek, current state change is %s", gst_element_state_change_return_get_name(getStateResult));
        printf("### %s: State change failure or no preroll (%s), returning\n", __PRETTY_FUNCTION__, gst_element_state_change_return_get_name(getStateResult)); fflush(stdout);
        return;
    }

    if (getStateResult == GST_STATE_CHANGE_ASYNC || state < GST_STATE_PAUSED || m_isEndReached) {
        m_seekIsPending = true;
        printf("### %s: State change async, less than paused or end reached\n", __PRETTY_FUNCTION__); fflush(stdout);
        if (m_isEndReached) {
            printf("### %s: Resetting pipeline\n", __PRETTY_FUNCTION__); fflush(stdout);
            LOG_MEDIA_MESSAGE("[Seek] reset pipeline");
            m_resetPipeline = true;
            changePipelineState(GST_STATE_PAUSED);
        }
    } else {
        gint64 startTime, endTime;
        if (m_player->rate() > 0) {
            startTime = clockTime;
            endTime = GST_CLOCK_TIME_NONE;
        } else {
            startTime = 0;
            endTime = clockTime;
        }

        printf("### %s: We can seek now to %f (startTime=%" GST_TIME_FORMAT ", endTime=%" GST_TIME_FORMAT ")\n", __PRETTY_FUNCTION__, time, GST_TIME_ARGS(startTime), GST_TIME_ARGS(endTime)); fflush(stdout);

        if (isMediaSource()) {
            webkit_media_src_set_seek_time(WEBKIT_MEDIA_SRC(m_source.get()), MediaTime(double(time)));
        }

        // We can seek now.
        printf("### %s: gst_element_seek()\n", __PRETTY_FUNCTION__); fflush(stdout);
        if (!gst_element_seek(m_playBin.get(), m_player->rate(), GST_FORMAT_TIME, static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
            GST_SEEK_TYPE_SET, startTime, GST_SEEK_TYPE_SET, endTime)) {
            LOG_MEDIA_MESSAGE("[Seek] seeking to %f failed", time);
            printf("### %s: Seeking to %f (startTime=%" GST_TIME_FORMAT ", endTime=%" GST_TIME_FORMAT ") failed\n", __PRETTY_FUNCTION__, time, GST_TIME_ARGS(startTime), GST_TIME_ARGS(endTime)); fflush(stdout);
            return;
        }

        if (isMediaSource()) {
            m_mediaSource->seekToTime(time);
        }
    }

    m_seeking = true;
    m_seekTime = time;
    m_isEndReached = false;
    printf("### %s: Method completed\n", __PRETTY_FUNCTION__); fflush(stdout);
}

bool MediaPlayerPrivateGStreamer::doSeek(gint64 position, float rate, GstSeekFlags seekType)
{
    printf("### %s: We can finally seek to position=%" GST_TIME_FORMAT "\n", __PRETTY_FUNCTION__, GST_TIME_ARGS(position)); fflush(stdout);

    gint64 startTime, endTime;

    // TODO: Should do more than that, need to notify the media source
    // and probably flush the pipeline at least
    /*
    if (isMediaSource()) {
        printf("### Returing because isMediaSource\n"); fflush(stdout);
        return true;
    }
    */

    if (rate > 0) {
        startTime = position;
        endTime = GST_CLOCK_TIME_NONE;
    } else {
        startTime = 0;
        // If we are at beginning of media, start from the end to
        // avoid immediate EOS.
        if (position < 0)
            endTime = static_cast<gint64>(duration() * GST_SECOND);
        else
            endTime = position;
    }

    if (!rate)
        rate = 1.0;

    MediaTime time = MediaTime(double(static_cast<double>(position) / GST_SECOND));

    if (isMediaSource()) {
        webkit_media_src_set_seek_time(WEBKIT_MEDIA_SRC(m_source.get()), time);
    }

    printf("### %s: gst_element_seek()\n", __PRETTY_FUNCTION__); fflush(stdout);
    if (!gst_element_seek(m_playBin.get(), rate, GST_FORMAT_TIME, seekType,
        GST_SEEK_TYPE_SET, startTime, GST_SEEK_TYPE_SET, endTime)) {
        printf("### %s: Finally seeking to %f (startTime=%" GST_TIME_FORMAT ", endTime=%" GST_TIME_FORMAT ") failed\n", __PRETTY_FUNCTION__, time.toDouble(), GST_TIME_ARGS(startTime), GST_TIME_ARGS(endTime)); fflush(stdout);
        return false;
    }

    if (isMediaSource()) {
        m_mediaSource->seekToTime(time);
    }

    printf("### %s: Method completed\n", __PRETTY_FUNCTION__); fflush(stdout);

    return true;
}

void MediaPlayerPrivateGStreamer::updatePlaybackRate()
{
    if (!m_changingRate)
        return;

    float currentPosition = static_cast<float>(playbackPosition() * GST_SECOND);
    bool mute = false;

    INFO_MEDIA_MESSAGE("Set Rate to %f", m_playbackRate);

    if (m_playbackRate > 0) {
        // Mute the sound if the playback rate is too extreme and
        // audio pitch is not adjusted.
        mute = (!m_preservesPitch && (m_playbackRate < 0.8 || m_playbackRate > 2));
    } else {
        if (currentPosition == 0.0f)
            currentPosition = -1.0f;
        mute = true;
    }

    INFO_MEDIA_MESSAGE("Need to mute audio?: %d", (int) mute);
    if (doSeek(currentPosition, m_playbackRate, static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH))) {
        g_object_set(m_playBin.get(), "mute", mute, NULL);
        m_lastPlaybackRate = m_playbackRate;
    } else {
        m_playbackRate = m_lastPlaybackRate;
        ERROR_MEDIA_MESSAGE("Set rate to %f failed", m_playbackRate);
    }

    if (m_playbackRatePause) {
        GstState state;
        GstState pending;

        gst_element_get_state(m_playBin.get(), &state, &pending, 0);
        if (state != GST_STATE_PLAYING && pending != GST_STATE_PLAYING)
            changePipelineState(GST_STATE_PLAYING);
        m_playbackRatePause = false;
    }

    m_changingRate = false;
    m_player->rateChanged();
}

bool MediaPlayerPrivateGStreamer::paused() const
{
    if (m_isEndReached) {
        LOG_MEDIA_MESSAGE("Ignoring pause at EOS");
        return true;
    }

    if (m_playbackRatePause)
        return false;

    GstState state;
    gst_element_get_state(m_playBin.get(), &state, 0, 0);
    return state <= GST_STATE_PAUSED;
}

bool MediaPlayerPrivateGStreamer::seeking() const
{
    return m_seeking;
}

void MediaPlayerPrivateGStreamer::videoChanged()
{
    if (m_videoTimerHandler)
        g_source_remove(m_videoTimerHandler);
    m_videoTimerHandler = g_idle_add_full(G_PRIORITY_DEFAULT, reinterpret_cast<GSourceFunc>(mediaPlayerPrivateVideoChangeTimeoutCallback), this, 0);
    g_source_set_name_by_id(m_videoTimerHandler, "[WebKit] mediaPlayerPrivateVideoChangeTimeoutCallback");
}

void MediaPlayerPrivateGStreamer::notifyPlayerOfVideo()
{
    printf("### %s\n", __PRETTY_FUNCTION__); fflush(stdout);

    m_videoTimerHandler = 0;
    gint numTracks = 0;
    bool useMediaSource = false;
    if (m_playBin) {
#if ENABLE(MEDIA_SOURCE)
        if (m_mediaSource && WEBKIT_IS_MEDIA_SRC(m_source.get())) {
            g_object_get(m_source.get(), "n-video", &numTracks, NULL);
            useMediaSource = true;
        } else
#endif
            g_object_get(m_playBin.get(), "n-video", &numTracks, NULL);
    }

    m_hasVideo = numTracks > 0;
    m_videoSize = IntSize();

#if ENABLE(VIDEO_TRACK)
    for (gint i = 0; i < numTracks; ++i) {
        GRefPtr<GstPad> pad;
#if ENABLE(MEDIA_SOURCE)
        if (useMediaSource)
            pad = webkit_media_src_get_video_pad(WEBKIT_MEDIA_SRC(m_source.get()), i);
        else
#endif
            g_signal_emit_by_name(m_playBin.get(), "get-video-pad", i, &pad.outPtr(), NULL);
        ASSERT(pad);

        if (i < static_cast<gint>(m_videoTracks.size())) {
            RefPtr<VideoTrackPrivateGStreamer> existingTrack = m_videoTracks[i];
            existingTrack->setIndex(i);
            if (existingTrack->pad() == pad)
                continue;
        }

        RefPtr<VideoTrackPrivateGStreamer> track = VideoTrackPrivateGStreamer::create(m_playBin, i, pad);
        m_videoTracks.append(track);
#if ENABLE(MEDIA_SOURCE)
        if (isMediaSource()) {
            // TODO: This will leak if the event gets freed without arriving in
            // the source, e.g. when we go flushing here
            RefPtr<VideoTrackPrivateGStreamer>* trackCopy = new RefPtr<VideoTrackPrivateGStreamer>(track);
            GstStructure* videoEventStructure = gst_structure_new("webKitVideoTrack", "track", G_TYPE_POINTER, trackCopy, nullptr);

            if (useMediaSource) {
                // Using the stream->demuxersrcpad. The GstUriDecodeBin pad
                // may not exist at this point. Using an alternate way to
                // notify the upper layer.
                webkit_media_src_track_added(WEBKIT_MEDIA_SRC(m_source.get()), pad.get(), gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, videoEventStructure));
            } else {
                // Using the GstUriDecodeBin source pad
                gst_pad_push_event(pad.get(), gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, videoEventStructure));
            }
        }
#endif
        m_player->addVideoTrack(track.release());
    }

    while (static_cast<gint>(m_videoTracks.size()) > numTracks) {
        RefPtr<VideoTrackPrivateGStreamer> track = m_videoTracks.last();
        track->disconnect();
        m_videoTracks.removeLast();
        m_player->removeVideoTrack(track.release());
    }
#endif

    ASSERT(m_player->mediaPlayerClient());
    m_player->mediaPlayerClient()->mediaPlayerEngineUpdated(m_player);

    // m_videoTimerHandler = 0;

    // gint videoTracks = 0;
    // if (m_playBin)
    //     g_object_get(m_playBin.get(), "n-video", &videoTracks, NULL);

    // m_hasVideo = videoTracks > 0;

    // m_videoSize = IntSize();

    // m_player->mediaPlayerClient()->mediaPlayerEngineUpdated(m_player);
}

void MediaPlayerPrivateGStreamer::audioChanged()
{
    if (m_audioTimerHandler)
        g_source_remove(m_audioTimerHandler);
    m_audioTimerHandler = g_idle_add_full(G_PRIORITY_DEFAULT, reinterpret_cast<GSourceFunc>(mediaPlayerPrivateAudioChangeTimeoutCallback), this, 0);
    g_source_set_name_by_id(m_audioTimerHandler, "[WebKit] mediaPlayerPrivateAudioChangeTimeoutCallback");
}

void MediaPlayerPrivateGStreamer::notifyPlayerOfAudio()
{
    printf("### %s\n", __PRETTY_FUNCTION__); fflush(stdout);

    m_audioTimerHandler = 0;
    gint numTracks = 0;
    bool useMediaSource = false;
    if (m_playBin) {
#if ENABLE(MEDIA_SOURCE)
        if (m_mediaSource && WEBKIT_IS_MEDIA_SRC(m_source.get())) {
            g_object_get(m_source.get(), "n-audio", &numTracks, NULL);
            useMediaSource = true;
        } else
#endif
            g_object_get(m_playBin.get(), "n-audio", &numTracks, NULL);
    }

    m_hasAudio = numTracks > 0;

#if ENABLE(VIDEO_TRACK)
    for (gint i = 0; i < numTracks; ++i) {
        GRefPtr<GstPad> pad;
#if ENABLE(MEDIA_SOURCE)
        if (useMediaSource)
            pad = webkit_media_src_get_audio_pad(WEBKIT_MEDIA_SRC(m_source.get()), i);
        else
#endif
            g_signal_emit_by_name(m_playBin.get(), "get-audio-pad", i, &pad.outPtr(), NULL);
        ASSERT(pad);

        if (i < static_cast<gint>(m_audioTracks.size())) {
            RefPtr<AudioTrackPrivateGStreamer> existingTrack = m_audioTracks[i];
            existingTrack->setIndex(i);
            if (existingTrack->pad() == pad)
                continue;
        }

        RefPtr<AudioTrackPrivateGStreamer> track = AudioTrackPrivateGStreamer::create(m_playBin, i, pad);
        m_audioTracks.insert(i, track);
 #if ENABLE(MEDIA_SOURCE)
         if (isMediaSource()) {
             // TODO: This will leak if the event gets freed without arriving in
             // the source, e.g. when we go flushing here
             RefPtr<AudioTrackPrivateGStreamer>* trackCopy = new RefPtr<AudioTrackPrivateGStreamer>(track);
             GstStructure* audioEventStructure = gst_structure_new("webKitAudioTrack", "track", G_TYPE_POINTER, trackCopy, nullptr);

             if (useMediaSource) {
                 // Using the stream->demuxersrcpad. The GstUriDecodeBin pad
                 // may not exist at this point. Using an alternate way to
                 // notify the upper layer.
                 webkit_media_src_track_added(WEBKIT_MEDIA_SRC(m_source.get()), pad.get(), gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, audioEventStructure));
             } else {
                 // Using the GstUriDecodeBin source pad
                 gst_pad_push_event(pad.get(), gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, audioEventStructure));
             }
         }
 #endif
        m_player->addAudioTrack(track.release());
    }

    while (static_cast<gint>(m_audioTracks.size()) > numTracks) {
        RefPtr<AudioTrackPrivateGStreamer> track = m_audioTracks.last();
        track->disconnect();
        m_audioTracks.removeLast();
        m_player->removeAudioTrack(track.release());
    }
#endif

    ASSERT(m_player->mediaPlayerClient());
    m_player->mediaPlayerClient()->mediaPlayerEngineUpdated(m_player);
    // m_audioTimerHandler = 0;

    // gint audioTracks = 0;
    // if (m_playBin)
    //     g_object_get(m_playBin.get(), "n-audio", &audioTracks, NULL);
    // m_hasAudio = audioTracks > 0;
    // m_player->mediaPlayerClient()->mediaPlayerEngineUpdated(m_player);
}

#if ENABLE(VIDEO_TRACK)
void MediaPlayerPrivateGStreamer::textChanged()
{
    if (m_textTimerHandler)
        g_source_remove(m_textTimerHandler);
    m_textTimerHandler = g_idle_add_full(G_PRIORITY_DEFAULT, reinterpret_cast<GSourceFunc>(mediaPlayerPrivateTextChangedCallback), this, 0);
    g_source_set_name_by_id(m_textTimerHandler, "[WebKit] mediaPlayerPrivateTextChangeCallback");
}

void MediaPlayerPrivateGStreamer::notifyPlayerOfText()
{
    gint numTracks = 0;
    bool useMediaSource = false;
    if (m_playBin) {
#if ENABLE(MEDIA_SOURCE)
        if (m_mediaSource && WEBKIT_IS_MEDIA_SRC(m_source.get())) {
            g_object_get(m_source.get(), "n-text", &numTracks, NULL);
            useMediaSource = true;
        } else
#endif
            g_object_get(m_playBin.get(), "n-text", &numTracks, NULL);
    }

    for (gint i = 0; i < numTracks; ++i) {
        GRefPtr<GstPad> pad;
#if ENABLE(MEDIA_SOURCE)
        if (useMediaSource)
            pad = webkit_media_src_get_text_pad(WEBKIT_MEDIA_SRC(m_source.get()), i);
        else
#endif
            g_signal_emit_by_name(m_playBin.get(), "get-text-pad", i, &pad.outPtr(), NULL);
        ASSERT(pad);

        if (i < static_cast<gint>(m_textTracks.size())) {
            RefPtr<InbandTextTrackPrivateGStreamer> existingTrack = m_textTracks[i];
            existingTrack->setIndex(i);
            if (existingTrack->pad() == pad)
                continue;
        }

        RefPtr<InbandTextTrackPrivateGStreamer> track = InbandTextTrackPrivateGStreamer::create(i, pad);
        m_textTracks.insert(i, track);
        // TODO: Text tracks for the media source
        m_player->addTextTrack(track.release());
    }

    while (static_cast<gint>(m_textTracks.size()) > numTracks) {
        RefPtr<InbandTextTrackPrivateGStreamer> track = m_textTracks.last();
        track->disconnect();
        m_textTracks.removeLast();
        m_player->removeTextTrack(track.release());
    }
}

void MediaPlayerPrivateGStreamer::newTextSample()
{
    if (!m_textAppSink)
        return;

    GRefPtr<GstEvent> streamStartEvent = adoptGRef(
        gst_pad_get_sticky_event(m_textAppSinkPad.get(), GST_EVENT_STREAM_START, 0));

    GRefPtr<GstSample> sample;
    g_signal_emit_by_name(m_textAppSink.get(), "pull-sample", &sample.outPtr(), NULL);
    ASSERT(sample);

    if (streamStartEvent) {
        bool found = FALSE;
        const gchar* id;
        gst_event_parse_stream_start(streamStartEvent.get(), &id);
        for (size_t i = 0; i < m_textTracks.size(); ++i) {
            RefPtr<InbandTextTrackPrivateGStreamer> track = m_textTracks[i];
            if (track->streamId() == id) {
                track->handleSample(sample);
                found = true;
                break;
            }
        }
        if (!found)
            WARN_MEDIA_MESSAGE("Got sample with unknown stream ID.");
    } else
        WARN_MEDIA_MESSAGE("Unable to handle sample with no stream start event.");
}
#endif

// METRO FIXME: GStreamer mediaplayer manages the readystate on its own. We shouldn't change it manually.
void MediaPlayerPrivateGStreamer::setReadyState(MediaPlayer::ReadyState state)
{
    if (state != m_readyState) {
        LOG_MEDIA_MESSAGE("Ready State Changed manually from %u to %u", m_readyState, state);
        m_readyState = state;
        m_player->readyStateChanged();
    }
}

void MediaPlayerPrivateGStreamer::setRate(float rate)
{
    // Higher rate causes crash.
    rate = clampTo(rate, -20.0, 20.0);

    // Avoid useless playback rate update.
    if (m_playbackRate == rate) {
        // and make sure that upper layers were notified if rate was set

        if (!m_changingRate && m_player->rate() != m_playbackRate)
            m_player->rateChanged();
        return;
    }

    if (isLiveStream()) {
        // notify upper layers that we cannot handle passed rate.
        m_changingRate = false;
        m_player->rateChanged();
        return;
    }

    GstState state;
    GstState pending;

    m_playbackRate = rate;
    m_changingRate = true;

    gst_element_get_state(m_playBin.get(), &state, &pending, 0);

    if (!rate) {
        m_changingRate = false;
        m_playbackRatePause = true;
        if (state != GST_STATE_PAUSED && pending != GST_STATE_PAUSED)
            changePipelineState(GST_STATE_PAUSED);
        return;
    }

    if ((state != GST_STATE_PLAYING && state != GST_STATE_PAUSED)
        || (pending == GST_STATE_PAUSED))
        return;

    updatePlaybackRate();
}

void MediaPlayerPrivateGStreamer::setPreservesPitch(bool preservesPitch)
{
    m_preservesPitch = preservesPitch;
}

PassOwnPtr<PlatformTimeRanges> MediaPlayerPrivateGStreamer::buffered() const
{
#if ENABLE(MEDIA_SOURCE)
    if (isMediaSource())
        return m_mediaSource->buffered();
#endif

    OwnPtr<PlatformTimeRanges> timeRanges = PlatformTimeRanges::create();
    if (m_errorOccured || isLiveStream())
        return timeRanges.release();

#if GST_CHECK_VERSION(0, 10, 31)
    float mediaDuration(duration());
    if (!mediaDuration || std::isinf(mediaDuration))
        return timeRanges.release();

    GstQuery* query = gst_query_new_buffering(GST_FORMAT_PERCENT);

    if (!gst_element_query(m_playBin.get(), query)) {
        gst_query_unref(query);
        return timeRanges.release();
    }

    guint numBufferingRanges = gst_query_get_n_buffering_ranges(query);
    for (guint index = 0; index < numBufferingRanges; index++) {
        gint64 rangeStart = 0, rangeStop = 0;
        if (gst_query_parse_nth_buffering_range(query, index, &rangeStart, &rangeStop))
            timeRanges->add(MediaTime::createWithDouble((rangeStart * mediaDuration) / gPercentMax),
                MediaTime::createWithDouble((rangeStop * mediaDuration) / gPercentMax));
    }

    // Fallback to the more general maxTimeLoaded() if no range has
    // been found.
    if (!timeRanges->length())
        if (float loaded = maxTimeLoaded())
            timeRanges->add(MediaTime::zeroTime(), MediaTime::createWithDouble(loaded));

    gst_query_unref(query);
#else
    float loaded = maxTimeLoaded();
    if (!m_errorOccured && !isLiveStream() && loaded > 0)
        timeRanges->add(MediaTime::zeroTime(), MediaTime::createWithDouble(loaded));
#endif
    return timeRanges.release();
}

#if ENABLE(ENCRYPTED_MEDIA) || ENABLE(ENCRYPTED_MEDIA_V2)
struct MainThreadNeedKeyCallbackInfo {
    MainThreadNeedKeyCallbackInfo(MediaPlayerPrivateGStreamer* handle, PassRefPtr<Uint8Array> initData) : handle(handle), initData(initData) { }
    MediaPlayerPrivateGStreamer* handle;
    RefPtr<Uint8Array> initData;
};
#endif

static gboolean mediaPlayerPrivateNotifyDurationChanged(MediaPlayerPrivateGStreamer* instance);

#if ENABLE(MEDIA_SOURCE)
static StreamType getStreamType(GstElement* element)
{
    g_return_val_if_fail(GST_IS_ELEMENT(element), STREAM_TYPE_UNKNOWN);

    GstIterator* it;
    GstPad* pad;
    GValue item = G_VALUE_INIT;
    StreamType result = STREAM_TYPE_UNKNOWN;

    it = gst_element_iterate_sink_pads(element);

    if (it && (gst_iterator_next(it, &item)) == GST_ITERATOR_OK
        && ((pad = GST_PAD(g_value_get_object(&item))) != 0)) {
        GstCaps* caps = gst_pad_get_current_caps(pad);
        const gchar* mediatype = gst_structure_get_name(gst_caps_get_structure(caps, 0));
        // Look for "audio", "video", "text"
        switch (mediatype[0]) {
        case 'a':
            result = STREAM_TYPE_AUDIO;
            break;
        case 'v':
            result = STREAM_TYPE_VIDEO;
            break;
        case 't':
            result = STREAM_TYPE_TEXT;
            break;
        default:
            break;
        }
        gst_caps_unref(caps);
    }

    g_value_unset(&item);

    if (it)
        gst_iterator_free(it);

    return result;
}
#endif

void MediaPlayerPrivateGStreamer::handleSyncMessage(GstMessage* message)
{
    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ELEMENT:
        case GST_MESSAGE_APPLICATION:
        {
#if ENABLE(ENCRYPTED_MEDIA) || ENABLE(ENCRYPTED_MEDIA_V2)
            const GstStructure* s = gst_message_get_structure (message);
            /* Here we receive the DRM init data from the pipeline: we will emit
             * the needkey event with that data and the browser might create a 
             * CDMSession from this event handler. If such a session was created
             * We will emit the message event from the session to provide the 
             * DRM challenge to the browser and wait for an update. If on the
             * contrary no session was created we won't wait and let the pipeline
             * error out by itself. */
            if (gst_structure_has_name (s, "drm-key-needed")) {
                const guint8 *data = NULL;
                guint32 data_length = 0;
                gboolean ret = gst_structure_get (s, "data", G_TYPE_POINTER, &data, "data-length", G_TYPE_UINT, &data_length, NULL);

                if (ret && data_length) {
                  GST_DEBUG ("queueing keyNeeded event with %u bytes of data", data_length);
                  RefPtr<Uint8Array> initData = Uint8Array::create(reinterpret_cast<const unsigned char *>(data), data_length);
                  MainThreadNeedKeyCallbackInfo info(this, initData);
                  // We need to reset the semaphore first. signal, wait
                  m_drmKeySemaphore.signal ();
                  m_drmKeySemaphore.wait ();
                  // Fire the need key event from main thread
                  callOnMainThreadAndWait(needKeyEventFromMain, &info);
                  // Wait for a potential license
                  GST_DEBUG ("waiting for a license");
                  m_drmKeySemaphore.wait ();
                  GST_DEBUG ("finished waiting");
                }
            }
#endif
            break;
        }
#ifdef GST_API_VERSION_1
        case GST_MESSAGE_DURATION_CHANGED:
#else
        case GST_MESSAGE_DURATION:
#endif
            {
                m_pendingAsyncOperationsLock.lock();
                guint asyncOperationId = g_timeout_add(0, (GSourceFunc)mediaPlayerPrivateNotifyDurationChanged, this);
                m_pendingAsyncOperations = g_list_append(m_pendingAsyncOperations, GUINT_TO_POINTER(asyncOperationId));
                m_pendingAsyncOperationsLock.unlock();
            }
            break;

        default:
            break;
    }
}

void MediaPlayerPrivateGStreamer::handleMessage(GstMessage* message)
{
    GOwnPtr<GError> err;
    GOwnPtr<gchar> debug;
    MediaPlayer::NetworkState error;
    bool issueError = true;
    bool attemptNextLocation = false;
    const GstStructure* structure = gst_message_get_structure(message);
    GstState requestedState, currentState;

    m_canFallBackToLastFinishedSeekPosition = false;

    if (structure) {
        const gchar* messageTypeName = gst_structure_get_name(structure);

        // Redirect messages are sent from elements, like qtdemux, to
        // notify of the new location(s) of the media.
        if (!g_strcmp0(messageTypeName, "redirect")) {
            mediaLocationChanged(message);
            return;
        }
    }

    // We ignore state changes from internal elements. They are forwarded to playbin2 anyway.
    bool messageSourceIsPlaybin = GST_MESSAGE_SRC(message) == reinterpret_cast<GstObject*>(m_playBin.get());

    printf("### %s: MESSAGE (%s) %s\n", __PRETTY_FUNCTION__, GST_MESSAGE_SRC_NAME(message), GST_MESSAGE_TYPE_NAME(message)); fflush(stdout);

    LOG_MEDIA_MESSAGE("Message %s received from element %s", GST_MESSAGE_TYPE_NAME(message), GST_MESSAGE_SRC_NAME(message));
    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR:
        if (m_resetPipeline)
            break;
        if (m_missingPlugins)
            break;
        gst_message_parse_error(message, &err.outPtr(), &debug.outPtr());
        ERROR_MEDIA_MESSAGE("Error %d: %s (url=%s)", err->code, err->message, m_url.string().utf8().data());

        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_playBin.get()), GST_DEBUG_GRAPH_SHOW_ALL, "webkit-video.error");

        error = MediaPlayer::Empty;
        if (err->code == GST_STREAM_ERROR_CODEC_NOT_FOUND
            || err->code == GST_STREAM_ERROR_WRONG_TYPE
            || err->code == GST_STREAM_ERROR_FAILED
            || err->code == GST_CORE_ERROR_MISSING_PLUGIN
            || err->code == GST_RESOURCE_ERROR_NOT_FOUND)
            error = MediaPlayer::FormatError;
        else if (err->domain == GST_STREAM_ERROR) {
            // Let the mediaPlayerClient handle the stream error, in
            // this case the HTMLMediaElement will emit a stalled
            // event.
            if (err->code == GST_STREAM_ERROR_TYPE_NOT_FOUND) {
                ERROR_MEDIA_MESSAGE("Decode error, let the Media element emit a stalled event.");
                break;
            }
            error = MediaPlayer::DecodeError;
            attemptNextLocation = true;
        } else if (err->domain == GST_RESOURCE_ERROR)
            error = MediaPlayer::NetworkError;

        if (attemptNextLocation)
            issueError = !loadNextLocation();
        if (issueError)
            loadingFailed(error);
        break;
    case GST_MESSAGE_EOS:
        didEnd();
        break;
    case GST_MESSAGE_ASYNC_DONE:
        if (!messageSourceIsPlaybin || m_delayingLoad)
            break;
        asyncStateChangeDone();
        break;
    case GST_MESSAGE_STATE_CHANGED: {
        {
            // DEBUG
            GstState newState;
            gst_message_parse_state_changed(message, &currentState, &newState, 0);
            printf("### %s: STATE CHANGE (%s) %s -> %s\n", __PRETTY_FUNCTION__, GST_MESSAGE_SRC_NAME(message), gst_element_state_get_name(currentState), gst_element_state_get_name(newState)); fflush(stdout);
        }
        if (!messageSourceIsPlaybin || m_delayingLoad)
            break;
        updateStates();

        // Construct a filename for the graphviz dot file output.
        GstState newState;
        gst_message_parse_state_changed(message, &currentState, &newState, 0);
        CString dotFileName = String::format("webkit-video.%s_%s", gst_element_state_get_name(currentState), gst_element_state_get_name(newState)).utf8();
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_playBin.get()), GST_DEBUG_GRAPH_SHOW_ALL, dotFileName.data());

        break;
    }
    case GST_MESSAGE_BUFFERING:
        processBufferingStats(message);
        break;
#ifdef GST_API_VERSION_1
    case GST_MESSAGE_DURATION_CHANGED:
#else
    case GST_MESSAGE_DURATION:
#endif
        durationChanged();
        break;
    case GST_MESSAGE_REQUEST_STATE:
        gst_message_parse_request_state(message, &requestedState);
        gst_element_get_state(m_playBin.get(), &currentState, NULL, 250);
        if (requestedState < currentState) {
            GOwnPtr<gchar> elementName(gst_element_get_name(GST_ELEMENT(message)));
            INFO_MEDIA_MESSAGE("Element %s requested state change to %s", elementName.get(),
                gst_element_state_get_name(requestedState));
            m_requestedState = requestedState;
            changePipelineState(requestedState);
        }
        break;
    case GST_MESSAGE_ELEMENT:
        if (gst_is_missing_plugin_message(message)) {
            gchar* detail = gst_missing_plugin_message_get_installer_detail(message);
            gchar* detailArray[2] = {detail, 0};
            GstInstallPluginsReturn result = gst_install_plugins_async(detailArray, 0, mediaPlayerPrivatePluginInstallerResultFunction, this);
            m_missingPlugins = result == GST_INSTALL_PLUGINS_STARTED_OK;
            g_free(detail);
        }
#if ENABLE(VIDEO_TRACK) && USE(GSTREAMER_MPEGTS)
        else {
            GstMpegtsSection* section = gst_message_parse_mpegts_section(message);
            if (section) {
                processMpegTsSection(section);
                gst_mpegts_section_unref(section);
            }
        }
#endif
        break;
#if ENABLE(VIDEO_TRACK)
    case GST_MESSAGE_TOC:
        processTableOfContents(message);
        break;
#endif
#if ENABLE(MEDIA_SOURCE)
    case GST_MESSAGE_RESET_TIME:
        if (m_source && WEBKIT_IS_MEDIA_SRC(m_source.get())) {
                printf("### %s: Received reset-time message for %s\n", __PRETTY_FUNCTION__, GST_MESSAGE_SRC_NAME(message)); fflush(stdout);

                StreamType streamType = getStreamType(GST_ELEMENT(GST_MESSAGE_SRC(message)));
                printf("### %s: streamType=%d\n", __PRETTY_FUNCTION__, streamType); fflush(stdout);

                if (streamType == STREAM_TYPE_AUDIO || streamType == STREAM_TYPE_VIDEO)
                    webkit_media_src_segment_needed(WEBKIT_MEDIA_SRC(m_source.get()), streamType);
        }
        break;
#endif
    default:
        LOG_MEDIA_MESSAGE("Unhandled GStreamer message type: %s",
                    GST_MESSAGE_TYPE_NAME(message));
        break;
    }
}

void MediaPlayerPrivateGStreamer::handlePluginInstallerResult(GstInstallPluginsReturn result)
{
    m_missingPlugins = false;
    if (result == GST_INSTALL_PLUGINS_SUCCESS) {
        gst_element_set_state(m_playBin.get(), GST_STATE_READY);
        gst_element_set_state(m_playBin.get(), GST_STATE_PAUSED);
    }
}

void MediaPlayerPrivateGStreamer::processBufferingStats(GstMessage* message)
{
    m_buffering = true;
    const GstStructure *structure = gst_message_get_structure(message);
    gst_structure_get_int(structure, "buffer-percent", &m_bufferingPercentage);

    LOG_MEDIA_MESSAGE("[Buffering] Buffering: %d%%.", m_bufferingPercentage);

    updateStates();
}

#if ENABLE(VIDEO_TRACK) && USE(GSTREAMER_MPEGTS)
void MediaPlayerPrivateGStreamer::processMpegTsSection(GstMpegtsSection* section)
{
    ASSERT(section);

    if (section->section_type == GST_MPEGTS_SECTION_PMT) {
        const GstMpegtsPMT* pmt = gst_mpegts_section_get_pmt(section);
        m_metadataTracks.clear();
        for (guint i = 0; i < pmt->streams->len; ++i) {
            const GstMpegtsPMTStream* stream = static_cast<const GstMpegtsPMTStream*>(g_ptr_array_index(pmt->streams, i));
            if (stream->stream_type == 0x05 || stream->stream_type >= 0x80) {
                AtomicString pid = String::number(stream->pid);
                RefPtr<InbandMetadataTextTrackPrivateGStreamer> track = InbandMetadataTextTrackPrivateGStreamer::create(
                    InbandTextTrackPrivate::Metadata, InbandTextTrackPrivate::Data, pid);

                // 4.7.10.12.2 Sourcing in-band text tracks
                // If the new text track's kind is metadata, then set the text track in-band metadata track dispatch
                // type as follows, based on the type of the media resource:
                // Let stream type be the value of the "stream_type" field describing the text track's type in the
                // file's program map section, interpreted as an 8-bit unsigned integer. Let length be the value of
                // the "ES_info_length" field for the track in the same part of the program map section, interpreted
                // as an integer as defined by the MPEG-2 specification. Let descriptor bytes be the length bytes
                // following the "ES_info_length" field. The text track in-band metadata track dispatch type must be
                // set to the concatenation of the stream type byte and the zero or more descriptor bytes bytes,
                // expressed in hexadecimal using uppercase ASCII hex digits.
                String inbandMetadataTrackDispatchType;
                appendUnsignedAsHexFixedSize(stream->stream_type, inbandMetadataTrackDispatchType, 2);
                for (guint j = 0; j < stream->descriptors->len; ++j) {
                    const GstMpegtsDescriptor* descriptor = static_cast<const GstMpegtsDescriptor*>(g_ptr_array_index(stream->descriptors, j));
                    for (guint k = 0; k < descriptor->length; ++k)
                        appendByteAsHex(descriptor->data[k], inbandMetadataTrackDispatchType);
                }
                track->setInBandMetadataTrackDispatchType(inbandMetadataTrackDispatchType);

                m_metadataTracks.add(pid, track);
                m_player->addTextTrack(track);
            }
        }
    } else {
        AtomicString pid = String::number(section->pid);
        RefPtr<InbandMetadataTextTrackPrivateGStreamer> track = m_metadataTracks.get(pid);
        if (!track)
            return;

        GRefPtr<GBytes> data = gst_mpegts_section_get_data(section);
        gsize size;
        const void* bytes = g_bytes_get_data(data.get(), &size);

        track->addDataCue(MediaTime::createWithDouble(currentTimeDouble()), MediaTime::createWithDouble(currentTimeDouble()), bytes, size);
    }
}
#endif

#if ENABLE(VIDEO_TRACK)
void MediaPlayerPrivateGStreamer::processTableOfContents(GstMessage* message)
{
    if (m_chaptersTrack)
        m_player->removeTextTrack(m_chaptersTrack);

    m_chaptersTrack = InbandMetadataTextTrackPrivateGStreamer::create(InbandTextTrackPrivate::Chapters, InbandTextTrackPrivate::Generic);
    m_player->addTextTrack(m_chaptersTrack);

    GRefPtr<GstToc> toc;
    gboolean updated;
    gst_message_parse_toc(message, &toc.outPtr(), &updated);
    ASSERT(toc);

    for (GList* i = gst_toc_get_entries(toc.get()); i; i = i->next)
        processTableOfContentsEntry(static_cast<GstTocEntry*>(i->data), 0);
}

void MediaPlayerPrivateGStreamer::processTableOfContentsEntry(GstTocEntry* entry, GstTocEntry* parent)
{
    UNUSED_PARAM(parent);
    ASSERT(entry);

    RefPtr<GenericCueData> cue = GenericCueData::create();

    gint64 start = -1, stop = -1;
    gst_toc_entry_get_start_stop_times(entry, &start, &stop);
    if (start != -1)
        cue->setStartTime(MediaTime(start, GST_SECOND));
    if (stop != -1)
        cue->setEndTime(MediaTime(stop, GST_SECOND));

    GstTagList* tags = gst_toc_entry_get_tags(entry);
    if (tags) {
        gchar* title =  0;
        gst_tag_list_get_string(tags, GST_TAG_TITLE, &title);
        if (title) {
            cue->setContent(title);
            g_free(title);
        }
    }

    m_chaptersTrack->addGenericCue(cue.release());

    for (GList* i = gst_toc_entry_get_sub_entries(entry); i; i = i->next)
        processTableOfContentsEntry(static_cast<GstTocEntry*>(i->data), entry);
}
#endif

void MediaPlayerPrivateGStreamer::fillTimerFired(Timer<MediaPlayerPrivateGStreamer>*)
{
    GstQuery* query = gst_query_new_buffering(GST_FORMAT_PERCENT);

    if (!gst_element_query(m_playBin.get(), query)) {
        gst_query_unref(query);
        return;
    }

    gint64 start, stop;
    gdouble fillStatus = 100.0;

    gst_query_parse_buffering_range(query, 0, &start, &stop, 0);
    gst_query_unref(query);

    if (stop != -1)
        fillStatus = 100.0 * stop / GST_FORMAT_PERCENT_MAX;

    LOG_MEDIA_MESSAGE("[Buffering] Download buffer filled up to %f%%", fillStatus);

    if (!m_mediaDuration)
        durationChanged();

    // Update maxTimeLoaded only if the media duration is
    // available. Otherwise we can't compute it.
    if (m_mediaDuration) {
        if (fillStatus == 100.0)
            m_maxTimeLoaded = m_mediaDuration;
        else
            m_maxTimeLoaded = static_cast<float>((fillStatus * m_mediaDuration) / 100.0);
        LOG_MEDIA_MESSAGE("[Buffering] Updated maxTimeLoaded: %f", m_maxTimeLoaded);
    }

    m_downloadFinished = fillStatus == 100.0;
    if (!m_downloadFinished) {
        updateStates();
        return;
    }

    // Media is now fully loaded. It will play even if network
    // connection is cut. Buffering is done, remove the fill source
    // from the main loop.
    m_fillTimer.stop();
    updateStates();
}

float MediaPlayerPrivateGStreamer::maxTimeSeekable() const
{
    if (m_errorOccured)
        return 0.0f;

    LOG_MEDIA_MESSAGE("maxTimeSeekable");
    // infinite duration means live stream
    if (std::isinf(duration()))
        return 0.0f;

    return duration();
}

float MediaPlayerPrivateGStreamer::maxTimeLoaded() const
{
    if (m_errorOccured)
        return 0.0f;

    float loaded = m_maxTimeLoaded;
    if (m_isEndReached && m_mediaDuration)
        loaded = m_mediaDuration;
    LOG_MEDIA_MESSAGE("maxTimeLoaded: %f", loaded);
    return loaded;
}

bool MediaPlayerPrivateGStreamer::didLoadingProgress() const
{
    if (!m_playBin || !m_mediaDuration || (!isMediaSource() && !totalBytes()))
        return false;
    float currentMaxTimeLoaded = maxTimeLoaded();
    bool didLoadingProgress = currentMaxTimeLoaded != m_maxTimeLoadedAtLastDidLoadingProgress;
    m_maxTimeLoadedAtLastDidLoadingProgress = currentMaxTimeLoaded;
    LOG_MEDIA_MESSAGE("didLoadingProgress: %d", didLoadingProgress);
    return didLoadingProgress;
}

unsigned long long MediaPlayerPrivateGStreamer::totalBytes() const
{
    if (m_errorOccured)
        return 0;

    if (m_totalBytes)
        return m_totalBytes;

    if (!m_source)
        return 0;

    GstFormat fmt = GST_FORMAT_BYTES;
    gint64 length = 0;
#ifdef GST_API_VERSION_1
    if (gst_element_query_duration(m_source.get(), fmt, &length)) {
#else
    if (gst_element_query_duration(m_source.get(), &fmt, &length)) {
#endif
        INFO_MEDIA_MESSAGE("totalBytes %" G_GINT64_FORMAT, length);
        m_totalBytes = static_cast<unsigned long long>(length);
        m_isStreaming = !length;
        return m_totalBytes;
    }

    // Fall back to querying the source pads manually.
    // See also https://bugzilla.gnome.org/show_bug.cgi?id=638749
    GstIterator* iter = gst_element_iterate_src_pads(m_source.get());
    bool done = false;
    while (!done) {
#ifdef GST_API_VERSION_1
        GValue item = G_VALUE_INIT;
        switch (gst_iterator_next(iter, &item)) {
        case GST_ITERATOR_OK: {
            GstPad* pad = static_cast<GstPad*>(g_value_get_object(&item));
            gint64 padLength = 0;
            if (gst_pad_query_duration(pad, fmt, &padLength) && padLength > length)
                length = padLength;
            break;
        }
#else
        gpointer data;

        switch (gst_iterator_next(iter, &data)) {
        case GST_ITERATOR_OK: {
            GRefPtr<GstPad> pad = adoptGRef(GST_PAD_CAST(data));
            gint64 padLength = 0;
            if (gst_pad_query_duration(pad.get(), &fmt, &padLength) && padLength > length)
                length = padLength;
            break;
        }
#endif
        case GST_ITERATOR_RESYNC:
            gst_iterator_resync(iter);
            break;
        case GST_ITERATOR_ERROR:
            // Fall through.
        case GST_ITERATOR_DONE:
            done = true;
            break;
        }

#ifdef GST_API_VERSION_1
        g_value_unset(&item);
#endif
    }

    gst_iterator_free(iter);

    INFO_MEDIA_MESSAGE("totalBytes %" G_GINT64_FORMAT, length);
    m_totalBytes = static_cast<unsigned long long>(length);
    m_isStreaming = !length;
    return m_totalBytes;
}

// #### DEBUG
GstPadProbeReturn videoDecoderProbe (GstPad*, GstPadProbeInfo *info, gpointer)
{
    GstBuffer* buf = GST_BUFFER(info->data);
    printf("### %s: PTS=%" GST_TIME_FORMAT "\n", __PRETTY_FUNCTION__, GST_TIME_ARGS(buf->pts)); fflush(stdout);
    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn videoSinkProbe (GstPad*, GstPadProbeInfo *info, gpointer)
{
    GstBuffer* buf = GST_BUFFER(info->data);
    printf("### %s: PTS=%" GST_TIME_FORMAT "\n", __PRETTY_FUNCTION__, GST_TIME_ARGS(buf->pts)); fflush(stdout);
    return GST_PAD_PROBE_OK;
}

void MediaPlayerPrivateGStreamer::updateAudioSink()
{
    if (!m_playBin)
        return;

    GstElement* sinkPtr = 0;

    g_object_get(m_playBin.get(), "audio-sink", &sinkPtr, NULL);
    m_webkitAudioSink = adoptGRef(sinkPtr);

    // #### DEBUG
    {
        static gulong videoSinkProbeId = 0;
        static gulong videoDecoderProbeId = 0;

        GstIterator* iter = gst_bin_iterate_recurse(GST_BIN(m_pipeline));
        GValue v = G_VALUE_INIT;
        while (gst_iterator_next(iter, &v) == GST_ITERATOR_OK) {
            GstElement* element = GST_ELEMENT(g_value_get_object(&v));
            const gchar* elementName = G_OBJECT_CLASS_NAME(G_OBJECT_GET_CLASS(G_OBJECT(element)));
            if (g_str_equal(elementName, "GstOMXH264Dec-omxh264dec")) {
                GstPad* sinkPad = gst_element_get_static_pad(element, "sink");
                if (sinkPad) {
                    printf("### %s: [DEBUG] Installing probe in video decoder sink\n", __PRETTY_FUNCTION__); fflush(stdout);
                    if (videoDecoderProbeId)
                        gst_pad_remove_probe(sinkPad, videoDecoderProbeId);
                    videoDecoderProbeId = gst_pad_add_probe(sinkPad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback) videoDecoderProbe, NULL, NULL);
                    g_object_unref(sinkPad);
                }
            }
            g_value_reset(&v);
        }
        g_value_unset(&v);
        gst_iterator_free(iter);

        GstElement* videoSink = 0;
        g_object_get(m_playBin.get(), "video-sink", &videoSink, NULL);
        if (videoSink) {
            GstPad* pad = gst_element_get_static_pad(videoSink, "sink");
            if (videoSinkProbeId)
                gst_pad_remove_probe(pad, videoSinkProbeId);
            printf("### %s: [DEBUG] Installing probe in video sink\n", __PRETTY_FUNCTION__); fflush(stdout);
            videoSinkProbeId = gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback) videoSinkProbe, NULL, NULL);
            g_object_unref(pad);
            g_object_unref(videoSink);
        }
    }
}

GstElement* MediaPlayerPrivateGStreamer::audioSink() const
{
    return m_webkitAudioSink.get();
}

void MediaPlayerPrivateGStreamer::sourceChanged()
{
    GstElement* srcPtr = 0;

    g_object_get(m_playBin.get(), "source", &srcPtr, NULL);
    m_source = adoptGRef(srcPtr);

    if (WEBKIT_IS_WEB_SRC(m_source.get()))
        webKitWebSrcSetMediaPlayer(WEBKIT_WEB_SRC(m_source.get()), m_player);
#if ENABLE(MEDIA_SOURCE)
    if (m_mediaSource && WEBKIT_IS_MEDIA_SRC(m_source.get())) {
        MediaSourceGStreamer::open(m_mediaSource.get(), WEBKIT_MEDIA_SRC(m_source.get()), this);
        g_signal_connect(m_source.get(), "video-changed", G_CALLBACK(mediaPlayerPrivateVideoChangedCallback), this);
        g_signal_connect(m_source.get(), "audio-changed", G_CALLBACK(mediaPlayerPrivateAudioChangedCallback), this);
        g_signal_connect(m_source.get(), "text-changed", G_CALLBACK(mediaPlayerPrivateTextChangedCallback), this);
        webkit_media_src_set_mediaplayerprivate(WEBKIT_MEDIA_SRC(m_source.get()), this);
    }
#endif
}

void MediaPlayerPrivateGStreamer::cancelLoad()
{
#if ENABLE(ENCRYPTED_MEDIA) || ENABLE(ENCRYPTED_MEDIA_V2)
    // Potentially unblock GStreamer thread for DRM license acquisition.
    m_drmKeySemaphore.signal ();
#endif

    if (m_networkState < MediaPlayer::Loading || m_networkState == MediaPlayer::Loaded)
        return;

    if (m_playBin)
        gst_element_set_state(m_playBin.get(), GST_STATE_NULL);
}

void MediaPlayerPrivateGStreamer::asyncStateChangeDone()
{
    if (!m_playBin || m_errorOccured)
        return;

    if (m_seeking) {
        if (m_seekIsPending)
            updateStates();
        else {
            LOG_MEDIA_MESSAGE("[Seek] seeked to %f", m_seekTime);
            m_seeking = false;

            if (m_timeOfOverlappingSeek != m_seekTime && m_timeOfOverlappingSeek != -1) {
                seek(m_timeOfOverlappingSeek);
                m_timeOfOverlappingSeek = -1;
                return;
            }
            m_timeOfOverlappingSeek = -1;

            // The pipeline can still have a pending state. In this case a position query will fail.
            // Right now we can use m_seekTime as a fallback.
            m_canFallBackToLastFinishedSeekPosition = true;
            timeChanged();
        }
    } else
        updateStates();
}

void MediaPlayerPrivateGStreamer::updateStates()
{
    if (!m_playBin)
        return;

    if (m_errorOccured)
        return;

    MediaPlayer::NetworkState oldNetworkState = m_networkState;
    MediaPlayer::ReadyState oldReadyState = m_readyState;
    GstState state;
    GstState pending;

    GstStateChangeReturn getStateResult = gst_element_get_state(m_playBin.get(), &state, &pending, 250 * GST_NSECOND);

    bool shouldUpdatePlaybackState = false;
    switch (getStateResult) {
    case GST_STATE_CHANGE_SUCCESS: {
        LOG_MEDIA_MESSAGE("State: %s, pending: %s", gst_element_state_get_name(state), gst_element_state_get_name(pending));

        if (state <= GST_STATE_READY) {
            m_resetPipeline = true;
            m_mediaDuration = 0;
        } else {
            m_resetPipeline = false;
            cacheDuration();
        }

        bool didBuffering = m_buffering;

        // Update ready and network states.
        switch (state) {
        case GST_STATE_NULL:
            m_readyState = MediaPlayer::HaveNothing;
            m_networkState = MediaPlayer::Empty;
            break;
        case GST_STATE_READY:
            m_readyState = MediaPlayer::HaveMetadata;
            m_networkState = MediaPlayer::Empty;
            break;
        case GST_STATE_PAUSED:
        case GST_STATE_PLAYING:
            if (m_buffering) {
                if (m_bufferingPercentage == 100) {
                    LOG_MEDIA_MESSAGE("[Buffering] Complete.");
                    m_buffering = false;
                    m_readyState = MediaPlayer::HaveEnoughData;
                    m_networkState = m_downloadFinished ? MediaPlayer::Idle : MediaPlayer::Loading;
                } else {
                    m_readyState = MediaPlayer::HaveCurrentData;
                    m_networkState = MediaPlayer::Loading;
                }
            } else if (m_downloadFinished) {
                m_readyState = MediaPlayer::HaveEnoughData;
                m_networkState = MediaPlayer::Loaded;
            } else {
                m_readyState = MediaPlayer::HaveFutureData;
                m_networkState = MediaPlayer::Loading;
            }

            break;
        default:
            ASSERT_NOT_REACHED();
            break;
        }

        // Sync states where needed.
        if (state == GST_STATE_PAUSED) {
            if (!m_webkitAudioSink)
                updateAudioSink();

            if (!m_volumeAndMuteInitialized) {
                notifyPlayerOfVolumeChange();
                notifyPlayerOfMute();
                m_volumeAndMuteInitialized = true;
            }

            if (didBuffering && !m_buffering && !m_paused && m_playbackRate) {
                LOG_MEDIA_MESSAGE("[Buffering] Restarting playback.");
                gst_element_set_state(m_playBin.get(), GST_STATE_PLAYING);
            }
        } else if (state == GST_STATE_PLAYING) {
            m_paused = false;

            if ((m_buffering && !isLiveStream()) || !m_playbackRate) {
                LOG_MEDIA_MESSAGE("[Buffering] Pausing stream for buffering.");
                gst_element_set_state(m_playBin.get(), GST_STATE_PAUSED);
            }
        } else
            m_paused = true;

        if (m_requestedState == GST_STATE_PAUSED && state == GST_STATE_PAUSED) {
            shouldUpdatePlaybackState = true;
            LOG_MEDIA_MESSAGE("Requested state change to %s was completed", gst_element_state_get_name(state));
        }

        break;
    }
    case GST_STATE_CHANGE_ASYNC:
        LOG_MEDIA_MESSAGE("Async: State: %s, pending: %s", gst_element_state_get_name(state), gst_element_state_get_name(pending));
        // Change in progress.
        break;
    case GST_STATE_CHANGE_FAILURE:
        LOG_MEDIA_MESSAGE("Failure: State: %s, pending: %s", gst_element_state_get_name(state), gst_element_state_get_name(pending));
        // Change failed
        return;
    case GST_STATE_CHANGE_NO_PREROLL:
        LOG_MEDIA_MESSAGE("No preroll: State: %s, pending: %s", gst_element_state_get_name(state), gst_element_state_get_name(pending));

        // Live pipelines go in PAUSED without prerolling.
        m_isStreaming = true;
        setDownloadBuffering();

        if (state == GST_STATE_READY)
            m_readyState = MediaPlayer::HaveNothing;
        else if (state == GST_STATE_PAUSED) {
            m_readyState = MediaPlayer::HaveEnoughData;
            m_paused = true;
        } else if (state == GST_STATE_PLAYING)
            m_paused = false;

        if (!m_paused && m_playbackRate)
            changePipelineState(GST_STATE_PLAYING);

        m_networkState = MediaPlayer::Loading;
        break;
    default:
        LOG_MEDIA_MESSAGE("Else : %d", getStateResult);
        break;
    }

    m_requestedState = GST_STATE_VOID_PENDING;

    if (shouldUpdatePlaybackState)
        m_player->playbackStateChanged();

    if (m_networkState != oldNetworkState) {
        LOG_MEDIA_MESSAGE("Network State Changed from %u to %u", oldNetworkState, m_networkState);
        m_player->networkStateChanged();
    }
    if (m_readyState != oldReadyState) {
        LOG_MEDIA_MESSAGE("Ready State Changed from %u to %u", oldReadyState, m_readyState);
        m_player->readyStateChanged();
    }

    if (getStateResult == GST_STATE_CHANGE_SUCCESS && state >= GST_STATE_PAUSED) {
        updatePlaybackRate();
        if (m_seekIsPending) {
            printf("### %s: Committing pending seek to %f\n", __PRETTY_FUNCTION__, m_seekTime); fflush(stdout);
            LOG_MEDIA_MESSAGE("[Seek] committing pending seek to %f", m_seekTime);
            m_seekIsPending = false;
            m_seeking = doSeek(toGstClockTime(m_seekTime), m_player->rate(), static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE));
            if (!m_seeking)
                LOG_MEDIA_MESSAGE("[Seek] seeking to %f failed", m_seekTime);
        }
    }
}

#if ENABLE(MEDIA_SOURCE)
GRefPtr<GstCaps> MediaPlayerPrivateGStreamer::currentDemuxerCaps() const
{
    GRefPtr<GstCaps> result;
    if (m_mediaSource && WEBKIT_IS_MEDIA_SRC(m_source.get())) {
        // METRO FIXME: Select the current demuxer pad (how to know?) instead of the first one
        GstPad* demuxersrcpad = webkit_media_src_get_video_pad(WEBKIT_MEDIA_SRC(m_source.get()), 0);

        if (demuxersrcpad) {
            result = adoptGRef(gst_pad_get_current_caps(demuxersrcpad));
        }
    }
    return result;
}
#endif

void MediaPlayerPrivateGStreamer::mediaLocationChanged(GstMessage* message)
{
    if (m_mediaLocations)
        gst_structure_free(m_mediaLocations);

    const GstStructure* structure = gst_message_get_structure(message);
    if (structure) {
        // This structure can contain:
        // - both a new-location string and embedded locations structure
        // - or only a new-location string.
        m_mediaLocations = gst_structure_copy(structure);
        const GValue* locations = gst_structure_get_value(m_mediaLocations, "locations");

        if (locations)
            m_mediaLocationCurrentIndex = static_cast<int>(gst_value_list_get_size(locations)) -1;

        loadNextLocation();
    }
}

bool MediaPlayerPrivateGStreamer::loadNextLocation()
{
    if (!m_mediaLocations)
        return false;

    const GValue* locations = gst_structure_get_value(m_mediaLocations, "locations");
    const gchar* newLocation = 0;

    if (!locations) {
        // Fallback on new-location string.
        newLocation = gst_structure_get_string(m_mediaLocations, "new-location");
        if (!newLocation)
            return false;
    }

    if (!newLocation) {
        if (m_mediaLocationCurrentIndex < 0) {
            m_mediaLocations = 0;
            return false;
        }

        const GValue* location = gst_value_list_get_value(locations,
                                                          m_mediaLocationCurrentIndex);
        const GstStructure* structure = gst_value_get_structure(location);

        if (!structure) {
            m_mediaLocationCurrentIndex--;
            return false;
        }

        newLocation = gst_structure_get_string(structure, "new-location");
    }

    if (newLocation) {
        // Found a candidate. new-location is not always an absolute url
        // though. We need to take the base of the current url and
        // append the value of new-location to it.
        KURL baseUrl = gst_uri_is_valid(newLocation) ? KURL() : m_url;
        KURL newUrl = KURL(baseUrl, newLocation);

        RefPtr<SecurityOrigin> securityOrigin = SecurityOrigin::create(m_url);
        if (securityOrigin->canRequest(newUrl)) {
            INFO_MEDIA_MESSAGE("New media url: %s", newUrl.string().utf8().data());

#if ENABLE(ENCRYPTED_MEDIA) || ENABLE(ENCRYPTED_MEDIA_V2)
            // Potentially unblock GStreamer thread for DRM license acquisition.
            m_drmKeySemaphore.signal ();
#endif
            // Reset player states.
            m_networkState = MediaPlayer::Loading;
            m_player->networkStateChanged();
            m_readyState = MediaPlayer::HaveNothing;
            m_player->readyStateChanged();

            // Reset pipeline state.
            m_resetPipeline = true;
            gst_element_set_state(m_playBin.get(), GST_STATE_READY);

            GstState state;
            gst_element_get_state(m_playBin.get(), &state, 0, 0);
            if (state <= GST_STATE_READY) {
                // Set the new uri and start playing.
                g_object_set(m_playBin.get(), "uri", newUrl.string().utf8().data(), NULL);
                m_url = newUrl;
                gst_element_set_state(m_playBin.get(), GST_STATE_PLAYING);
                return true;
            }
        } else
            INFO_MEDIA_MESSAGE("Not allowed to load new media location: %s", newUrl.string().utf8().data());
    }
    m_mediaLocationCurrentIndex--;
    return false;
}

void MediaPlayerPrivateGStreamer::loadStateChanged()
{
    updateStates();
}

void MediaPlayerPrivateGStreamer::timeChanged()
{
    updateStates();
    m_player->timeChanged();
}

void MediaPlayerPrivateGStreamer::didEnd()
{
    // Synchronize position and duration values to not confuse the
    // HTMLMediaElement. In some cases like reverse playback the
    // position is not always reported as 0 for instance.
    float now = currentTime();
    if (now > 0 && now <= duration() && m_mediaDuration != now) {
        m_mediaDurationKnown = true;
        m_mediaDuration = now;
        m_player->durationChanged();
    }

    m_isEndReached = true;
    timeChanged();

    if (!m_player->mediaPlayerClient()->mediaPlayerIsLooping()) {
        m_paused = true;
        gst_element_set_state(m_playBin.get(), GST_STATE_NULL);
        m_downloadFinished = false;
    }
}

void MediaPlayerPrivateGStreamer::cacheDuration()
{
    if (m_mediaDuration || !m_mediaDurationKnown)
        return;

    float newDuration = duration();
    if (std::isinf(newDuration)) {
        // Only pretend that duration is not available if the the query failed in a stable pipeline state.
        GstState state;
        if (gst_element_get_state(m_playBin.get(), &state, 0, 0) == GST_STATE_CHANGE_SUCCESS && state > GST_STATE_READY)
            m_mediaDurationKnown = false;
        return;
    }

    m_mediaDuration = newDuration;
}

static gboolean mediaPlayerPrivateNotifyDurationChanged(MediaPlayerPrivateGStreamer* instance)
{
    MediaPlayerPrivateGStreamer::notifyDurationChanged(instance);
    return G_SOURCE_REMOVE;
}

void MediaPlayerPrivateGStreamer::notifyDurationChanged(MediaPlayerPrivateGStreamer* instance)
{
    ASSERT(instance);
    instance->m_pendingAsyncOperationsLock.lock();
    ASSERT(instance->m_pendingAsyncOperations);
    instance->m_pendingAsyncOperations = g_list_remove(instance->m_pendingAsyncOperations, instance->m_pendingAsyncOperations->data);
    instance->m_pendingAsyncOperationsLock.unlock();

    instance->durationChanged();
}

void MediaPlayerPrivateGStreamer::durationChanged()
{
    float previousDuration = m_mediaDuration;

    // Force duration refresh.
    m_mediaDuration = 0;
    cacheDuration();

    // Avoid emiting durationchanged in the case where the previous
    // duration was 0 because that case is already handled by the
    // HTMLMediaElement.
    if (previousDuration && m_mediaDuration != previousDuration)
        m_player->durationChanged();
}

void MediaPlayerPrivateGStreamer::loadingFailed(MediaPlayer::NetworkState error)
{
    m_errorOccured = true;
    if (m_networkState != error) {
        m_networkState = error;
        m_player->networkStateChanged();
    }
    if (m_readyState != MediaPlayer::HaveNothing) {
        m_readyState = MediaPlayer::HaveNothing;
        m_player->readyStateChanged();
    }
}

static HashSet<String> mimeTypeCache()
{
    initializeGStreamerAndRegisterWebKitElements();

    DEFINE_STATIC_LOCAL(HashSet<String>, cache, ());
    static bool typeListInitialized = false;

    if (typeListInitialized)
        return cache;

    const char* mimeTypes[] = {
        "application/ogg",
        "application/vnd.apple.mpegurl",
        "application/vnd.rn-realmedia",
        "application/x-3gp",
        "application/x-pn-realaudio",
        "audio/3gpp",
        "audio/aac",
        "audio/flac",
        "audio/iLBC-sh",
        "audio/midi",
        "audio/mobile-xmf",
        "audio/mp1",
        "audio/mp2",
        "audio/mp3",
        "audio/mp4",
        "audio/mpeg",
        "audio/ogg",
        "audio/opus",
        "audio/qcelp",
        "audio/riff-midi",
        "audio/speex",
        "audio/wav",
        "audio/webm",
        "audio/x-ac3",
        "audio/x-aiff",
        "audio/x-amr-nb-sh",
        "audio/x-amr-wb-sh",
        "audio/x-au",
        "audio/x-ay",
        "audio/x-celt",
        "audio/x-dts",
        "audio/x-flac",
        "audio/x-gbs",
        "audio/x-gsm",
        "audio/x-gym",
        "audio/x-imelody",
        "audio/x-ircam",
        "audio/x-kss",
        "audio/x-m4a",
        "audio/x-mod",
        "audio/x-mp3",
        "audio/x-mpeg",
        "audio/x-musepack",
        "audio/x-nist",
        "audio/x-nsf",
        "audio/x-paris",
        "audio/x-sap",
        "audio/x-sbc",
        "audio/x-sds",
        "audio/x-shorten",
        "audio/x-sid",
        "audio/x-spc",
        "audio/x-speex",
        "audio/x-svx",
        "audio/x-ttafile",
        "audio/x-vgm",
        "audio/x-voc",
        "audio/x-vorbis+ogg",
        "audio/x-w64",
        "audio/x-wav",
        "audio/x-wavpack",
        "audio/x-wavpack-correction",
        "video/3gpp",
        "video/mj2",
        "video/mp4",
        "video/mpeg",
        "video/mpegts",
        "video/ogg",
        "video/quicktime",
        "video/vivo",
        "video/webm",
        "video/x-cdxa",
        "video/x-dirac",
        "video/x-dv",
        "video/x-fli",
        "video/x-flv",
        "video/x-h263",
        "video/x-ivf",
        "video/x-m4v",
        "video/x-matroska",
        "video/x-mng",
        "video/x-ms-asf",
        "video/x-msvideo",
        "video/x-mve",
        "video/x-nuv",
        "video/x-vcd"
    };

    for (unsigned i = 0; i < (sizeof(mimeTypes) / sizeof(*mimeTypes)); ++i)
        cache.add(String(mimeTypes[i]));

    typeListInitialized = true;
    return cache;
}

void MediaPlayerPrivateGStreamer::getSupportedTypes(HashSet<String>& types)
{
    types = mimeTypeCache();
}

bool MediaPlayerPrivateGStreamer::supportsKeySystem(const String& keySystem, const String& mimeType)
{
    GST_DEBUG ("Checking for KeySystem support with %s and type %s", keySystem.utf8().data(), mimeType.utf8().data());

#if USE(DXDRM)
    if (equalIgnoringCase(keySystem, "com.microsoft.playready") ||
        equalIgnoringCase(keySystem, "com.youtube.playready"))
        return true;
#endif

    if (equalIgnoringCase(keySystem, "org.w3.clearkey"))
        return true;

    return false;
}

#if ENABLE(ENCRYPTED_MEDIA_V2)
PassOwnPtr<CDMSession> MediaPlayerPrivateGStreamer::createSession(const String& keySystem)
{
    if (!supportsKeySystem(keySystem, emptyString()))
        return nullptr;

    return adoptPtr(new CDMSessionGStreamer(this));
}

void MediaPlayerPrivateGStreamer::needKey(RefPtr<Uint8Array> initData)
{
    bool handled = m_player->keyNeeded (initData.get());
    
    if (!handled) {
      GST_DEBUG ("no event handler for key needed, waking up GStreamer thread");
      m_drmKeySemaphore.signal ();
    }
}
#endif

#if ENABLE(ENCRYPTED_MEDIA) || ENABLE(ENCRYPTED_MEDIA_V2)

void MediaPlayerPrivateGStreamer::signalDRM ()
{
    GST_DEBUG ("key/license was changed or failed, signal semaphore");
    // Wake up a potential waiter blocked in the GStreamer thread
    m_drmKeySemaphore.signal ();
}

/* Called from main while the GStreamer thread is blocked */
void MediaPlayerPrivateGStreamer::needKeyEventFromMain(void* invocation)
{
    MainThreadNeedKeyCallbackInfo* info = static_cast<MainThreadNeedKeyCallbackInfo*>(invocation);
    /* Dispatch to the instance handler which will emit the need key event */
    if (info && info->handle) {
      info->handle->needKey(info->initData);
    }
}

MediaPlayer::SupportsType MediaPlayerPrivateGStreamer::extendedSupportsType(const String& type, const String& codecs, const String& keySystem, const KURL& url)
{

    // From: <http://dvcs.w3.org/hg/html-media/raw-file/eme-v0.1b/encrypted-media/encrypted-media.html#dom-canplaytype>
    // In addition to the steps in the current specification, this method must run the following steps:

    // 1. Check whether the Key System is supported with the specified container and codec type(s) by following the steps for the first matching condition from the following list:
    //    If keySystem is null, continue to the next step.
    if (keySystem.isNull() || keySystem.isEmpty())
        return supportsType(type, codecs, url);

    // If keySystem contains an unrecognized or unsupported Key System, return the empty string
    if (!supportsKeySystem(keySystem, emptyString()))
        return MediaPlayer::IsNotSupported;

    // If the Key System specified by keySystem does not support decrypting the container and/or codec specified in the rest of the type string.
    // (AVFoundation does not provide an API which would allow us to determine this, so this is a no-op)

    // 2. Return "maybe" or "probably" as appropriate per the existing specification of canPlayType().
    return supportsType(type, codecs, url);
}
#endif

MediaPlayer::SupportsType MediaPlayerPrivateGStreamer::supportsType(const String& type, const String& codecs, const KURL&)
{
    if (type.isNull() || type.isEmpty())
        return MediaPlayer::IsNotSupported;

    // Disable VPX/Opus on MSE for now, mp4/avc1 seems way more reliable currently.
    if (type.endsWith("webm"))
        return MediaPlayer::IsNotSupported;

    // spec says we should not return "probably" if the codecs string is empty
    if (mimeTypeCache().contains(type))
        return codecs.isEmpty() ? MediaPlayer::MayBeSupported : MediaPlayer::IsSupported;
    return MediaPlayer::IsNotSupported;
}

void MediaPlayerPrivateGStreamer::setDownloadBuffering()
{
    if (!m_playBin)
        return;

#if ENABLE(MEDIA_SOURCE)
    if (isMediaSource())
        return;
#endif

    unsigned flags;
    g_object_get(m_playBin.get(), "flags", &flags, NULL);

    unsigned flagDownload = getGstPlaysFlag("download");

    // We don't want to stop downloading if we already started it.
    if (flags & flagDownload && m_readyState > MediaPlayer::HaveNothing && !m_resetPipeline)
        return;

    bool shouldDownload = !isLiveStream() && m_preload == MediaPlayer::Auto;
    if (shouldDownload) {
        LOG_MEDIA_MESSAGE("Enabling on-disk buffering");
        g_object_set(m_playBin.get(), "flags", flags | flagDownload, NULL);
        m_fillTimer.startRepeating(0.2);
    } else {
        LOG_MEDIA_MESSAGE("Disabling on-disk buffering");
        g_object_set(m_playBin.get(), "flags", flags & ~flagDownload, NULL);
        m_fillTimer.stop();
    }
}

void MediaPlayerPrivateGStreamer::setPreload(MediaPlayer::Preload preload)
{
    if (preload == MediaPlayer::Auto && isLiveStream())
        return;

    m_preload = preload;
    setDownloadBuffering();

    if (m_delayingLoad && m_preload != MediaPlayer::None) {
        m_delayingLoad = false;
        commitLoad();
    }
}

void MediaPlayerPrivateGStreamer::createAudioSink()
{
    // Construct audio sink if pitch preserving is enabled.
    if (!m_preservesPitch)
        return;

    if (!m_playBin)
        return;

    GstElement* scale = gst_element_factory_make("scaletempo", 0);
    if (!scale) {
        GST_WARNING("Failed to create scaletempo");
        return;
    }

    GstElement* convert = gst_element_factory_make("audioconvert", 0);
    GstElement* resample = gst_element_factory_make("audioresample", 0);
    GstElement* sink = gst_element_factory_make("autoaudiosink", 0);

    m_autoAudioSink = sink;

    g_signal_connect(sink, "child-added", G_CALLBACK(setAudioStreamPropertiesCallback), this);

    GstElement* audioSink = gst_bin_new("audio-sink");
    gst_bin_add_many(GST_BIN(audioSink), scale, convert, resample, sink, NULL);

    if (!gst_element_link_many(scale, convert, resample, sink, NULL)) {
        GST_WARNING("Failed to link audio sink elements");
        gst_object_unref(audioSink);
        return;
    }

    GRefPtr<GstPad> pad = adoptGRef(gst_element_get_static_pad(scale, "sink"));
    gst_element_add_pad(audioSink, gst_ghost_pad_new("sink", pad.get()));

    g_object_set(m_playBin.get(), "audio-sink", audioSink, NULL);

    GRefPtr<GstElement> playsink = adoptGRef(gst_bin_get_by_name(GST_BIN(m_playBin.get()), "playsink"));
    if (playsink) {
        // The default value (0) means "send events to all the sinks", instead
        // of "only to the first that returns true". This is needed for MSE seek.
        g_object_set(G_OBJECT(playsink.get()), "send-event-mode", 0, NULL);
    }
}

void MediaPlayerPrivateGStreamer::createGSTPlayBin()
{
    ASSERT(!m_playBin);

    // gst_element_factory_make() returns a floating reference so
    // we should not adopt.
    m_playBin = gst_element_factory_make(gPlaybinName, "play");
    setStreamVolumeElement(GST_STREAM_VOLUME(m_playBin.get()));

    GRefPtr<GstBus> bus = webkitGstPipelineGetBus(GST_PIPELINE(m_playBin.get()));
    gst_bus_add_signal_watch(bus.get());
    g_signal_connect(bus.get(), "message", G_CALLBACK(mediaPlayerPrivateMessageCallback), this);
    gst_bus_enable_sync_message_emission (bus.get());
    g_signal_connect(bus.get(), "sync-message", G_CALLBACK(mediaPlayerPrivateSyncMessageCallback), this);

    unsigned flagNativeVideo = getGstPlaysFlag("native-video");
    unsigned flagSoftVolume = getGstPlaysFlag("soft-volume");
    unsigned flagAudio = getGstPlaysFlag("audio");
    unsigned flagVideo = getGstPlaysFlag("video");
    g_object_set(m_playBin.get(), "mute", m_player->muted(), "flags", flagNativeVideo | flagSoftVolume | flagAudio | flagVideo, NULL);

    g_signal_connect(m_playBin.get(), "notify::source", G_CALLBACK(mediaPlayerPrivateSourceChangedCallback), this);

    // If we load a MediaSource later, we will also listen the signals from
    // WebKitMediaSrc, which will be connected later in sourceChanged()
    // METRO FIXME: In that case, we shouldn't listen to these signals coming from playbin, or the callbacks will be called twice.
    g_signal_connect(m_playBin.get(), "video-changed", G_CALLBACK(mediaPlayerPrivateVideoChangedCallback), this);
    g_signal_connect(m_playBin.get(), "audio-changed", G_CALLBACK(mediaPlayerPrivateAudioChangedCallback), this);

#if ENABLE(VIDEO_TRACK)
    if (webkitGstCheckVersion(1, 1, 2)) {
        g_signal_connect(m_playBin.get(), "text-changed", G_CALLBACK(mediaPlayerPrivateTextChangedCallback), this);

        GstElement* textCombiner = webkitTextCombinerNew();
        ASSERT(textCombiner);
        g_object_set(m_playBin.get(), "text-stream-combiner", textCombiner, NULL);

        m_textAppSink = webkitTextSinkNew();
        ASSERT(m_textAppSink);

        m_textAppSinkPad = adoptGRef(gst_element_get_static_pad(m_textAppSink.get(), "sink"));
        ASSERT(m_textAppSinkPad);

        g_object_set(m_textAppSink.get(), "emit-signals", true, "enable-last-sample", false, "caps", gst_caps_new_empty_simple("text/vtt"), NULL);
        g_signal_connect(m_textAppSink.get(), "new-sample", G_CALLBACK(mediaPlayerPrivateNewTextSampleCallback), this);

        g_object_set(m_playBin.get(), "text-sink", m_textAppSink.get(), NULL);
    }
#endif

    GstElement* videoElement = createVideoSink(m_playBin.get());

    g_object_set(m_playBin.get(), "video-sink", videoElement, NULL);

    GRefPtr<GstPad> videoSinkPad = adoptGRef(gst_element_get_static_pad(m_webkitVideoSink.get(), "sink"));
    if (videoSinkPad)
        g_signal_connect(videoSinkPad.get(), "notify::caps", G_CALLBACK(mediaPlayerPrivateVideoSinkCapsChangedCallback), this);

    createAudioSink();
}

void MediaPlayerPrivateGStreamer::simulateAudioInterruption()
{
    GstMessage* message = gst_message_new_request_state(GST_OBJECT(m_playBin.get()), GST_STATE_PAUSED);
    gst_element_post_message(m_playBin.get(), message);
}

bool MediaPlayerPrivateGStreamer::didPassCORSAccessCheck() const
{
    if (WEBKIT_IS_WEB_SRC(m_source.get()))
        return webKitSrcPassedCORSAccessCheck(WEBKIT_WEB_SRC(m_source.get()));
    return false;
}

}

#endif // USE(GSTREAMER)
