/*
 * Copyright (C) 2011 Ericsson AB. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of Ericsson nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#if ENABLE(MEDIA_STREAM)

#include "CentralPipelineUnit.h"
#include "GstMediaStream.h"
#include "MediaStreamSourceGStreamer.h"
#include "PeerConnectionHandlerPrivateGStreamer.h"
#include <wtf/text/CString.h>
#include <wtf/text/StringBuilder.h>
#include <gst/audio/audio.h>
#include <stdio.h>

#define RTC_LOG(fmt, args...) printf(fmt "\n", ##args)
#define RTC_LOG_FENTER() printf("%s:%d::%s\n", __FILE__, __LINE__, __func__)
// #define RTC_LOG(fmt, args...) (void) 0
// #define RTC_LOG_FENTER() (void) 0
#define RTC_LOG_FLEAVE() (void) 0

namespace WebCore {

static String generateSourceExtensionIdAudio(const String& sourceId, const String& mediaType, const String& codec, const String& rate, const String& bitrate);
static String generateSourceExtensionIdVideo(const String& sourceId, const String& mediaType, const String& codec, const String& rate, const String& bitrate, const String& width, const String& height);


static unsigned int getNextRtpSessionId() {
    static unsigned int sid = 0;
    return sid++;
}

GstRtpStream::GstRtpStream(GstMediaStream* gstMediaStream, MediaType type, guint streamId)
    : m_type(type)
    , m_streamId(streamId)
    , m_gstMediaStream(gstMediaStream)
    , m_rtpSession(0)
    , m_localKey(0), m_localKeyLength(0), m_localSalt(0), m_localSaltLength(0), m_localMki(0), m_localMkiLength(0)
    , m_remoteKey(0), m_remoteKeyLength(0), m_remoteSalt(0), m_remoteSaltLength(0), m_remoteMki(0), m_remoteMkiLength(0)
    , m_rtpNiceSink(0), m_rtpNiceSinkPad(0), m_rtpBinSendRtpSrcPad(0), m_rtpNiceSinkQueue(0)
    , m_rtcpNiceSink(0), m_rtcpNiceSinkPad(0), m_rtpBinSendRtcpSrcPad(0), m_rtcpNiceSinkQueue(0)
    , m_bin(0), m_rtpSendSinkPad(0), m_rtpSendSinkGhostPad(0), m_rtpSendSinkGhostPadUnlinkedSignalHandler(0)
    , m_rtpRecvSrcPad(0), m_rtpRecvSrcGhostPad(0), m_rtpRecvSrcGhostPadUnlinkedSignalHandler(0)
    , m_rtpNiceSrc(0), m_rtpNiceSrcPad(0), m_rtpBinRecvRtpSinkPad(0), m_rtpNiceSrcQueue(0)
    , m_rtcpNiceSrc(0), m_rtcpNiceSrcPad(0), m_rtpBinRecvRtcpSinkPad(0), m_rtcpNiceSrcQueue(0)
    , m_negotiatedCodec(0), m_rtxpt(0), m_defaultRemotePort(0)
    , m_encoder(0), m_pay(0)
    , m_maxBandwidth(0), m_framesizeWidth(0), m_framesizeHeight(0)
{
    m_rtpSession = getNextRtpSessionId();
    //m_rtpSession = streamId;
};

void GstRtpStream::setLocalCryptoContext(unsigned char* key, unsigned int keyLength,
                                         unsigned char* salt, unsigned int saltLength,
                                         unsigned char* mki, unsigned int mkiLength)
{
    m_localKey = (unsigned char* )malloc(keyLength);
    memcpy(m_localKey, key, keyLength);
    m_localKeyLength = keyLength;

    m_localSalt = (unsigned char* )malloc(saltLength);
    memcpy(m_localSalt, salt, saltLength);
    m_localSaltLength = saltLength;

    if (mki == NULL || mkiLength == 0) {
        m_localMki = NULL;
        m_localMkiLength = 0;
    } else {
        m_localMki = (unsigned char* )malloc(mkiLength);
        memcpy(m_localMki, mki, mkiLength);
        m_localMkiLength = mkiLength;
    }
}

void GstRtpStream::setRemoteCryptoContext(unsigned char* key, unsigned int keyLength,
                                         unsigned char* salt, unsigned int saltLength,
                                         unsigned char* mki, unsigned int mkiLength)
{
    m_remoteKey = (unsigned char* )malloc(keyLength);
    memcpy(m_remoteKey, key, keyLength);
    m_remoteKeyLength = keyLength;

    m_remoteSalt = (unsigned char* )malloc(saltLength);
    memcpy(m_remoteSalt, salt, saltLength);
    m_remoteSaltLength = saltLength;

    if (mki == NULL || mkiLength == 0) {
        m_remoteMki = NULL;
        m_remoteMkiLength = 0;
    } else {
        m_remoteMki = (unsigned char* )malloc(mkiLength);
        memcpy(m_remoteMki, mki, mkiLength);
        m_remoteMkiLength = mkiLength;
    }
}


void GstRtpStream::getLocalCryptoContext(unsigned char** key, unsigned int* keyLength,
                                         unsigned char** salt, unsigned int* saltLength,
                                         unsigned char** mki, unsigned int* mkiLength)
{
    *key = m_localKey;
    *keyLength = m_localKeyLength;
    *salt = m_localSalt;
    *saltLength = m_localSaltLength;
    *mki = m_localMki;
    *mkiLength = m_localMkiLength;
}

void GstRtpStream::getRemoteCryptoContext(unsigned char** key, unsigned int* keyLength,
                                         unsigned char** salt, unsigned int* saltLength,
                                         unsigned char** mki, unsigned int* mkiLength)
{
    *key = m_remoteKey;
    *keyLength = m_remoteKeyLength;
    *salt = m_remoteSalt;
    *saltLength = m_remoteSaltLength;
    *mki = m_remoteMki;
    *mkiLength = m_remoteMkiLength;
}


void GstRtpStream::constructGStreamerSource()
{
    RTC_LOG_FENTER();
    if (!m_negotiatedCodec) {
        RTC_LOG("No codecs negotiated. Could not create Source element");
        return;
    }

    if (m_type == video) {
        createVideoSourceBin();
    } else if (m_type == audio) {
        createAudioSourceBin();
    }
    RTC_LOG_FLEAVE();
}

void GstRtpStream::createSinkBinWithoutEncoder()
{
    if (m_type == video) {
        createVideoSinkBinWithoutEncoder();
    } else if (m_type == audio) {
        createAudioSinkBinWithoutEncoder();
    }
}

void GstRtpStream::createSinkBinWithEncoder()
{
    if (m_type == video) {
        createVideoSinkBinWithEncoder();
    } else if (m_type == audio) {
        createAudioSinkBinWithEncoder();
    }
}

GstElement* GstRtpStream::createEncodingSourceExtension()
{
    if (m_type == video) {
        return createVideoEncodingSourceExtension();
    } else if (m_type == audio) {
        return createAudioEncodingSourceExtension();
    }
    return 0;
}

void GstRtpStream::createAudioSinkBinWithoutEncoder()
{
    RTC_LOG_FENTER();
    GstElement *pay = m_negotiatedCodec->createRtpPacketizerElement();
    m_bin = pay;

    //FIXME: m_encoderSourceId should be generated somewhere else
    String bitrate = m_options.attribute("bitrate");
    m_encoderSourceId = generateSourceExtensionIdAudio(m_baseSourceId,
                                                       "audio",
                                                       m_negotiatedCodec->name(),
                                                       bitrate,
                                                       String::number(m_negotiatedCodec->rate()));
    RTC_LOG_FLEAVE();
}

void GstRtpStream::createVideoSinkBinWithoutEncoder()
{
    RTC_LOG_FENTER();
    GstElement *pay = m_negotiatedCodec->createRtpPacketizerElement();
    m_bin = pay;
    String bitrate = m_options.attribute("bitrate");
    String width = m_options.attribute("width");
    String height = m_options.attribute("height");
    m_encoderSourceId = generateSourceExtensionIdVideo(m_baseSourceId,
                                                       "video",
                                                       m_negotiatedCodec->name(),
                                                       String::number(m_negotiatedCodec->rate()),
                                                       bitrate,
                                                       width,
                                                       height);
    RTC_LOG_FLEAVE();
}

void GstRtpStream::createAudioSinkBinWithEncoder()
{
    RTC_LOG_FENTER();
    ASSERT(m_negotiatedCodec);

    GstElement* encoder = m_encoder;
    GstElement* pay = m_pay;
    if (!encoder || !pay) {
        RTC_LOG("Failed to create encoder or payload packitizer element");
        return;
    }

    bool wasInt1 = false;
    String bitrate = m_options.attribute("bitrate");
    int iBitrate = bitrate.toUInt(&wasInt1);
    if (!wasInt1) iBitrate = -1;

    m_bin = gst_bin_new("PeerConnection_AudioSinkBin");


    GstCaps *rescaps = gst_caps_new_simple("audio/x-raw",
                                        "rate", G_TYPE_INT, m_negotiatedCodec->rate(),
                                           "format", G_TYPE_STRING, gst_audio_format_to_string(GST_AUDIO_FORMAT_S16LE),
                                        "channels", G_TYPE_INT, m_negotiatedCodec->channels(),
                                           "layout", G_TYPE_STRING, "interleaved",
                                        NULL);
    GstElement *capsfilt_res = gst_element_factory_make("capsfilter","capsfilt_res");
    g_object_set(capsfilt_res,"caps",rescaps,NULL);

    if (iBitrate != -1) {
        RTC_LOG("Using bitrate %d", iBitrate);
        g_object_set(encoder, "bitrate", iBitrate, NULL);
    }

    GstElement *convert = gst_element_factory_make("audioconvert", "audioconvert");
    GstElement *resample = gst_element_factory_make("audioresample", "audioresample");

    gst_bin_add_many(GST_BIN(m_bin), convert, resample, capsfilt_res, encoder, pay, NULL);

    gboolean res = gst_element_link_many(convert, resample, capsfilt_res, NULL);
    if (!res) RTC_LOG("link failed");
    ASSERT(res);
    res = gst_element_link_many(encoder, pay, NULL);
    if (!res) RTC_LOG("link failed");
    ASSERT(res);
    res = gst_element_link_filtered(capsfilt_res, encoder, rescaps);
    if (!res) RTC_LOG("link failed");
    ASSERT(res);

    GstPad *sinkPad = gst_element_get_static_pad(convert, "sink");
    GstPad *ghostSinkPad = gst_ghost_pad_new("sink", sinkPad);
    gst_pad_set_active(ghostSinkPad, TRUE);
    gst_element_add_pad(m_bin, ghostSinkPad);
    gst_object_unref(sinkPad);

    GstPad *srcPad = gst_element_get_static_pad(pay, "src");
    GstPad* ghostSrcPad = gst_ghost_pad_new("src", srcPad);
    gst_pad_set_active(ghostSrcPad, TRUE);
    gst_element_add_pad(m_bin, ghostSrcPad);
    gst_object_unref(srcPad);

    RTC_LOG_FLEAVE();
}

void GstRtpStream::createVideoSinkBinWithEncoder()
{
    RTC_LOG_FENTER();
    ASSERT(m_negotiatedCodec);

    GstElement* encoder = this->m_encoder;
    GstElement* pay = this->m_pay;
    if (!encoder || !pay) {
        RTC_LOG("Failed to create encoder or payload packitizer element");
        return;
    }

    int iBitrate = m_maxBandwidth!=0?m_maxBandwidth/1000:-1; // bitrate in kb / sec.
    int iWidth = m_framesizeWidth!=0?m_framesizeWidth:-1;
    int iHeight = m_framesizeHeight!=0?m_framesizeHeight:-1;

    RTC_LOG("width = %d, height = %d", iWidth, iHeight);

    m_bin = gst_bin_new("PeerConnection_VideoSinkBin");

    GstElement *colorspace = gst_element_factory_make("videoconvert", 0);
    GstElement *scale = gst_element_factory_make("videobox", "scale");
    g_object_set(scale, "autocrop", true, NULL);
    gst_bin_add_many(GST_BIN(m_bin), colorspace, scale, encoder, pay, NULL);
    // GstCaps* videocaps = gst_caps_new_empty_simple("video/x-raw-yuv");

    // if (iWidth != -1 && iHeight != -1) {
    //     RTC_LOG("Using width %d and height %d", iWidth, iHeight);
    //     gst_caps_set_simple(videocaps, "width", G_TYPE_INT, iWidth, "height", G_TYPE_INT, iHeight, NULL);
    // }

    if (iBitrate != -1) {
        RTC_LOG("Using bitrate %d", iBitrate);
        g_object_set(encoder, "bitrate", iBitrate, NULL);
    }
    bool linked;
    linked = gst_element_link(colorspace, scale);
    ASSERT(linked);
    if (!linked) {
        RTC_LOG("createVideoSinkBinWithEncoder link failed");
        m_bin = NULL;
        return;
    }

    linked = gst_element_link(scale, encoder /*, videocaps*/);
    ASSERT(linked);
    if (!linked) {
        RTC_LOG("createVideoSinkBinWithEncoder link failed");
        m_bin = NULL;
        return;
    }
    linked = gst_element_link(encoder, pay);
    ASSERT(linked);
    if (!linked) {
        RTC_LOG("createVideoSinkBinWithEncoder link failed");
        m_bin = NULL;
        return;
    }

    GstPad *sinkPad = gst_element_get_static_pad(colorspace, "sink");
    GstPad *ghostSinkPad = gst_ghost_pad_new("sink", sinkPad);
    gst_pad_set_active(ghostSinkPad, TRUE);
    gst_element_add_pad(m_bin, ghostSinkPad);

    GstPad *srcPad = gst_element_get_static_pad(pay, "src");
    GstPad* ghostSrcPad = gst_ghost_pad_new("src", srcPad);
    gst_pad_set_active(ghostSrcPad, TRUE);
    gst_element_add_pad(m_bin, ghostSrcPad);
    gst_object_unref(srcPad);

    RTC_LOG_FLEAVE();
}


GstElement* GstRtpStream::createAudioEncodingSourceExtension()
{
    RTC_LOG_FENTER();
    ASSERT(m_negotiatedCodec);

    GstElement* bin = gst_bin_new("PeerConnection_AudioEncodingSourceExtension");

    GstElement* encoder = m_encoder;
    GstElement* pay = m_pay;
    if (!encoder || !pay) {
        RTC_LOG("Failed to create encoder or payload packitizer element");
        return 0;
    }

    bool wasInt1 = false;
    String bitrate = m_options.attribute("bitrate");
    int iBitrate = bitrate.toUInt(&wasInt1);
    if (!wasInt1) iBitrate = -1;

    GstElement *convert = gst_element_factory_make("audioconvert", "audioconvert");
    GstElement *resample = gst_element_factory_make("audioresample", "audioresample");

    GstCaps *rescaps = gst_caps_new_simple("audio/x-raw-int",
                                            "rate", G_TYPE_INT, m_negotiatedCodec->rate(),
                                            "width", G_TYPE_INT, 16,
                                            "depth", G_TYPE_INT, 16,
                                            "channels", G_TYPE_INT, m_negotiatedCodec->channels(),
                                            "signed", G_TYPE_BOOLEAN, TRUE,
                                            "endianness", G_TYPE_INT, G_BYTE_ORDER,
                                            NULL);
    GstElement *capsfilt_res = gst_element_factory_make("capsfilter", "capsfilt_res");
    g_object_set(capsfilt_res, "caps", rescaps, NULL);

    if (iBitrate != -1) {
        RTC_LOG("Using bitrate %d", iBitrate);
        g_object_set(encoder, "bitrate", iBitrate, NULL);
    }

    gst_bin_add_many(GST_BIN(bin), convert, resample, capsfilt_res, encoder, NULL);
    gst_element_link_many(convert, resample, capsfilt_res, NULL);

    gst_element_link_filtered(capsfilt_res, encoder, rescaps);

    GstPad *sinkPad = gst_element_get_static_pad(convert, "sink");
    GstPad *ghostSinkPad = gst_ghost_pad_new("sink", sinkPad);
    gst_pad_set_active(ghostSinkPad, TRUE);
    gst_element_add_pad(bin, ghostSinkPad);
    gst_object_unref(sinkPad);

    GstPad *srcPad = gst_element_get_static_pad(encoder, "src");
    GstPad* ghostSrcPad = gst_ghost_pad_new("src", srcPad);
    gst_pad_set_active(ghostSrcPad, TRUE);
    gst_element_add_pad(bin, ghostSrcPad);
    gst_object_unref(srcPad);

    RTC_LOG_FLEAVE();
    return bin;
}

GstElement* GstRtpStream::createVideoEncodingSourceExtension()
{
    RTC_LOG_FENTER();
    GstElement* bin = gst_bin_new("PeerConnection_VideoEncodingSourceExtension");

    GstElement* encoder = m_encoder;
    GstElement* pay = m_pay;
    if (!encoder || !pay) {
        RTC_LOG("Failed to create encoder or payload packitizer element");
        return 0;
    }

    bool wasInt1 = false;
    bool wasInt2 = false;

    String bitrate = m_options.attribute("bitrate");
    int iBitrate = bitrate.toUInt(&wasInt1);
    if (!wasInt1) iBitrate = -1;

    String width = m_options.attribute("width");
    String height = m_options.attribute("height");
    int iWidth = width.toUInt(&wasInt1);
    int iHeight = height.toUInt(&wasInt2);
    if (!wasInt1 || !wasInt2) {
        iWidth = -1;
        iHeight = -1;
    }

    GstElement *colorspace = gst_element_factory_make("ffmpegcolorspace", "colorspace");
    GstElement *scale = gst_element_factory_make("videobox", "scale");
    g_object_set(scale,"autocrop", true, NULL);
    gst_bin_add_many(GST_BIN(bin), colorspace, scale, encoder, NULL);

    GstCaps* videocaps = gst_caps_new_empty_simple("video/x-raw-yuv");
    if (iWidth != -1 && iHeight != -1) {
        RTC_LOG("Using width %d and height %d", iWidth, iHeight);
        gst_caps_set_simple(videocaps, "width", G_TYPE_INT, iWidth, "height", G_TYPE_INT, iHeight, NULL);
    }
    if (iBitrate != -1) {
        RTC_LOG("Using bitrate %d", iBitrate);
        g_object_set(encoder, "bitrate", iBitrate, NULL);
    }

    bool linked;
    linked = gst_element_link(colorspace, scale);
    if (!linked) RTC_LOG("link failed");
    ASSERT(linked);
    linked = gst_element_link_filtered(scale, encoder, videocaps);
    if (!linked) RTC_LOG("link failed");
    ASSERT(linked);

    GstPad *sinkPad = gst_element_get_static_pad(colorspace, "sink");
    GstPad *ghostSinkPad = gst_ghost_pad_new("sink", sinkPad);
    gst_pad_set_active(ghostSinkPad, TRUE);
    gst_element_add_pad(bin, ghostSinkPad);

    GstPad *srcPad = gst_element_get_static_pad(encoder, "src");
    GstPad* ghostSrcPad = gst_ghost_pad_new("src", srcPad);
    gst_pad_set_active(ghostSrcPad, TRUE);
    gst_element_add_pad(bin, ghostSrcPad);

    RTC_LOG_FLEAVE();
    return bin;
}

void GstRtpStream::createAudioSourceBin()
{
    RTC_LOG_FENTER();
    ASSERT(m_negotiatedCodec);

    m_bin = gst_bin_new("PeerConnection_AudioSourceDepayAndDecodeBin");

    GstElement *depay = m_negotiatedCodec->createRtpDepacketizerElement();
    GstElement *decoder = m_negotiatedCodec->createDecoderElement();

    if (depay && decoder) {
        gst_bin_add_many(GST_BIN(m_bin), depay, decoder, NULL);

        GstPad *depaySinkPad = gst_element_get_static_pad(depay, "sink");

        gst_element_link(depay, decoder);

        GstPad *ghostSinkPad = gst_ghost_pad_new("sink", depaySinkPad);
        gst_pad_set_active(ghostSinkPad, TRUE);
        gst_element_add_pad(m_bin, ghostSinkPad);
        gst_object_unref(depaySinkPad);

        GstPad *srcPad = gst_element_get_static_pad(decoder, "src");
        GstPad *ghostSrcPad = gst_ghost_pad_new("src", srcPad);
        gst_pad_set_active(ghostSrcPad, TRUE);
        gst_element_add_pad(m_bin, ghostSrcPad);
        gst_object_unref(srcPad);
    }
    RTC_LOG_FLEAVE();
}

void GstRtpStream::createVideoSourceBin()
{
    RTC_LOG_FENTER();
    ASSERT(m_negotiatedCodec);

    m_bin = gst_bin_new("PeerConnection_VideoSourceDepayAndDecodeBin");

    GstElement *depay = m_negotiatedCodec->createRtpDepacketizerElement();
    GstElement *decoder = m_negotiatedCodec->createDecoderElement();
    GstCaps    *caps = 0;

    caps = gst_caps_new_simple ("application/x-rtp",
                                 "media", G_TYPE_STRING, "video",
                                 "clock-rate", G_TYPE_INT, m_negotiatedCodec->rate(),
                                 "payload", G_TYPE_INT, m_negotiatedCodec->payloadTypeNumber(),
                                 NULL);

    if (depay && decoder) {
        bool linked = false;
        GstElement* q1 = gst_element_factory_make("queue", NULL);
        gst_bin_add_many(GST_BIN(m_bin), q1, depay, decoder, NULL);
        GstPad* q1SinkPad = gst_element_get_static_pad(q1, "sink");

        GstPad *depaySinkPad = gst_element_get_static_pad(depay, "sink");
        gst_pad_set_caps(depaySinkPad, caps);
        gst_caps_unref(caps);

        linked = gst_element_link_pads(q1, "src", depay, "sink");
        ASSERT(linked);
        if (!linked) {
            RTC_LOG("Video source bin depay link failed");
            m_bin = NULL;
            return;
        }
        linked = gst_element_link_pads(depay, "src", decoder, "sink");
        ASSERT(linked);
        if (!linked) {
            RTC_LOG("Video source bin decoder link failed");
            m_bin = NULL;
            return;
        }

        GstPad* ghostSinkPad = gst_ghost_pad_new("sink", q1SinkPad);
        gst_pad_set_active(ghostSinkPad, TRUE);
        gst_element_add_pad(m_bin, ghostSinkPad);
        gst_object_unref(q1SinkPad);
        gst_object_unref(depaySinkPad);

        GstPad *srcPad = gst_element_get_static_pad(decoder, "src");
        GstPad *ghostSrcPad = gst_ghost_pad_new("src", srcPad);
        gst_pad_set_active(ghostSrcPad, TRUE);
        gst_element_add_pad(m_bin, ghostSrcPad);
        gst_object_unref(srcPad);

        RTC_LOG("Video source bin created successfully");
    }
    RTC_LOG_FLEAVE();
}

//FIXME: remove mediaType parameter
static String generateSourceExtensionIdAudio(const String& sourceId, const String& mediaType, const String& codec, const String& rate, const String& bitrate) {
    StringBuilder builder;
    builder.append(sourceId);
    builder.append(":");
    builder.append(mediaType);
    builder.append(":");
    builder.append(codec);
    builder.append(":");
    builder.append(rate);
    builder.append(":");
    builder.append(bitrate);
    return builder.toString();
}


//FIXME: remove mediaType parameter
static String generateSourceExtensionIdVideo(const String& sourceId, const String& mediaType, const String& codec, const String& rate, const String& bitrate, const String& width, const String& height) {
    StringBuilder builder;
    builder.append(sourceId);
    builder.append(":");
    builder.append(mediaType);
    builder.append(":");
    builder.append(codec);
    builder.append(":");
    builder.append(rate);
    builder.append(":");
    builder.append(bitrate);
    builder.append(":");
    builder.append(width);
    builder.append(":");
    builder.append(height);
    return builder.toString();
}

GstRtpStream* GstMediaStream::addRtpStream(GstRtpStream::MediaType type, guint streamId)
{
    GstRtpStream* ns = new GstRtpStream(this, type, streamId);
    m_gstRtpStreams.append(ns);
    return ns;
}

void GstMediaStream::createPrivateStream(PeerConnectionHandlerPrivateGStreamer* peerConnectionHandler)
{
    Vector<RefPtr<MediaStreamTrackPrivate> > audioPrivateTracks;
    Vector<RefPtr<MediaStreamTrackPrivate> > videoPrivateTracks;
    streamPrivate = MediaStreamPrivate::create(audioPrivateTracks, videoPrivateTracks);
    g_printerr("GstMediaStream::createPrivateStream %s\n", streamPrivate->id().utf8().data());

    Vector<GstRtpStream*>::iterator gstRtpStreamsIt = m_gstRtpStreams.begin();
    while (gstRtpStreamsIt != m_gstRtpStreams.end()) {
        GstRtpStream* gstRtpStream = *gstRtpStreamsIt;
        gstRtpStreamsIt++;
        MediaStreamSource::Type sourceType;

        switch (gstRtpStream->type()) {
        case GstRtpStream::audio:
            sourceType = MediaStreamSource::Audio;
            break;
        case GstRtpStream::video:
            sourceType = MediaStreamSource::Video;
            break;
        }

        peerConnectionHandler->prepareTransportSource(gstRtpStream);
        GstElement* transportBin = peerConnectionHandler->transportBin();
        gboolean linked = gst_element_link(transportBin, gstRtpStream->m_bin);
        g_printerr("transport bin linked to depayloader: %d\n", linked);

        String deviceId = ""; // FIXME
        RefPtr<MediaStreamSource> source = adoptRef(new MediaStreamSourceGStreamer(m_type, deviceId, "", sourceType, "", "", gstRtpStream->m_bin));
        streamPrivate->addTrack(MediaStreamTrackPrivate::create(source));
    }
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)
