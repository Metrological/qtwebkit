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

#ifndef PeerConnectionHandlerPrivateGStreamer_h
#define PeerConnectionHandlerPrivateGStreamer_h

#if ENABLE(MEDIA_STREAM)

#include "CallbackProxy.h"
#include "Codec.h"
#include "GstMediaStream.h"
#include "IceAgent.h"
#include "PeerConnectionHandlerConfiguration.h"
#include "RTCPeerConnectionHandler.h"
#include <gst/gst.h>
#include <wtf/PassOwnPtr.h>
#include <wtf/text/WTFString.h>
#include <wtf/HashMap.h>
#include <wtf/Vector.h>

#define SRTP_MASTER_LEN      30
#define SRTP_MASTERKEY_LEN   16
#define SRTP_MASTERSALT_LEN  ((SRTP_MASTER_LEN) - (SRTP_MASTERKEY_LEN))

namespace WebCore {

class Candidate;
class MediaDescription;
class SignalingMessage;

class PeerConnectionHandlerPrivateGStreamer : public RTCPeerConnectionHandler, public IceAgentClient, public CallbackProxyClient {

public:
    PeerConnectionHandlerPrivateGStreamer(RTCPeerConnectionHandlerClient*);
    ~PeerConnectionHandlerPrivateGStreamer();

    // RTCPeerConnectionHandler implementation.
    bool initialize(PassRefPtr<RTCConfigurationPrivate>);
    void createOffer(PassRefPtr<RTCSessionDescriptionRequest>, PassRefPtr<RTCOfferOptionsPrivate>);
    void createAnswer(PassRefPtr<RTCSessionDescriptionRequest>, PassRefPtr<RTCOfferAnswerOptionsPrivate>);
    void setLocalDescription(PassRefPtr<RTCVoidRequest>, PassRefPtr<RTCSessionDescriptionDescriptor>);
    void setRemoteDescription(PassRefPtr<RTCVoidRequest>, PassRefPtr<RTCSessionDescriptionDescriptor>);
    PassRefPtr<RTCSessionDescriptionDescriptor> localDescription();
    PassRefPtr<RTCSessionDescriptionDescriptor> remoteDescription();
    bool updateIce(PassRefPtr<RTCConfigurationPrivate>);
    bool addIceCandidate(PassRefPtr<RTCVoidRequest>, PassRefPtr<RTCIceCandidateDescriptor>);
    bool addStream(PassRefPtr<MediaStreamPrivate>);
    void removeStream(PassRefPtr<MediaStreamPrivate>);
    void getStats(PassRefPtr<RTCStatsRequest>);
    PassOwnPtr<RTCDataChannelHandler> createDataChannel(const String& label, const RTCDataChannelInit&);
    PassOwnPtr<RTCDTMFSenderHandler> createDTMFSender(PassRefPtr<MediaStreamSource>);

    void removeRequestedStreams(SignalingMessage*);
    void stop();

    // CallbackProxyClient implementation.
    void sourceInserted(GstElement* source, GstPad* pad);
    void onPadAdded(GstElement* element, GstPad* newPad);
    void onPadUnlinked(GstPad* pad, GstPad* peer);
    GstCaps* onRequestPtMap(GstElement* element, guint session, guint pt);
    gboolean onEnableOrdering(guint session, guint ssrc);
    guint onEnableRtx(guint session, guint ssrc, guint pt);
    void onFir(GstElement* bin, gint id, guint32 media_ssrc, guint32 csqn);

    // IceAgentClient implementation.
    void onIceGatheringDone(guint stream_id);
    void onIceStateChanged(guint stream_id, guint component_id, guint state);
    void onIceNewSelectedPair(guint stream_id, guint component_id, gchar *lfoundation, gchar *rfoundation);
    void onIceReceive(guint stream_id, guint component_id, guint len, gchar *buf);

    GstElement* transportBin() const { return m_transportBin; };
    void prepareTransportSource(GstRtpStream* gstRtpStream);

private:
    void setDataRemoteCandidates();
    void addDataMediaDescriptionToSignalingMessage(SignalingMessage* signalingMessage);
    void addMediaDescriptionToSignalingMessage(SignalingMessage* signalingMessage, GstMediaStream* gstMediaStream);
    void setRemoteCandidates(GstMediaStream* gstMediaStream);
    void addIceAttributes(MediaDescription* md, guint streamId);
    MediaDescription* createMediaDescription(GstRtpStream* gstRtpStream);

    void processSDP(const String&);
    void processStreamMediaDescriptionAnswer(Vector<MediaDescription*>& mds);
    void constructGStreamerSink(GstMediaStream* gstMediaStream);

    void ensureTransportBin(GstRtpStream* gstRtpStream);

    GstElement* createSourceExtension(const String& sourceId);
    GstPad* prepareTransportSink(GstRtpStream* gstRtpStream);

    void shutdownAndRemoveMediaStream(GstMediaStream*, bool fromClose);

    void processStreamMediaDescriptionOffer(Vector<MediaDescription*>& mds);

private:
    typedef HashMap<String, GstMediaStream*> GstMediaStreamMap;
    typedef HashMap<unsigned, GstRtpStream*, WTF::IntHash<unsigned int>, WTF::UnsignedWithZeroKeyHashTraits<unsigned int> > StreamIdGstRtpStreamMap;
    typedef HashMap<unsigned, GstRtpStream*, WTF::IntHash<unsigned int>, WTF::UnsignedWithZeroKeyHashTraits<unsigned int> > RtpSessionGstRtpStreamMap;

private:
    RTCPeerConnectionHandlerClient* m_client;
    IceAgent* m_iceAgent;
    CallbackProxy* m_callbackProxy;
    RefPtr<RTCSessionDescriptionDescriptor> m_localSessionDescription;
    RefPtr<RTCSessionDescriptionDescriptor> m_remoteSessionDescription;

    RefPtr<PeerConnectionHandlerConfiguration> m_configuration;

    guint m_dataStreamId;
    bool m_gatheringDataCandidates;
    String m_dataRemoteUsername;
    String m_dataRemotePassword;
    Vector<Candidate*> m_dataRemoteCandidates;
    bool m_dataCandidatesGathered;

    String m_sdp;

    CodecRegistry m_codecRegistry;
    GstElement* m_transportBin;
    String m_transportBinSourceId;
    GstElement* m_rtpBin;

    GstMediaStreamMap m_gstMediaStreamMap;
    StreamIdGstRtpStreamMap m_streamIdGstRtpStreamMap;
    RtpSessionGstRtpStreamMap m_rtpSessionGstRtpStreamMap;
    Vector<guint> m_pendingGatherings;
    int m_gatheringsLeft;
    int m_componentsNotReady;
    Vector<GstMediaStream*> m_streamsToRemove;

    gulong m_padAddedSignalHandler;
    gulong m_requestPtMapSignalHandler;

    gulong m_onFirSignalHandler;

    Vector<RefPtr<RTCSessionDescriptionRequest> > m_requests;
    bool m_answerPending;
    bool m_creatingOffer;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

#endif // PeerConnectionHandlerPrivateGStreamer_h
