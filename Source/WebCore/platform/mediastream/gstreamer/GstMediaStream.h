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

#ifndef GstMediaStream_h
#define GstMediaStream_h

#if ENABLE(MEDIA_STREAM)

#include "Codec.h"
#include "IceAgent.h"
#include "MediaStreamPrivate.h"
#include <gst/gst.h>
#include <wtf/text/WTFString.h>

namespace WebCore
{

class GstMediaStream;
class PeerConnectionHandlerPrivateGStreamer;

// FIXME:  All Gst* refcounted
class GstRtpStream
{
    friend class GstMediaStream;
public:
    typedef enum {
        audio,
        video
    } MediaType;

private:
    GstRtpStream(GstMediaStream* gstMediaStream, MediaType type, guint streamId);
public:
    MediaType type() const {return m_type;}

    guint streamId() const {return m_streamId;}

    CodecOptions& options() {return m_options;}

    IceAgent::CandidateVector& rtpCandidates() {return m_rtpCandidates;}
    IceAgent::CandidateVector& rtcpCandidates() {return m_rtcpCandidates;}

    GstMediaStream* gstMediaStream() const {return m_gstMediaStream;}

    guint rtpSessionId() const {return m_rtpSession;}

    void constructGStreamerSource();

    void createSinkBinWithoutEncoder();
    void createSinkBinWithEncoder();
    GstElement* createEncodingSourceExtension();

    void setMaxBandwidth(unsigned int maxBandwidth) { m_maxBandwidth = maxBandwidth; }
    unsigned int maxBandwidth() const {return m_maxBandwidth; }

    void setFramesizeWidth(unsigned int width) { m_framesizeWidth = width; }
    void setFramesizeHeight(unsigned int height) { m_framesizeHeight = height; }
    unsigned int framesizeWidth() const { return m_framesizeWidth; }
    unsigned int framesizeHeight() const { return m_framesizeHeight; }

    void setLocalCryptoContext(unsigned char* key, unsigned int keyLength,
                               unsigned char* salt, unsigned int saltLength,
                               unsigned char* mki, unsigned int emkiLength);
    void getLocalCryptoContext(unsigned char** key, unsigned int* keyLength,
                               unsigned char** salt, unsigned int* saltLength,
                               unsigned char** mki, unsigned int* mkiLength);

    void setRemoteCryptoContext(unsigned char* key, unsigned int keyLength,
                               unsigned char* salt, unsigned int saltLength,
                               unsigned char* mki, unsigned int emkiLength);
    void getRemoteCryptoContext(unsigned char** key, unsigned int* keyLength,
                               unsigned char** salt, unsigned int* saltLength,
                               unsigned char** mki, unsigned int* mkiLength);


private:
    void createAudioSourceBin();
    void createVideoSourceBin();

    void createAudioSinkBinWithoutEncoder();
    void createVideoSinkBinWithoutEncoder();

    void createAudioSinkBinWithEncoder();
    void createVideoSinkBinWithEncoder();

    GstElement* createAudioEncodingSourceExtension();
    GstElement* createVideoEncodingSourceExtension();

private:
    MediaType m_type;
    guint m_streamId;
    CodecOptions m_options;
    IceAgent::CandidateVector m_rtpCandidates;
    IceAgent::CandidateVector m_rtcpCandidates;
    GstMediaStream* m_gstMediaStream;
    guint m_rtpSession;

    unsigned char* m_localKey;
    unsigned int m_localKeyLength;
    unsigned char* m_localSalt;
    unsigned int m_localSaltLength;
    unsigned char* m_localMki;
    unsigned int m_localMkiLength;

    unsigned char* m_remoteKey;
    unsigned int m_remoteKeyLength;
    unsigned char* m_remoteSalt;
    unsigned int m_remoteSaltLength;
    unsigned char* m_remoteMki;
    unsigned int m_remoteMkiLength;

    //FIXME: make private
public:

    GstElement* m_rtpNiceSink;
    GstPad* m_rtpNiceSinkPad;
    GstPad* m_rtpBinSendRtpSrcPad;
    GstElement* m_rtpNiceSinkQueue;

    GstElement* m_rtcpNiceSink;
    GstPad* m_rtcpNiceSinkPad;
    GstPad* m_rtpBinSendRtcpSrcPad;
    GstElement* m_rtcpNiceSinkQueue;

    GstElement* m_bin;
    GstPad* m_rtpSendSinkPad;
    GstPad* m_rtpSendSinkGhostPad;
    gulong m_rtpSendSinkGhostPadUnlinkedSignalHandler;
    GstPad* m_rtpRecvSrcPad;
    GstPad* m_rtpRecvSrcGhostPad;
    gulong m_rtpRecvSrcGhostPadUnlinkedSignalHandler;

    GstElement* m_rtpNiceSrc;
    GstPad* m_rtpNiceSrcPad;
    GstPad* m_rtpBinRecvRtpSinkPad;
    GstElement* m_rtpNiceSrcQueue;

    GstElement* m_rtcpNiceSrc;
    GstPad* m_rtcpNiceSrcPad;
    GstPad* m_rtpBinRecvRtcpSinkPad;
    GstElement* m_rtcpNiceSrcQueue;

    String m_baseSourceId;
    String m_encoderSourceId;
    String m_packetizerSourceId;
    Codec* m_negotiatedCodec;
    String m_username;
    String m_password;
    guint m_rtxpt;

    String m_defaultLocalFoundation;
    String m_defaultRemoteFoundation;
    String m_defaultRemoteAddress;
    guint m_defaultRemotePort;

    GstElement* m_encoder;
    GstElement* m_pay;

    unsigned int m_maxBandwidth;
    unsigned int m_framesizeWidth;
    unsigned int m_framesizeHeight;
};


class GstMediaStream
{
public:
    typedef enum {Local, Remote} StreamType;

    typedef enum {
        GatheringIdle,
        GatheringOffer,
        GatheringAnswer
    } GatheringState;

    typedef enum {
        Idle,
        OfferSent,
        UpdateSent,
    } NegotiationState;

    GstMediaStream(StreamType type, String label)
        : m_type(type)
        , m_label(label)
        , m_closed(false)
        , m_numberOfRecevingStreams(0)
        , streamPrivate(0)
        , m_gatheringState(GatheringIdle)
        , m_negotiationState(Idle)
        {};
    ~GstMediaStream() {};

    StreamType type() const {return m_type;}
    String label() const {return m_label;}

    bool closed() const {return m_closed;}
    void setClosed() {m_closed = true;}

    GstRtpStream* addRtpStream(GstRtpStream::MediaType type, guint streamId);
    Vector<GstRtpStream*>& rtpStreams() {return m_gstRtpStreams;}

    void createPrivateStream(PeerConnectionHandlerPrivateGStreamer*);

private:
    StreamType m_type;
    String m_label;
    bool m_closed;
//FIXME: make private
public:
    guint m_numberOfRecevingStreams;
    RefPtr<MediaStreamPrivate> streamPrivate;
    GatheringState m_gatheringState;
    NegotiationState m_negotiationState;
private:
    Vector<GstRtpStream*> m_gstRtpStreams;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

#endif // GstMediaStream_h
