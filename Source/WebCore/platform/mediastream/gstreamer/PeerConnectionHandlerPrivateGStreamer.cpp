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

#include "PeerConnectionHandlerPrivateGStreamer.h"

#include "Candidate.h"
#include "CentralPipelineUnit.h"
#include "GStreamerUtilities.h"
#include "Payload.h"
#include "RTCPeerConnectionHandler.h"
#include "SignalingMessage.h"
#include "UUID.h"
#include "KURL.h"
#include <openssl/rand.h>
#include "MediaStreamPrivate.h"
#include "RTCConfiguration.h"
#include "RTCDataChannel.h"
#include "RTCDataChannelHandler.h"
#include "RTCDTMFSenderHandler.h"
#include "RTCIceCandidateDescriptor.h"
#include "RTCPeerConnectionHandlerClient.h"
#include "RTCSessionDescriptionDescriptor.h"
#include "RTCSessionDescriptionRequest.h"
#include "RTCVoidRequest.h"
#include "MediaStreamPrivate.h"
#include "MediaStreamSourceGStreamer.h"

#include <wtf/MainThread.h>
#include <wtf/text/Base64.h>
#include <wtf/text/CString.h>
#include <wtf/text/StringBuilder.h>
#include <stdio.h>

#if USE(NICE)
#include <nice.h>
#endif

#define RTC_LOG(fmt, args...) printf("[PC] " fmt "\n", ##args)
//#define RTC_LOG(fmt, args...) (void) 0

namespace WebCore {


static IceAgent::Candidate sdpCand2IceCand(Candidate* candidate);

static int getNumberOfEnabledComponents(const MediaStreamPrivate* streamPrivate)
{
    int count = 0;
    for (unsigned i = 0; i < streamPrivate->numberOfAudioSources(); i++) {
        if (streamPrivate->audioSources(i)->enabled())
            count++;
    }
    for (unsigned i = 0; i < streamPrivate->numberOfVideoSources(); i++) {
        if (streamPrivate->videoSources(i)->enabled())
            count++;
    }
    RTC_LOG("%d components enabled", count);
    return count;
}

static String generateTransportBinSourceId(const PeerConnectionHandlerPrivateGStreamer* peerConnectionHandlerPointerValue)
{
    String sourceId = "TransportBin:";
    sourceId.append(String::number((gulong)peerConnectionHandlerPointerValue));
    return sourceId;
}

static String generateTransportBinPadSourceId(const PeerConnectionHandlerPrivateGStreamer* peerConnectionHandlerPointerValue, String padname)
{
    StringBuilder builder;
    builder.append("TransportBin:");
    builder.append(String::number((gulong)peerConnectionHandlerPointerValue));
    builder.append(":");
    builder.append(padname);
    return builder.toString();
}

PeerConnectionHandlerPrivateGStreamer::PeerConnectionHandlerPrivateGStreamer(RTCPeerConnectionHandlerClient* peerConnectionHandlerClient)
    : m_client(peerConnectionHandlerClient)
    , m_dataStreamId(0)
    // , m_dataCandidatesGathered(false)
    , m_gatheringsLeft(0)
    , m_componentsNotReady(0)
{
    initializeGStreamer();

    m_iceAgent = new IceAgent();
    m_iceAgent->setClient(this);

    m_callbackProxy = new CallbackProxy(this);

    m_transportBin = gst_bin_new(0);
    gst_object_ref(m_transportBin);
    g_object_set(m_transportBin, "async-handling", TRUE, NULL);
    m_rtpBin = gst_element_factory_make("rtpbin", 0);
    g_object_set(m_rtpBin, "latency", 100, "async-handling", TRUE, NULL);
    gst_object_ref(m_rtpBin);
    gst_bin_add(GST_BIN(m_transportBin), m_rtpBin);
    m_padAddedSignalHandler = m_callbackProxy->connectPadAddedSignal(m_rtpBin);
    m_requestPtMapSignalHandler = m_callbackProxy->connectRequestPtMapSignal(m_rtpBin);
    m_onFirSignalHandler = m_callbackProxy->connectOnFirSignal(m_rtpBin);

    // FIXMEPHIL
    m_configuration = PeerConnectionHandlerConfiguration::create();

    m_codecRegistry.installCodec(new H264Codec(), 2000);
    m_codecRegistry.installCodec(new VP8Codec(), 2100);
    m_codecRegistry.installCodec(new Mpeg4Codec(), 1800);
    m_codecRegistry.installCodec(new THEORACodec(), 1600);
    m_codecRegistry.installCodec(new JPEGCodec(), 1400);

    m_codecRegistry.installCodec(new G719Codec(), 2200);
    m_codecRegistry.installCodec(new AMRWBCodec(), 2000);
    m_codecRegistry.installCodec(new AMRCodec(), 1800);
    m_codecRegistry.installCodec(new PCMACodec(), 1600);
    m_codecRegistry.installCodec(new PCMUCodec(), 1400);
    m_codecRegistry.installCodec(new SPEEXCodec(), 1200);
    m_codecRegistry.installCodec(new OpusCodec(), 2300);

}

PeerConnectionHandlerPrivateGStreamer::~PeerConnectionHandlerPrivateGStreamer()
{
}

bool PeerConnectionHandlerPrivateGStreamer::initialize(PassRefPtr<RTCConfigurationPrivate> prpConfiguration)
{
    RTC_LOG("[%p] RTC initialize", this);
    RefPtr<RTCConfigurationPrivate> configuration = prpConfiguration;
    if (configuration->numberOfServers()) {
        RTCIceServerPrivate* iceServer = configuration->server(0);
        KURL url(KURL(), iceServer->urls().at(0));
        m_iceAgent->setStunServer(url.host().ascii().data(), url.port());
    }

    m_dataStreamId = m_iceAgent->addStream(1);
    m_iceAgent->attachReceive(m_dataStreamId, 1);

    return true;
}

void PeerConnectionHandlerPrivateGStreamer::createOffer(PassRefPtr<RTCSessionDescriptionRequest> prpRequest, PassRefPtr<RTCOfferOptionsPrivate> prpOptions)
{
    RTC_LOG("[%p] RTC createOffer", this);
    RefPtr<RTCSessionDescriptionRequest> request = prpRequest;

    // FIXME
    UNUSED_PARAM(prpOptions);

    m_creatingOffer = true;
    m_answerPending = false;
    m_gatheringsLeft += 1;
    m_componentsNotReady += 1;
    // for (size_t i = 0; i < pendingAddStreams.size(); i++)
    //     addStream(pendingAddStreams[i], "");

    m_gatheringDataCandidates = true;

    m_requests.append(request);

    m_iceAgent->gatherCandidates(m_dataStreamId);
    for(guint i=0; i < m_pendingGatherings.size(); i++) {
        m_iceAgent->gatherCandidates(m_pendingGatherings[i]);
    }
}

void PeerConnectionHandlerPrivateGStreamer::createAnswer(PassRefPtr<RTCSessionDescriptionRequest> prpRequest, PassRefPtr<RTCOfferAnswerOptionsPrivate>)
{
    RTC_LOG("[%p] RTC createAnswer", this);
    RefPtr<RTCSessionDescriptionRequest> request = prpRequest;

    m_creatingOffer = false;
    m_gatheringsLeft += 1;
    m_componentsNotReady += 1;
    // for (size_t i = 0; i < pendingAddStreams.size(); i++)
    //     addStream(pendingAddStreams[i], "");

    m_gatheringDataCandidates = true;
    m_answerPending = true;

    m_requests.append(request);

    m_iceAgent->gatherCandidates(m_dataStreamId);
    for(guint i=0; i < m_pendingGatherings.size(); i++) {
        m_iceAgent->gatherCandidates(m_pendingGatherings[i]);
    }

    // m_client->didChangeSignalingState(RTCPeerConnectionHandlerClient::SignalingStateStable);

    // RTC_LOG("SDP: %s", m_sdp.utf8().data());
    // if (!m_sdp.isEmpty()) {
    //     RefPtr<RTCSessionDescriptionDescriptor> descriptor = RTCSessionDescriptionDescriptor::create("answer", m_sdp);
    //     request->requestSucceeded(descriptor);
    // }// else
    // //    request->requestSucceeded(0);

}

void PeerConnectionHandlerPrivateGStreamer::setLocalDescription(PassRefPtr<RTCVoidRequest> prpRequest, PassRefPtr<RTCSessionDescriptionDescriptor> prpDescription)
{
    m_localSessionDescription = prpDescription;
    String sdp = m_localSessionDescription->sdp();
    String typ = m_localSessionDescription->type();
    RTC_LOG("[%p] RTC setLocalDescription type=%s", this, typ.utf8().data());
    // if (!typ.isEmpty())
    //     RTC_LOG("type:%s", typ.utf8().data());
    // if (!sdp.isEmpty())
    //     RTC_LOG("sdp: %s", sdp.utf8().data());

    RefPtr<RTCVoidRequest> request = prpRequest;
    request->requestSucceeded();
    m_client->didChangeSignalingState(RTCPeerConnectionHandlerClient::SignalingStateHaveLocalOffer);

}

void PeerConnectionHandlerPrivateGStreamer::setRemoteDescription(PassRefPtr<RTCVoidRequest> prpRequest, PassRefPtr<RTCSessionDescriptionDescriptor> prpDescription)
{
    m_remoteSessionDescription = prpDescription;
    String sdp = m_remoteSessionDescription->sdp();
    String typ = m_remoteSessionDescription->type();
    RTC_LOG("[%p] RTC setRemoteDescription type=%s", this, typ.utf8().data());
    //RTC_LOG("type:%s, sdp:%s", typ.utf8().data(), sdp.utf8().data());
    RefPtr<RTCVoidRequest> request = prpRequest;
    request->requestSucceeded();
    // FIXMEPHIL: emit this BEFORE setRemoteDescription gets called
    m_client->didChangeSignalingState(RTCPeerConnectionHandlerClient::SignalingStateHaveRemoteOffer);
}

PassRefPtr<RTCSessionDescriptionDescriptor> PeerConnectionHandlerPrivateGStreamer::localDescription()
{
    return m_localSessionDescription;
}

PassRefPtr<RTCSessionDescriptionDescriptor> PeerConnectionHandlerPrivateGStreamer::remoteDescription()
{
    return m_remoteSessionDescription;
}

bool PeerConnectionHandlerPrivateGStreamer::updateIce(PassRefPtr<RTCConfigurationPrivate>)
{
    RTC_LOG("RTC updateIce");
    return false;
}

bool PeerConnectionHandlerPrivateGStreamer::addIceCandidate(PassRefPtr<RTCVoidRequest> prpRequest, PassRefPtr<RTCIceCandidateDescriptor> prpDescriptor)
{
    RTC_LOG("[%p] RTC addIceCandidate", this);
    //RefPtr<RTCIceCandidateDescriptor> descriptor = prpDescriptor;
    //m_client->didGenerateIceCandidate(prpDescriptor);
    UNUSED_PARAM(prpDescriptor);
    RefPtr<RTCVoidRequest> request = prpRequest;
    request->requestSucceeded();
    return true;
}

bool PeerConnectionHandlerPrivateGStreamer::addStream(PassRefPtr<MediaStreamPrivate> prpStreamPrivate)
{
    RefPtr<MediaStreamPrivate> streamPrivate = prpStreamPrivate;
    RTC_LOG("[%p] RTC addStream streamPrivate=%p", this, streamPrivate.get());
    // FIXME: do something with constraints.

    GstMediaStream* gstMediaStream = new GstMediaStream(GstMediaStream::Local, streamPrivate->id());
    RTC_LOG("[%p] gstMediaStream %p", this, gstMediaStream);
    gstMediaStream->streamPrivate = streamPrivate;
    gstMediaStream->m_gatheringState = GstMediaStream::GatheringOffer;
    m_gstMediaStreamMap.add(gstMediaStream->streamPrivate->id(), gstMediaStream);

    guint nComponents = getNumberOfEnabledComponents(streamPrivate.get());

    m_gatheringsLeft += nComponents;
    m_componentsNotReady += nComponents * 2;
    RTC_LOG("number of components = %d, m_gatheringsLeft = %d, m_componentsNotReady = %d", nComponents, m_gatheringsLeft, m_componentsNotReady);

    CodecOptions audioOptions;
    CodecOptions videoOptions;
    // audioOptions.parse(options, "audio");
    // videoOptions.parse(options, "video");

    for (unsigned i = 0; i < streamPrivate->numberOfAudioSources(); i++) {
        if (!streamPrivate->audioSources(i)->enabled())
            continue;

        guint streamId = m_iceAgent->addStream(2);
        GstRtpStream* gstRtpStream = 0;

        gstRtpStream = gstMediaStream->addRtpStream(GstRtpStream::audio, streamId);
        // gstRtpStream->options() = audioOptions;
        // gstRtpStream = gstMediaStream->addRtpStream(GstRtpStream::video, streamId);
        // gstRtpStream->options() = videoOptions;
        gstRtpStream->m_baseSourceId = streamPrivate->audioSources(i)->id();

        m_iceAgent->attachIgnore(streamId, IceAgent::RTP_COMPONENT);
        m_iceAgent->attachIgnore(streamId, IceAgent::RTCP_COMPONENT);

        // if (m_configuration->type == PeerConnectionHandlerConfiguration::TypeTURN) {
        //     m_iceAgent->setRelayInfo(streamId,
        //                              IceAgent::RTP_COMPONENT,
        //                              m_configuration->host.ascii().data(),
        //                              m_configuration->port,
        //                              m_configuration->username.ascii().data(),
        //                              m_configuration->password.ascii().data());

        //     m_iceAgent->setRelayInfo(streamId,
        //                              IceAgent::RTCP_COMPONENT,
        //                              m_configuration->host.ascii().data(),
        //                              m_configuration->port,
        //                              m_configuration->username.ascii().data(),
        //                              m_configuration->password.ascii().data());
        // }

        m_streamIdGstRtpStreamMap.add(streamId, gstRtpStream);
        RTC_LOG("Added gstRtpStream to m_rtpSessionGstRtpStreamMap with sessionId %d", gstRtpStream->rtpSessionId());
        m_rtpSessionGstRtpStreamMap.add(gstRtpStream->rtpSessionId(), gstRtpStream);
        m_pendingGatherings.append(streamId);
    }

    for (unsigned i = 0; i < streamPrivate->numberOfVideoSources(); i++) {
        if (!streamPrivate->videoSources(i)->enabled())
            continue;

        guint streamId = m_iceAgent->addStream(2);
        GstRtpStream* gstRtpStream = 0;

        gstRtpStream = gstMediaStream->addRtpStream(GstRtpStream::video, streamId);
        gstRtpStream->m_baseSourceId = streamPrivate->videoSources(i)->id();

        m_iceAgent->attachIgnore(streamId, IceAgent::RTP_COMPONENT);
        m_iceAgent->attachIgnore(streamId, IceAgent::RTCP_COMPONENT);

        m_streamIdGstRtpStreamMap.add(streamId, gstRtpStream);
        RTC_LOG("Added gstRtpStream to m_rtpSessionGstRtpStreamMap with sessionId %d", gstRtpStream->rtpSessionId());
        m_rtpSessionGstRtpStreamMap.add(gstRtpStream->rtpSessionId(), gstRtpStream);
        m_pendingGatherings.append(streamId);
    }

    //m_client->didAddRemoteStream(streamPrivate.release());
    m_client->negotiationNeeded();
    return true;
}

void PeerConnectionHandlerPrivateGStreamer::removeStream(PassRefPtr<MediaStreamPrivate>)
{
    RTC_LOG("RTC removeStream");
}

void PeerConnectionHandlerPrivateGStreamer::getStats(PassRefPtr<RTCStatsRequest>)
{

}

PassOwnPtr<RTCDataChannelHandler> PeerConnectionHandlerPrivateGStreamer::createDataChannel(const String& label, const RTCDataChannelInit&)
{
    UNUSED_PARAM(label);
    return nullptr;
}

PassOwnPtr<RTCDTMFSenderHandler> PeerConnectionHandlerPrivateGStreamer::createDTMFSender(PassRefPtr<MediaStreamSource>)
{
    return nullptr;
}

void PeerConnectionHandlerPrivateGStreamer::removeRequestedStreams(SignalingMessage* signalingMessage)
{
    ASSERT(isMainThread());
    RTC_LOG("m_streamsToRemove.size = %u", m_streamsToRemove.size());
    Vector<GstMediaStream*>::iterator rsIt = m_streamsToRemove.begin();
    while (rsIt != m_streamsToRemove.end()) {
        RTC_LOG("removing a media");
        GstMediaStream* gstMediaStream = *rsIt;
        rsIt++;

        addMediaDescriptionToSignalingMessage(signalingMessage, gstMediaStream);
        shutdownAndRemoveMediaStream(gstMediaStream, false);
        m_gstMediaStreamMap.remove(gstMediaStream->label());
        delete gstMediaStream;
    }
    m_streamsToRemove.clear();
}

void PeerConnectionHandlerPrivateGStreamer::stop()
{

}

void PeerConnectionHandlerPrivateGStreamer::sourceInserted(GstElement* source, GstPad* pad)
{
    RTC_LOG("sourceInserted %p", source);
    ASSERT(isMainThread());
    if (!pad) {
        RTC_LOG("Source did not have a pad");
        return;
    }

    gchar* elementname = gst_element_get_name(source);
    String padname(gst_pad_get_name(pad));
    RTC_LOG("element name = %s, padname = %s", elementname, padname.ascii().data());
}

void PeerConnectionHandlerPrivateGStreamer::onPadAdded(GstElement* element, GstPad* newPad)
{
    RTC_LOG("PeerConnectionHandlerPrivateGStreamer::onPadAdded");
    ASSERT(isMainThread());
    gchar* elementname = gst_element_get_name(element);
    String padname(gst_pad_get_name(newPad));
    RTC_LOG("element name = %s, padname = %s", elementname, padname.ascii().data());

    if (padname.startsWith("recv_rtp_src_")) {
        Vector<String> tokens;
        padname.split("_", true, tokens); // recv_rtp_src_x_y_z
        if (tokens.size() != 6) {
            RTC_LOG("Unexpected padname");
            return;
        }
        bool wasInt1 = false;
        guint session = tokens[3].toUInt(&wasInt1);
        //guint ssrc = tokens[4].toUInt(&wasInt2);
        if (!wasInt1) {
            RTC_LOG("Could not parse the padname: %s", padname.ascii().data());
            return;
        }

        PeerConnectionHandlerPrivateGStreamer::RtpSessionGstRtpStreamMap::iterator nsIt = m_rtpSessionGstRtpStreamMap.find(session);
        if (nsIt == m_rtpSessionGstRtpStreamMap.end()) {
            RTC_LOG("Did not find a GstRtpStream with sessionId %u", session);
            return;
        }
        GstRtpStream* gstRtpStream = nsIt->value;

        StringBuilder builder;
        builder.append("src_");
        builder.append(tokens[3]);
        builder.append("_");
        builder.append(tokens[4]);
        builder.append("_");
        builder.append(tokens[5]);
        String ghostpadname = builder.toString();

        gst_bin_add(GST_BIN(m_transportBin), gstRtpStream->m_bin);

        GstPad* decBinSrcPad = gst_element_get_static_pad(gstRtpStream->m_bin, "src");
        GstPad* decBinSinkPad = gst_element_get_static_pad(gstRtpStream->m_bin, "sink");

        RTC_LOG("decBinSrcPad (%p), decBinSinkPad (%p)", decBinSrcPad, decBinSinkPad);
        gst_element_sync_state_with_parent(gstRtpStream->m_bin);
        GstPadLinkReturn linkRes;
        linkRes = gst_pad_link(newPad, decBinSinkPad);
        if (linkRes != GST_PAD_LINK_OK) {
            RTC_LOG("newPad not linked to decBinSinkPad");
            // RTC_LOG("newPad caps= %s", gst_caps_to_string(gst_pad_get_caps(newPad)));
            // RTC_LOG("decBinSinkPad caps= %s", gst_caps_to_string(gst_pad_get_caps(decBinSinkPad)));
        }
        ASSERT(linkRes == GST_PAD_LINK_OK);

        GstPad *ghostSrcPad = gst_ghost_pad_new(ghostpadname.ascii().data(), decBinSrcPad);
        gst_pad_set_active(ghostSrcPad, TRUE);
        gst_element_add_pad(m_transportBin, ghostSrcPad);
        // FIXMEPHIL: needed?
        //gst_pad_set_blocked_async(ghostSrcPad, TRUE, padBlockedHelper, 0);

        //gst_object_unref(ghostSrcPad);

        gstRtpStream->m_rtpRecvSrcPad = newPad;
        gstRtpStream->m_rtpRecvSrcGhostPad = ghostSrcPad;

        gstRtpStream->m_rtpRecvSrcGhostPadUnlinkedSignalHandler = m_callbackProxy->connectPadUnlinkedSignal(ghostSrcPad);

        String transportBinPadSourceId = generateTransportBinPadSourceId(this, padname);
        gstRtpStream->m_baseSourceId = transportBinPadSourceId;
        m_callbackProxy->insertSource(gstRtpStream->gstMediaStream()->streamPrivate->centralPipelineUnit(), m_transportBin, ghostSrcPad, transportBinPadSourceId);
    }

    // FIXMEPHIL: needed?
    //gst_pad_set_blocked_async(newPad, FALSE, padBlockedHelper, 0);
}

void PeerConnectionHandlerPrivateGStreamer::onPadUnlinked(GstPad* pad, GstPad* peer)
{
    RTC_LOG("PeerConnectionHandlerPrivateGStreamer::onPadUnlinked");
    UNUSED_PARAM(pad);
    UNUSED_PARAM(peer);
}

GstCaps* PeerConnectionHandlerPrivateGStreamer::onRequestPtMap(GstElement* element, guint session, guint pt)
{
    RTC_LOG("PeerConnectionHandlerPrivateGStreamer::onRequestPtMap");
    UNUSED_PARAM(element);
    UNUSED_PARAM(session);
    UNUSED_PARAM(pt);
    return 0;
}

gboolean PeerConnectionHandlerPrivateGStreamer::onEnableOrdering(guint session, guint ssrc)
{
    RTC_LOG("PeerConnectionHandlerPrivateGStreamer::onEnableOrdering");
    UNUSED_PARAM(session);
    UNUSED_PARAM(ssrc);
    return false;
}

guint PeerConnectionHandlerPrivateGStreamer::onEnableRtx(guint session, guint ssrc, guint pt)
{
    RTC_LOG("PeerConnectionHandlerPrivateGStreamer::onEnableRtx");
    UNUSED_PARAM(session);
    UNUSED_PARAM(ssrc);
    UNUSED_PARAM(pt);
    return 0;
}

void PeerConnectionHandlerPrivateGStreamer::onFir(GstElement* bin, gint id, guint32 media_ssrc, guint32 csqn)
{
    RTC_LOG("PeerConnectionHandlerPrivateGStreamer::onFir");
    UNUSED_PARAM(bin);
    UNUSED_PARAM(id);
    UNUSED_PARAM(media_ssrc);
    UNUSED_PARAM(csqn);
}

void PeerConnectionHandlerPrivateGStreamer::onIceGatheringDone(guint streamId)
{
    RTC_LOG("[%p] ICE gathering done %u", this, streamId);
    ASSERT(isMainThread());
    bool doStop = false;
    bool generateSdp = false;
    bool remoteOfferAvailable = false;

    RTC_LOG("Currently cached: %u requests", m_requests.size());
    RefPtr<RTCSessionDescriptionRequest> request;

    m_gatheringsLeft--;
    RTC_LOG("[%p] Gathering done for streamId %d, gatherings left=%d", this, streamId, m_gatheringsLeft);
    if (m_gatheringsLeft == 0) {
        SignalingMessage signalingMessage;
        if (m_gatheringDataCandidates) {
            addDataMediaDescriptionToSignalingMessage(&signalingMessage);
            generateSdp = true;
            m_gatheringDataCandidates = false;
            m_dataCandidatesGathered = true;
            setDataRemoteCandidates();
        }

        for(size_t i = 0; i < m_pendingGatherings.size(); i++) {
            guint streamId = m_pendingGatherings[i];
            StreamIdGstRtpStreamMap::iterator nsIt = m_streamIdGstRtpStreamMap.find(streamId);
            if (nsIt == m_streamIdGstRtpStreamMap.end()) {
                RTC_LOG("%p Did not find streamid in StreamIdGstRtpStreamMap", this);
                return;
            }
            GstRtpStream* gstRtpStream = nsIt->value;
            GstMediaStream* gstMediaStream = gstRtpStream->gstMediaStream();

            guint nCandidates = m_iceAgent->getLocalCandidates(streamId, IceAgent::RTP_COMPONENT).size();
            if (!nCandidates) {
                doStop = true;
                break;
            }

            if (gstMediaStream->m_gatheringState != GstMediaStream::GatheringIdle){

                if (m_answerPending || gstMediaStream->m_gatheringState == GstMediaStream::GatheringAnswer) {
                    RTC_LOG("GatheringAnswer");
                    setRemoteCandidates(gstMediaStream);
                    gstMediaStream->m_negotiationState = GstMediaStream::Idle;
                    remoteOfferAvailable = true;
                } else if (gstMediaStream->m_gatheringState == GstMediaStream::GatheringOffer) {
                    RTC_LOG("GatheringOffer");
                    gstMediaStream->m_negotiationState = GstMediaStream::OfferSent;
                }

                // if (gstMediaStream->m_gatheringState == GstMediaStream::GatheringOffer) {
                //     RTC_LOG("GatheringOffer");
                //     gstMediaStream->m_negotiationState = GstMediaStream::OfferSent;
                // } else if (gstMediaStream->m_gatheringState == GstMediaStream::GatheringAnswer) {
                //     RTC_LOG("GatheringAnswer");
                //     setRemoteCandidates(gstMediaStream);
                //     gstMediaStream->m_negotiationState = GstMediaStream::Idle;
                // }
                gstMediaStream->m_gatheringState = GstMediaStream::GatheringIdle;
                addMediaDescriptionToSignalingMessage(&signalingMessage, gstMediaStream);
                generateSdp = true;
            }
        }
        m_pendingGatherings.clear();

        removeRequestedStreams(&signalingMessage);

        if (generateSdp) {
            String sdp = formatSdp(&signalingMessage);
            if (sdp != "") {
                m_sdp = sdp;
                RTC_LOG("[%p] Ice gathering done - signaling message callback fired", this);

                Vector<MediaDescription*>::iterator mdIt = signalingMessage.mediaDescriptions().begin();
                while (mdIt != signalingMessage.mediaDescriptions().end()) {
                    MediaDescription* md = *mdIt;
                    mdIt++;

                    Vector<Candidate*> candidates = md->candidates();
                    Vector<Candidate*>::iterator cIt = candidates.begin();
                    while (cIt != candidates.end()) {
                        Candidate* candidate = *cIt;
                        cIt++;

                        unsigned short sdpMLineIndex = 0;
                        String candidateAttribute = formatCandidate(candidate);
                        String sdpMid = getSdpMid(md);
                        RefPtr<RTCIceCandidateDescriptor> iceCandidate = RTCIceCandidateDescriptor::create(candidateAttribute, sdpMid, sdpMLineIndex);
                        m_client->didGenerateIceCandidate(iceCandidate.release());
                    }

                }

                // if (!m_creatingOffer && remoteOfferAvailable) {
                //     m_client->didChangeSignalingState(RTCPeerConnectionHandlerClient::SignalingStateHaveRemoteOffer);
                // } else {
                //     m_client->didChangeSignalingState(RTCPeerConnectionHandlerClient::SignalingStateStable);
                // }

                m_client->didChangeIceGatheringState(RTCPeerConnectionHandlerClient::IceGatheringStateComplete);
                m_client->didChangeIceConnectionState(RTCPeerConnectionHandlerClient::IceConnectionStateCompleted);

                if (!m_requests.isEmpty()) {
                    request = m_requests.last();
                    m_requests.removeLast();
                }

                // TODO
                if (request) {
                    //RTC_LOG("SDP generated: %s", sdp.utf8().data());
                    processSDP(sdp);
                    RefPtr<RTCSessionDescriptionDescriptor> descriptor = RTCSessionDescriptionDescriptor::create("offer", sdp.utf8().data() /* sdp */);
                    request->requestSucceeded(descriptor);
                }

            }
        }

        if (doStop)
            stop();
    }
}

void PeerConnectionHandlerPrivateGStreamer::processSDP(const String& sdp)
{
    SignalingMessage* signalingMessage = createSignalingMessage(sdp);
    RTC_LOG("signalingMessage = %p", signalingMessage);
    if (!signalingMessage)
        return;

    Vector<String> labels = signalingMessage->labels();
    bool gatherDataCandidates = false;

    for(size_t i = 0; i < labels.size(); i++) {
        if (labels[i] == "data") {
            // FIXMEPHIL
            //gatherDataCandidates = processDataMediaDescription(signalingMessage->mediaDescriptions("data")[0]);
        } else {
            Vector<MediaDescription*> mds = signalingMessage->mediaDescriptions(labels[i]);
            RTC_LOG(">>>>>>>>>>> [%p] processSDP label=%s", this, labels[i].utf8().data());
            if (!m_gstMediaStreamMap.contains(labels[i]))
                continue;
            GstMediaStream* localGstMediaStream = m_gstMediaStreamMap.get(labels[i]);
            if (m_answerPending) {
                processStreamMediaDescriptionOffer(mds);
                m_answerPending = false;
            } else if (localGstMediaStream->m_negotiationState == GstMediaStream::OfferSent) {
                processStreamMediaDescriptionAnswer(mds);
            } else if (localGstMediaStream->m_negotiationState == GstMediaStream::UpdateSent) {
                // FIXMEPHIL
                //processStreamMediaDescriptionUpdateAnswer(mds);
                RTC_LOG("processStreamMediaDescriptionUpdateAnswer");
            } else if (localGstMediaStream->m_negotiationState == GstMediaStream::Idle) {
                // FIXMEPHIL
                //processStreamMediaDescriptionUpdate(mds);
                RTC_LOG("processStreamMediaDescriptionUpdate");
            }
        }
    }
    if (gatherDataCandidates) {
        m_gatheringDataCandidates = true;
        m_iceAgent->gatherCandidates(m_dataStreamId);
    }
    if (m_pendingGatherings.size() > 0) {
        for(guint i=0; i < m_pendingGatherings.size(); i++) {
            m_iceAgent->gatherCandidates(m_pendingGatherings[i]);
        }
        // FIXMEPHIL
    // } else if (m_streamsToRemove.size() > 0) {
    //     SignalingMessage signalingMessage;
    //     removeRequestedStreams(&signalingMessage);
    //     String sdp = formatSdp(signalingMessage);
    //     m_peerConnectionHandlerClient->didGenerateSDP(sdp);
    }
}

static void setIceParameters(GstRtpStream* gstRtpStream, MediaDescription* md)
{
    gstRtpStream->m_username = md->username();
    gstRtpStream->m_password = md->password();

    for(size_t k = 0; k < md->candidates().size(); k++) {
        Candidate* candidate = md->candidates()[k];
        if (candidate->componentId() == IceAgent::RTP_COMPONENT)
            gstRtpStream->rtpCandidates().append(sdpCand2IceCand(candidate));
        else if (candidate->componentId() == IceAgent::RTCP_COMPONENT)
            gstRtpStream->rtcpCandidates().append(sdpCand2IceCand(candidate));
    }
}


void PeerConnectionHandlerPrivateGStreamer::processStreamMediaDescriptionAnswer(Vector<MediaDescription*>& mds)
{
    String label = mds[0]->label();
    if (!m_gstMediaStreamMap.contains(label)) {
        RTC_LOG("SDP RESPONSE INVALID! Label %s not in map.", label.ascii().data());
        return;
    }

    GstMediaStream* gstMediaStream = m_gstMediaStreamMap.get(label);
    if (gstMediaStream->m_negotiationState == GstMediaStream::Idle) {
        RTC_LOG("Received answer twice. Ignoring last.");
        return;
    }

    //FIXME: What if only one port is 0???
    if (mds[0]->port() == 0) {
        RTC_LOG("Offered media rejected");
        m_componentsNotReady -= gstMediaStream->rtpStreams().size() * 2;
        // FIXMEPHIL
        //shutdownAndRemoveMediaStream(gstMediaStream, false);
        m_gstMediaStreamMap.remove(gstMediaStream->label());
        return;
    }

    if (mds.size() != gstMediaStream->rtpStreams().size()) {
        RTC_LOG("Number of rtp streams mismatch!");
        return;
    }

    for(size_t i = 0; i < mds.size(); i++) {
        MediaDescription* md = mds[i];
        GstRtpStream* gstRtpStream = gstMediaStream->rtpStreams()[i];

        gstRtpStream->m_defaultRemoteAddress = mds[i]->connectionAddress();
        gstRtpStream->m_defaultRemotePort = mds[i]->port();
        gstRtpStream->setMaxBandwidth(mds[i]->maximumBandwidth());

        for(size_t j = 0; j < md->payloads().size(); j++) {
            Payload* payload = md->payloads()[j];
            Codec* codec = m_codecRegistry.getCodec(payload->codecName(), payload->rate());
            if (codec == NULL || payload->rate() != codec->rate()) {
                RTC_LOG("Codec %s with rate %d not supported", payload->codecName().ascii().data(), payload->rate());
            } else {
                RTC_LOG("Selected codec %s with rate %d",  payload->codecName().ascii().data(), payload->rate());
                gstRtpStream->m_negotiatedCodec = codec;
                gstRtpStream->m_negotiatedCodec->setPayloadTypeNumber(payload->payloadTypeNumber());
                gstRtpStream->m_negotiatedCodec->setFmtp(payload->format());

                // FIXME: Should this be set on codec instead or something?
                gstRtpStream->setFramesizeWidth(payload->framesizeWidth());
                gstRtpStream->setFramesizeHeight(payload->framesizeHeight());
                RTC_LOG("gstRtpStream framesizeWidth set to %u and framesizeHeight to %u", gstRtpStream->framesizeWidth(), gstRtpStream->framesizeHeight());
                break;
            }
        }

        setIceParameters(gstRtpStream, md);

    }

    gstMediaStream->m_negotiationState = GstMediaStream::Idle;

    setRemoteCandidates(gstMediaStream);
    constructGStreamerSink(gstMediaStream);
}

void PeerConnectionHandlerPrivateGStreamer::processStreamMediaDescriptionOffer(Vector<MediaDescription*>& mds)
{
    String label = mds[0]->label();
    RTC_LOG("processStreamMediaDescriptionOffer label=%s", label.utf8().data());
    // Check if remote stream already exists. If so, abort.
    // FIXMEPHIL
    // if (m_gstMediaStreamMap.contains(label)) {
    //     RTC_LOG("Offered media already exists");
    //     return;
    // }

    GstMediaStream* gstMediaStream = new GstMediaStream(GstMediaStream::Remote, label);
    gstMediaStream->m_gatheringState = GstMediaStream::GatheringAnswer;
    m_gstMediaStreamMap.add(gstMediaStream->label(), gstMediaStream);

    for(size_t i = 0; i < mds.size(); i++) {
        MediaDescription* md = mds[i];
        guint streamId = m_iceAgent->addStream(2);
        GstRtpStream* gstRtpStream = 0;
        if (md->mediaType() == MediaDescription::Audio)
            gstRtpStream = gstMediaStream->addRtpStream(GstRtpStream::audio, streamId);
        else if (md->mediaType() == MediaDescription::Video)
            gstRtpStream = gstMediaStream->addRtpStream(GstRtpStream::video, streamId);

        gstRtpStream->m_defaultRemotePort = md->port();
        gstRtpStream->m_defaultRemoteAddress = md->connectionAddress();
        gstRtpStream->setMaxBandwidth(md->maximumBandwidth());

        for(size_t j = 0; j < md->payloads().size(); j++) {
            // FIXME: elidani  - set payloadtype for rtx from payload..
            Payload* payload = md->payloads()[j];
            Codec* codec = m_codecRegistry.getCodec(payload->codecName(), payload->rate());
            if (codec == NULL || payload->rate() != codec->rate()) {
                RTC_LOG("Codec %s with rate %d not supported", payload->codecName().ascii().data(), payload->rate());
            } else {
                RTC_LOG("Selected codec %s with rate %d",  payload->codecName().ascii().data(), payload->rate());
                codec->setFmtp(payload->format());
                gstRtpStream->m_negotiatedCodec = codec;
                gstRtpStream->m_negotiatedCodec->setPayloadTypeNumber(payload->payloadTypeNumber());

                gstRtpStream->setFramesizeWidth(payload->framesizeWidth());
                gstRtpStream->setFramesizeHeight(payload->framesizeHeight());
                RTC_LOG("Set framesizeWidth to %u and framesizeHeight to %u for gstRtpStream", payload->framesizeWidth(), payload->framesizeHeight());
                break;
            }
        }

        setIceParameters(gstRtpStream, md);

        m_streamIdGstRtpStreamMap.add(streamId, gstRtpStream);
        RTC_LOG("GstRtpStream added for streamId %u", streamId);

        m_rtpSessionGstRtpStreamMap.add(gstRtpStream->rtpSessionId(), gstRtpStream);


        if (m_configuration->type == PeerConnectionHandlerConfiguration::TypeTURN) {
            m_iceAgent->setRelayInfo(streamId,
                                     IceAgent::RTP_COMPONENT,
                                     m_configuration->host.ascii().data(),
                                     m_configuration->port,
                                     m_configuration->username.ascii().data(),
                                     m_configuration->password.ascii().data());

            m_iceAgent->setRelayInfo(streamId,
                                     IceAgent::RTCP_COMPONENT,
                                     m_configuration->host.ascii().data(),
                                     m_configuration->port,
                                     m_configuration->username.ascii().data(),
                                     m_configuration->password.ascii().data());
        }
        m_gatheringsLeft += 1;
        m_componentsNotReady += 2;
        m_pendingGatherings.append(streamId);

    }

    gstMediaStream->createPrivateStream(this);

        // gstRtpStream->m_packetizerSourceId = generateSourceExtensionIdForSinkBin(this, gstRtpStream->streamId());
        // if (!cpu->doesSourceExist(gstRtpStream->m_packetizerSourceId)) {
        //     source = createSourceExtension(gstRtpStream->m_packetizerSourceId);
        //     cpu->registerSourceExtension(source, sourceId, gstRtpStream->m_packetizerSourceId);
        // }

    // // FIXMEPHIL: merge this loop somehow with createPrivateStream logic
    // Vector<GstRtpStream*>::iterator gstRtpStreamsIt = gstMediaStream->rtpStreams().begin();
    // while (gstRtpStreamsIt != gstMediaStream->rtpStreams().end()) {
    //     GstRtpStream* gstRtpStream = *gstRtpStreamsIt;
    //     gstRtpStreamsIt++;
    //     prepareTransportSource(gstRtpStream);
        
    // }

    RTC_LOG("remote stream added");
    m_client->didAddRemoteStream(gstMediaStream->streamPrivate);
}

static String generateSourceExtensionIdForSinkBin(const PeerConnectionHandlerPrivateGStreamer* peerConnectionHandlerPointerValue, guint streamId) {
    StringBuilder builder;
    builder.append("pcSinkBin:");
    builder.append(String::number((gulong)peerConnectionHandlerPointerValue));
    builder.append(":");
    builder.append(String::number(streamId));
    return builder.toString();
}

void PeerConnectionHandlerPrivateGStreamer::constructGStreamerSink(GstMediaStream* gstMediaStream)
{
    CentralPipelineUnit* cpu = gstMediaStream->streamPrivate->centralPipelineUnit();
    Vector<GstRtpStream*>& gstRtpStreams = gstMediaStream->rtpStreams();
    Vector<GstRtpStream*>::iterator gstRtpStreamsIt = gstRtpStreams.begin();

    RTC_LOG("constructGStreamerSink for %u rtp streams", gstRtpStreams.size());

    while (gstRtpStreamsIt != gstRtpStreams.end()) {
        GstRtpStream* gstRtpStream = *gstRtpStreamsIt;
        gstRtpStreamsIt++;
        CodecOptions& options = gstRtpStream->options();

        bool codeOnlyOnce = options.attribute("encode_only_once") == "true";
        Codec* codec = gstRtpStream->m_negotiatedCodec;
        if (!codec) {
            RTC_LOG("No codec negotiated. Could not construct sink");
            return;
        }

        RTC_LOG("Creating encoder and payload packitizer for the negotiated %s codec", codec->name().ascii().data());
        GstElement* encoder = codec->createEncoderElement();
        GstElement* pay = codec->createRtpPacketizerElement();
        // const gchar* p = g_getenv("WEBKIT_RTP_MTU");
        // if (p) {
        //     guint mtu = strtol(p, NULL, 10);
        //     if (mtu >= 28) g_object_set(pay, "mtu", mtu, NULL);
        // }

        if (!encoder || !pay) {
            RTC_LOG("Failed to create encoder or payload packitizer element");
            return;
        }

        gstRtpStream->m_encoder = encoder;
        gstRtpStream->m_pay = pay;
        String sourceId = gstRtpStream->m_baseSourceId;
        GstPad* sinkPad = 0;
        GstElement* source = 0;
        if (codeOnlyOnce) {
            gstRtpStream->createSinkBinWithoutEncoder();
            if (!cpu->doesSourceExist(gstRtpStream->m_encoderSourceId)) {
                source = gstRtpStream->createEncodingSourceExtension();
                cpu->registerSourceExtension(source, sourceId, gstRtpStream->m_encoderSourceId);
            }
            sourceId = gstRtpStream->m_encoderSourceId;
        } else {
            gstRtpStream->createSinkBinWithEncoder();
        }

        gstRtpStream->m_packetizerSourceId = generateSourceExtensionIdForSinkBin(this, gstRtpStream->streamId());
        if (!cpu->doesSourceExist(gstRtpStream->m_packetizerSourceId)) {
            source = createSourceExtension(gstRtpStream->m_packetizerSourceId);
            cpu->registerSourceExtension(source, sourceId, gstRtpStream->m_packetizerSourceId);
        }

        sinkPad = prepareTransportSink(gstRtpStream);
        //cpu->connectToSource(0, gstRtpStream->m_packetizerSourceId, m_transportBin, sinkPad);
        cpu->connectToSource(0, sourceId, gstRtpStream->m_bin, 0);
        //cpu->connectToSource(0, gstRtpStream->m_packetizerSourceId, m_transportBin, sinkPad);
        if (source) {
            gboolean linked = gst_element_link(source, m_transportBin);
            RTC_LOG("packitizer linked to transport bin: %d", linked);
        }
    }
}

GstElement* PeerConnectionHandlerPrivateGStreamer::createSourceExtension(const String& sourceId)
{
    RTC_LOG("sourceId is %s", sourceId.ascii().data());
    bool wasInt1 = false;
    Vector<String> tokens;
    sourceId.split(":", true, tokens);
    if (tokens.isEmpty() || tokens.size() < 3) {
        RTC_LOG("Failed parsing sourceId. size of tokens vector is %d", (int)tokens.size());
        return NULL;
    }

    if (tokens[0] != "pcSinkBin") {
        RTC_LOG("Unknown sourceId: %s", sourceId.ascii().data());
        return NULL;
    }

    // TODO: Move to help function?
    String streamIdStr = tokens[2];
    guint streamId = streamIdStr.toUInt(&wasInt1);
    if (!wasInt1) {
        RTC_LOG("Invalid stream id");
        return NULL;
    }

    StreamIdGstRtpStreamMap::iterator nsIt = m_streamIdGstRtpStreamMap.find(streamId);
    if (nsIt == m_streamIdGstRtpStreamMap.end()) {
        RTC_LOG("Stream not found");
        return NULL;
    }

    RTC_LOG("sourceExtension bin is: %p", nsIt.get()->value->m_bin);
    return nsIt.get()->value->m_bin;
}

void PeerConnectionHandlerPrivateGStreamer::shutdownAndRemoveMediaStream(GstMediaStream* gstMediaStream, bool fromClose)
{
    RTC_LOG("shutdownAndRemoveMediaStream");
    UNUSED_PARAM(gstMediaStream);
    UNUSED_PARAM(fromClose);
    ASSERT(isMainThread());
}

void PeerConnectionHandlerPrivateGStreamer::ensureTransportBin(GstRtpStream* gstRtpStream)
{
    RTC_LOG("ensureTransportBin sourceId=%s stream=%p", m_transportBinSourceId.utf8().data(), gstRtpStream);
    if (m_transportBinSourceId.isEmpty()) {
        m_transportBinSourceId = generateTransportBinSourceId(this);
        CentralPipelineUnit* centralPipelineUnit = gstRtpStream->gstMediaStream()->streamPrivate->centralPipelineUnit();
        m_callbackProxy->insertSource(centralPipelineUnit, m_transportBin, NULL, m_transportBinSourceId);
        m_onFirSignalHandler = m_callbackProxy->connectOnFirSignal(m_rtpBin);
    }
}

GstPad* PeerConnectionHandlerPrivateGStreamer::prepareTransportSink(GstRtpStream* gstRtpStream)
{
    String rtpSendSinkName = String::format("send_rtp_sink_%d", gstRtpStream->rtpSessionId());
    String rtpSendSrcName = String::format("send_rtp_src_%d", gstRtpStream->rtpSessionId());
    String rtcpSendSrcName = String::format("send_rtcp_src_%d", gstRtpStream->rtpSessionId());
    String rtcpRecvSinkName = String::format("recv_rtcp_sink_%d", gstRtpStream->rtpSessionId());
    String ghostSinkName = String::format("sink_%d", gstRtpStream->rtpSessionId());

    GstElement* niceRtpSink = 0;
    GstElement* niceRtcpSink = 0;
    GstElement* niceRtcpSrc = 0;

    ensureTransportBin(gstRtpStream);

    gchar* niceRtpSinkName = createUniqueName("niceRtpSink");
    niceRtpSink = m_iceAgent->createGstSink(niceRtpSinkName, gstRtpStream->streamId(), IceAgent::RTP_COMPONENT);
    delete niceRtpSinkName;

    gchar* niceRtcpSinkName = createUniqueName("niceRtcpSink");
    niceRtcpSink = m_iceAgent->createGstSink(niceRtcpSinkName, gstRtpStream->streamId(), IceAgent::RTCP_COMPONENT);
    delete niceRtcpSinkName;

    gchar* niceRtcpSrcName = createUniqueName("niceRtcpSrc");
    niceRtcpSrc = m_iceAgent->createGstSrc(niceRtcpSrcName, gstRtpStream->streamId(), IceAgent::RTCP_COMPONENT);
    delete niceRtcpSrcName;

    GstPad* rtpSendSinkPad = gst_element_get_request_pad(m_rtpBin, rtpSendSinkName.ascii().data());
    RTC_LOG("rtpSendSinkPad = %p", rtpSendSinkPad);

    GstElement* q1 = gst_element_factory_make("queue", NULL);
    GstPad* q1SrcPad = gst_element_get_static_pad(q1, "src");
    GstPad* q1SinkPad = gst_element_get_static_pad(q1, "sink");
    GstElement* q2 = gst_element_factory_make("queue", NULL);
    GstPad* q2SrcPad = gst_element_get_static_pad(q2, "src");
    GstPad* q2SinkPad = gst_element_get_static_pad(q2, "sink");
    GstElement* q3 = gst_element_factory_make("queue", NULL);
    GstPad* q3SrcPad = gst_element_get_static_pad(q3, "src");
    GstPad* q3SinkPad = gst_element_get_static_pad(q3, "sink");

    gst_bin_add_many(GST_BIN(m_transportBin), niceRtpSink, q1,niceRtcpSink, q2, niceRtcpSrc, q3, NULL);

    GstPad* niceRtpSinkPad = gst_element_get_static_pad(niceRtpSink, "sink");
    GstPad* rtpSendSrcPad = gst_element_get_static_pad(m_rtpBin, rtpSendSrcName.ascii().data());
    RTC_LOG("Trying to connect rtpSendSrcPad %p with niceRtpSinkPad %p", rtpSendSrcPad, niceRtpSinkPad);

    GstPadLinkReturn linkRes;
    linkRes = gst_pad_link(rtpSendSrcPad,  q1SinkPad);
    if (linkRes != GST_PAD_LINK_OK) RTC_LOG("link failed");
    ASSERT(linkRes == GST_PAD_LINK_OK);
    linkRes = gst_pad_link(q1SrcPad, niceRtpSinkPad);
    if (linkRes != GST_PAD_LINK_OK) RTC_LOG("link failed");
    ASSERT(linkRes == GST_PAD_LINK_OK);

    gst_object_unref(niceRtpSinkPad);
    gst_object_unref(rtpSendSrcPad);

    GstPad* niceRtcpSinkPad = gst_element_get_static_pad(niceRtcpSink, "sink");
    GstPad* rtcpSendSrcPad = gst_element_get_request_pad(m_rtpBin, rtcpSendSrcName.ascii().data());
    RTC_LOG("Trying to connect rtcpSendSrcPad %p with niceRtcpSinkPad %p", rtcpSendSrcPad, niceRtcpSinkPad);

    linkRes = gst_pad_link(rtcpSendSrcPad, q2SinkPad);
    if (linkRes != GST_PAD_LINK_OK) RTC_LOG("link failed");
    ASSERT(linkRes == GST_PAD_LINK_OK);
    linkRes = gst_pad_link(q2SrcPad, niceRtcpSinkPad);
    if (linkRes != GST_PAD_LINK_OK) RTC_LOG("link failed");
    ASSERT(linkRes == GST_PAD_LINK_OK);

    gst_object_unref(niceRtcpSinkPad);
    gst_object_unref(rtcpSendSrcPad);

    GstPad* rtcpNiceSrcPad = gst_element_get_static_pad(niceRtcpSrc, "src");
    GstPad* rtcpRecvSinkPad = gst_element_get_request_pad(m_rtpBin, rtcpRecvSinkName.ascii().data());
    RTC_LOG("Trying to connect rtcpNiceSrcPad %p with rtcpRecvSinkPad %p", rtcpNiceSrcPad, rtcpRecvSinkPad);

    linkRes = gst_pad_link(rtcpNiceSrcPad, q3SinkPad);
    ASSERT(linkRes == GST_PAD_LINK_OK);
    linkRes = gst_pad_link(q3SrcPad, rtcpRecvSinkPad);
    ASSERT(linkRes == GST_PAD_LINK_OK);

    //gst_object_unref(rtcpNiceSrcPad);
    //gst_object_unref(q3SinkPad);

    gstRtpStream->m_rtpNiceSinkPad = niceRtpSinkPad;
    gstRtpStream->m_rtpBinSendRtpSrcPad = rtpSendSrcPad;
    gstRtpStream->m_rtpNiceSinkQueue = q1;

    gstRtpStream->m_rtcpNiceSinkPad = niceRtpSinkPad;
    gstRtpStream->m_rtpBinSendRtcpSrcPad = rtcpSendSrcPad;
    gstRtpStream->m_rtcpNiceSinkQueue = q2;

    gstRtpStream->m_rtcpNiceSrc = niceRtcpSrc;
    gstRtpStream->m_rtcpNiceSrcPad = rtcpNiceSrcPad;
    gstRtpStream->m_rtpBinRecvRtcpSinkPad = rtcpRecvSinkPad;
    gstRtpStream->m_rtcpNiceSrcQueue = q3;

    GstPad* ghostSinkPad = gst_ghost_pad_new(ghostSinkName.ascii().data(), rtpSendSinkPad);
    gst_pad_set_active(ghostSinkPad, TRUE);
    gst_element_add_pad(m_transportBin, ghostSinkPad);

    gstRtpStream->m_rtpSendSinkGhostPadUnlinkedSignalHandler = m_callbackProxy->connectPadUnlinkedSignal(ghostSinkPad);

    gst_object_unref(rtpSendSinkPad);

    gstRtpStream->m_rtpSendSinkPad = rtpSendSinkPad;
    gstRtpStream->m_rtpSendSinkGhostPad = ghostSinkPad;

    RTC_LOG("gstRtpStream->m_rtpSendSinkPad set to %p. gstRtpStream->m_rtpSendSinkGhostPad set to %p", gstRtpStream->m_rtpSendSinkPad, gstRtpStream->m_rtpSendSinkGhostPad);

    return ghostSinkPad;
}

void PeerConnectionHandlerPrivateGStreamer::prepareTransportSource(GstRtpStream* gstRtpStream)
{
    ensureTransportBin(gstRtpStream);

    String rtpSessionString = String::number(gstRtpStream->rtpSessionId());
    RTC_LOG("gstRtpStream sourceId = %s", gstRtpStream->m_baseSourceId.ascii().data());

    gstRtpStream->constructGStreamerSource();

    String rtpRecvSinkName = "recv_rtp_sink_";
    rtpRecvSinkName.append(rtpSessionString);
    String rtcpRecvSinkName = "recv_rtcp_sink_";
    rtcpRecvSinkName.append(rtpSessionString);
    String ghostSrcName = "src_";
    ghostSrcName.append(rtpSessionString);

    String rtcpSendSrcName = "send_rtcp_src_";
    rtcpSendSrcName.append(rtpSessionString);


    GstElement* niceRtpSrc = 0;
    GstElement* niceRtcpSrc = 0;
    GstElement* niceRtcpSink = 0;

    gchar* niceRtpSrcName = createUniqueName("niceRtpSrc");
    niceRtpSrc = m_iceAgent->createGstSrc(niceRtpSrcName, gstRtpStream->streamId(), IceAgent::RTP_COMPONENT);
    delete niceRtpSrcName;

    gchar* niceRtcpSrcName = createUniqueName("niceRtcpSrc");
    niceRtcpSrc = m_iceAgent->createGstSrc(niceRtcpSrcName, gstRtpStream->streamId(), IceAgent::RTCP_COMPONENT);
    delete niceRtcpSrcName;

    gchar* niceRtcpSinkName = createUniqueName("niceRtcpSink");
    niceRtcpSink = m_iceAgent->createGstSink(niceRtcpSinkName, gstRtpStream->streamId(), IceAgent::RTCP_COMPONENT);
    delete niceRtcpSinkName;

    GstElement* q1 = gst_element_factory_make("queue", NULL);
    GstElement* q2 = gst_element_factory_make("queue", NULL);
    GstElement* q3 = gst_element_factory_make("queue", NULL);
    GstPad* q1SrcPad = gst_element_get_static_pad(q1, "src");
    GstPad* q1SinkPad = gst_element_get_static_pad(q1, "sink");
    GstPad* q2SrcPad = gst_element_get_static_pad(q2, "src");
    GstPad* q2SinkPad = gst_element_get_static_pad(q2, "sink");
    GstPad* q3SrcPad = gst_element_get_static_pad(q3, "src");
    GstPad* q3SinkPad = gst_element_get_static_pad(q3, "sink");
    gstRtpStream->m_rtpNiceSrcQueue = q1;
    gstRtpStream->m_rtcpNiceSrcQueue = q3;
    gstRtpStream->m_rtcpNiceSinkQueue = q2;

    gst_bin_add_many(GST_BIN(m_transportBin), niceRtpSrc, niceRtcpSrc, niceRtcpSink, q1, q2, q3, NULL);

    GstPad* rtpNiceSrcPad = gst_element_get_static_pad(niceRtpSrc, "src");
    GstPad* rtpRecvSinkPad = gst_element_get_request_pad(m_rtpBin, rtpRecvSinkName.ascii().data());
    RTC_LOG("Trying to connect rtpNiceSrcPad %p with rtpRecvSinkPad %p", rtpNiceSrcPad, rtpRecvSinkPad);

    GstPadLinkReturn linkRes;
    linkRes = gst_pad_link(rtpNiceSrcPad, q1SinkPad);
    if (linkRes != GST_PAD_LINK_OK) RTC_LOG("link failed");
    ASSERT(linkRes == GST_PAD_LINK_OK);
    linkRes = gst_pad_link(q1SrcPad, rtpRecvSinkPad);
    if (linkRes != GST_PAD_LINK_OK) RTC_LOG("link failed");
    ASSERT(linkRes == GST_PAD_LINK_OK);

    gst_object_unref(rtpNiceSrcPad);
    gst_object_unref(rtpRecvSinkPad);

    GstPad* niceRtcpSinkPad = gst_element_get_static_pad(niceRtcpSink, "sink");
    GstPad* rtcpSendSrcPad = gst_element_get_request_pad(m_rtpBin, rtcpSendSrcName.ascii().data());
    RTC_LOG("Trying to connect rtcpSendSrcPad %p with niceRtcpSinkPad %p", rtcpSendSrcPad, niceRtcpSinkPad);

    linkRes = gst_pad_link(rtcpSendSrcPad, q2SinkPad);
    if (linkRes != GST_PAD_LINK_OK) RTC_LOG("link failed");
    ASSERT(linkRes == GST_PAD_LINK_OK);
    linkRes = gst_pad_link(q2SrcPad, niceRtcpSinkPad);
    if (linkRes != GST_PAD_LINK_OK) RTC_LOG("link failed");
    ASSERT(linkRes == GST_PAD_LINK_OK);

    gst_object_unref(niceRtcpSinkPad);
    gst_object_unref(rtcpSendSrcPad);

    GstPad* rtcpNiceSrcPad = gst_element_get_static_pad(niceRtcpSrc, "src");
    GstPad* rtcpRecvSinkPad = gst_element_get_request_pad(m_rtpBin, rtcpRecvSinkName.ascii().data());
    RTC_LOG("Trying to connect rtcpNiceSrcPad %p with rtcpRecvSinkPad %p", rtcpNiceSrcPad, rtcpRecvSinkPad);

    gst_element_sync_state_with_parent(niceRtpSrc);
    gst_element_sync_state_with_parent(niceRtcpSrc);

    linkRes = gst_pad_link(rtcpNiceSrcPad, q3SinkPad);
    ASSERT(linkRes == GST_PAD_LINK_OK);
    linkRes = gst_pad_link(q3SrcPad, rtcpRecvSinkPad);
    ASSERT(linkRes == GST_PAD_LINK_OK);

    gst_object_unref(rtcpNiceSrcPad);
    gst_object_unref(rtcpRecvSinkPad);

    RTC_LOG("gstRtpStream = %p", gstRtpStream);

    gstRtpStream->m_rtpNiceSrc = niceRtpSrc;
    gstRtpStream->m_rtpNiceSrcPad = rtpNiceSrcPad;
    gstRtpStream->m_rtpBinRecvRtpSinkPad = rtpRecvSinkPad;

    gstRtpStream->m_rtcpNiceSink = niceRtcpSink;
    gstRtpStream->m_rtcpNiceSrcPad = rtcpNiceSrcPad;
    gstRtpStream->m_rtpBinRecvRtcpSinkPad = rtcpRecvSinkPad;

    gstRtpStream->m_rtcpNiceSrc = niceRtcpSrc;

    gstRtpStream->m_rtpBinSendRtcpSrcPad = rtcpSendSrcPad;
    gstRtpStream->m_rtcpNiceSinkQueue = q2;

    gst_element_sync_state_with_parent(m_transportBin);
}

void PeerConnectionHandlerPrivateGStreamer::onIceStateChanged(guint streamId, guint componentId, guint state)
{
    RTC_LOG("%p onIceStateChange called. streamId = %d, componentId = %d, state = %d", this, streamId, componentId, state);

    switch (state) {
        case NICE_COMPONENT_STATE_READY:
            m_componentsNotReady--;
            RTC_LOG("m_componentsNotReady is %d", m_componentsNotReady);
            if (m_componentsNotReady == 0) {
                RTC_LOG("Calling iceProcessCompleted()");
                //m_peerConnectionHandlerClient->didCompleteICEProcessing();
                RTC_LOG("iceProcessCompleted() returned");
            }
            break;
        case NICE_COMPONENT_STATE_FAILED:
            RTC_LOG("Component %d in stream %d failed", componentId, streamId);
            m_componentsNotReady--;
            break;
        case NICE_COMPONENT_STATE_DISCONNECTED:
            // FIXME: what should we do here?
            RTC_LOG("Component %d in stream %d disconnected", componentId, streamId);
            break;
    }
}

void PeerConnectionHandlerPrivateGStreamer::onIceNewSelectedPair(guint stream_id, guint component_id, gchar *lfoundation, gchar *rfoundation)
{
    RTC_LOG("[%p] PeerConnectionHandlerPrivateGStreamer::onIceNewSelectedPair", this);
    RTC_LOG("%p stream_id=%d, component_id=%d, lfoundation=%s, rfoundation=%s", this, stream_id, component_id, lfoundation, rfoundation);
    gboolean controllingMode = false;
    controllingMode = m_iceAgent->controllingMode();
    //RTC_LOG("%p controllingMode=%d", this, controllingMode);
    if (controllingMode) {
        StreamIdGstRtpStreamMap::iterator nsIt = m_streamIdGstRtpStreamMap.find(stream_id);

        if (nsIt == m_streamIdGstRtpStreamMap.end()) {
            //RTC_LOG("did not find streamId in m_streamIdGstRtpStreamMap");
            return;
        }

        GstRtpStream* gstRtpStream = nsIt->value;
        //RTC_LOG("default lfoundation=%s, rfoundation=%s", gstRtpStream->m_defaultLocalFoundation.ascii().data(), gstRtpStream->m_defaultRemoteFoundation.ascii().data());
        if (gstRtpStream->m_defaultLocalFoundation != lfoundation || gstRtpStream->m_defaultRemoteFoundation != rfoundation) {
            //RTC_LOG("Selected pair and default mismatch, must send update!");
            //TODO Send update
        }
    }
}

void PeerConnectionHandlerPrivateGStreamer::onIceReceive(guint stream_id, guint component_id, guint len, gchar *buf)
{
    RTC_LOG("ICE receive");
    UNUSED_PARAM(stream_id);
    UNUSED_PARAM(component_id);
    UNUSED_PARAM(len);
    UNUSED_PARAM(buf);
}

void PeerConnectionHandlerPrivateGStreamer::setRemoteCandidates(GstMediaStream* gstMediaStream)
{
    RTC_LOG("[%p] PeerConnection - Set remote candidates", this);

    Vector<GstRtpStream*>& gstRtpStreamVector = gstMediaStream->rtpStreams();
    Vector<GstRtpStream*>::iterator gstRtpStreamIt = gstRtpStreamVector.begin();
    while (gstRtpStreamIt != gstRtpStreamVector.end()) {
        RTC_LOG("Checking a gstRtpStream for candidates");
        GstRtpStream* gstRtpStream = *gstRtpStreamIt;
        gstRtpStreamIt++;
        guint streamId = gstRtpStream->streamId();
        m_iceAgent->setRemoteCredentials(streamId, gstRtpStream->m_username.utf8().data(), gstRtpStream->m_password.utf8().data());
        m_iceAgent->setRemoteCandidates(streamId, IceAgent::RTP_COMPONENT, gstRtpStream->rtpCandidates());
        m_iceAgent->setRemoteCandidates(streamId, IceAgent::RTCP_COMPONENT, gstRtpStream->rtcpCandidates());
    }
}


// FIXME: This is not the preferred algorithm
IceAgent::Candidate selectDefaultCandidate(IceAgent::CandidateVector localCandidates) {
    IceAgent::CandidateVector::iterator candidateIt = localCandidates.begin();
    IceAgent::Candidate defaultCandidate;
    guint prio = 0;
    while (candidateIt != localCandidates.end()) {
        IceAgent::Candidate iceCandidate = *candidateIt;
        candidateIt++;
        if (iceCandidate.prio > prio) {
            defaultCandidate = iceCandidate;
            prio = iceCandidate.prio;
        }
    }
    return defaultCandidate;
}

static IceAgent::Candidate sdpCand2IceCand(Candidate* candidate)
{
    IceAgent::Candidate nsCand;

    nsCand.foundation = candidate->foundation();
    nsCand.address = candidate->address();
    nsCand.port = candidate->port();
    nsCand.prio = candidate->priority();
    Candidate::CandidateType type = candidate->candidateType();
    if (type == Candidate::ServerReflexive)
        nsCand.type = IceAgent::CANDIDATE_TYPE_SERVER_REFLEXIVE;
    else if (type == Candidate::PeerReflexive)
        nsCand.type = IceAgent::CANDIDATE_TYPE_PEER_REFLEXIVE;
    else if (type == Candidate::Relayed)
        nsCand.type = IceAgent::CANDIDATE_TYPE_RELAYED;
    else // (type == Host)
        nsCand.type = IceAgent::CANDIDATE_TYPE_HOST;

    return nsCand;
}

void PeerConnectionHandlerPrivateGStreamer::setDataRemoteCandidates()
{
    IceAgent::CandidateVector candidates;
    if (!m_dataRemoteCandidates.size())
        return;

    for(size_t i = 0; i < m_dataRemoteCandidates.size(); i++) {
        candidates.append(sdpCand2IceCand(m_dataRemoteCandidates[i]));
    }

    m_iceAgent->setRemoteCredentials(m_dataStreamId, m_dataRemoteUsername.utf8().data(), m_dataRemotePassword.utf8().data());
    m_iceAgent->setRemoteCandidates(m_dataStreamId, IceAgent::RTP_COMPONENT, candidates);
    m_dataRemoteCandidates.clear();
}

void PeerConnectionHandlerPrivateGStreamer::addIceAttributes(MediaDescription* md, guint streamId)
{
    Vector<Candidate*>& candidates = md->candidates();
    char *username = 0;
    char *password = 0;
    m_iceAgent->getLocalCredentials(streamId, &username, &password);
    md->setUsername(username);
    md->setPassword(password);

    for (guint component = 1; component <= ((streamId == m_dataStreamId) ? 1 : 2); component++) {
        IceAgent::CandidateVector localCandidates = m_iceAgent->getLocalCandidates(streamId, component);
        IceAgent::CandidateVector::iterator candidateIt = localCandidates.begin();
        guint size = localCandidates.size();
        RTC_LOG("component %d: %d candidates found", component, size);
        while (candidateIt != localCandidates.end()) {
            IceAgent::Candidate iceCandidate = *candidateIt;
            candidateIt++;
            Candidate* candidate = new Candidate();

            candidate->setAddress(iceCandidate.address);
            candidate->setPort(iceCandidate.port);
            candidate->setFoundation(iceCandidate.foundation);
            candidate->setComponentId(component);
            candidate->setPriority(iceCandidate.prio);

            switch (iceCandidate.type) {
            case NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE:
                candidate->setCandidateType(Candidate::ServerReflexive);
                break;
            case NICE_CANDIDATE_TYPE_PEER_REFLEXIVE:
                candidate->setCandidateType(Candidate::PeerReflexive);
                break;
            case NICE_CANDIDATE_TYPE_RELAYED:
                candidate->setCandidateType(Candidate::Relayed);
                break;
            case NICE_CANDIDATE_TYPE_HOST:
                candidate->setCandidateType(Candidate::Host);
                break;
            }
            candidates.append(candidate);
        }
    }
}

void PeerConnectionHandlerPrivateGStreamer::addDataMediaDescriptionToSignalingMessage(SignalingMessage* signalingMessage)
{
    RTC_LOG("[%p] PeerConnection - Construct SignalingMessage", this);
    Vector<MediaDescription*>& mediaDescriptions = signalingMessage->mediaDescriptions();
    MediaDescription* md = new MediaDescription();
    mediaDescriptions.append(md);
    md->setMediaTransportType(MediaDescription::UDP);
    md->setMediaType(MediaDescription::Text);
    md->setDirection(MediaDescription::SendRecv);
    md->setLabel("data");
    addIceAttributes(md, m_dataStreamId);

    IceAgent::Candidate defaultCandidate = selectDefaultCandidate(m_iceAgent->getLocalCandidates(m_dataStreamId, IceAgent::RTP_COMPONENT));
    md->setPort(defaultCandidate.port);
    md->setConnectionAddress(defaultCandidate.address);
}

void PeerConnectionHandlerPrivateGStreamer::addMediaDescriptionToSignalingMessage(SignalingMessage* signalingMessage, GstMediaStream* gstMediaStream)
{
    RTC_LOG("[%p] PeerConnection - Construct SignalingMessage", this);

    Vector<GstRtpStream*>& gstRtpStreams = gstMediaStream->rtpStreams();
    Vector<MediaDescription*>& mediaDescriptions = signalingMessage->mediaDescriptions();

    RTC_LOG("addMediaDescriptionToSignalingMessage: %u streams", gstRtpStreams.size());
    Vector<GstRtpStream*>::iterator nsIt = gstRtpStreams.begin();
    while (nsIt != gstRtpStreams.end()) {
        GstRtpStream* ns = *nsIt;
        nsIt++;

        MediaDescription* md = createMediaDescription(ns);
        mediaDescriptions.append(md);
        guint streamId = ns->streamId();
        addIceAttributes(md, streamId);

        IceAgent::Candidate defaultCandidate = selectDefaultCandidate(m_iceAgent->getLocalCandidates(streamId, IceAgent::RTP_COMPONENT));
        if (gstMediaStream->closed()) {
            md->setPort(0);
        } else {
            md->setPort(defaultCandidate.port);
            md->setConnectionAddress(defaultCandidate.address);
        }
        ns->m_defaultLocalFoundation = defaultCandidate.foundation;
    }
}

MediaDescription* PeerConnectionHandlerPrivateGStreamer::createMediaDescription(GstRtpStream* gstRtpStream)
{
    MediaDescription* md = new MediaDescription();
    GstMediaStream* gstMediaStream = gstRtpStream->gstMediaStream();
    GstRtpStream* ns = gstRtpStream;
    Vector<Payload*>& payloads = md->payloads();

    if (ns->type() == GstRtpStream::audio)
        md->setMediaType(MediaDescription::Audio);
    else
        md->setMediaType(MediaDescription::Video);

    if (gstMediaStream->type() == GstMediaStream::Local)
        md->setDirection(MediaDescription::SendOnly);
    else if (gstMediaStream->type() == GstMediaStream::Remote)
        md->setDirection(MediaDescription::RecvOnly);

    md->setMediaTransportType(MediaDescription::RTP_AVPF);
    md->setLabel(gstMediaStream->label());
    md->setMaximumBandwidth(gstRtpStream->maxBandwidth());

    if (ns->type() == GstRtpStream::audio || ns->type() == GstRtpStream::video) {
        Vector<Codec*> codecVec;
        if (gstMediaStream->type() == GstMediaStream::Local) {
            if (ns->type() == GstRtpStream::audio)
                codecVec = m_codecRegistry.getAudioCodecList();
            else
                codecVec = m_codecRegistry.getVideoCodecList();
            Vector<Codec*>::iterator cIt = codecVec.begin();
            while (cIt != codecVec.end()) {
                Codec* codec = *cIt;
                cIt++;
                Payload* payload = new Payload();
                payload->setCodecName(codec->name());
                payload->setRate(codec->rate());
                payload->setPayloadTypeNumber(codec->payloadTypeNumber());
                payload->setFormat(codec->fmtp());
                payload->setChannels(codec->channels());

                RTC_LOG("Setting framesizeWidth to %u and framesizeHeight to %u n payload", ns->framesizeWidth(), ns->framesizeHeight());
                payload->setFramesizeWidth(ns->framesizeWidth());
                payload->setFramesizeHeight(ns->framesizeHeight());

                payloads.append(payload);

            }
        } else if (gstMediaStream->type() == GstMediaStream::Remote) {
            if (ns->m_negotiatedCodec) {
                Codec* codec = ns->m_negotiatedCodec;
                Payload* payload = new Payload();
                payload->setCodecName(codec->name());
                payload->setRate(codec->rate());
                payload->setPayloadTypeNumber(codec->payloadTypeNumber());
                payload->setFormat(codec->fmtp());
                payload->setChannels(codec->channels());

                RTC_LOG("Setting framesizeWidth to %u and framesizeHeight to %u n payload", ns->framesizeWidth(), ns->framesizeHeight());
                payload->setFramesizeWidth(ns->framesizeWidth());
                payload->setFramesizeHeight(ns->framesizeHeight());

                payloads.append(payload);
            }
        } else {
            RTC_LOG("sendrecv currently not supported.. something will go wrong from here...");
            // FIXME: Can we handle this nicely?
        }
    }
    return md;
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)
