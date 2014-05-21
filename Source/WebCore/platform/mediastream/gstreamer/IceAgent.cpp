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

#if ENABLE(MEDIA_STREAM) && USE(NICE)

#include "IceAgent.h"

//#include "Logging.h"
//#include "gstbufferednicesrc.h"

#include <nice/agent.h>
#include <wtf/text/CString.h>
#include <stdio.h>

#define RTC_LOG(fmt, args...) printf(fmt "\n", ##args)
#define RTC_LOG_FENTER() (void) 0
#define RTC_LOG_FLEAVE() (void) 0

namespace WebCore {

typedef struct {
    guint streamId;
    gulong handle;
} IceGatheringDoneHelperParameters;

typedef struct {
    guint streamId;
    guint componentId;
    guint state;
    gulong handle;
} IceStateChangeHelperParameters;

typedef struct {
    guint streamId;
    guint componentId;
    gchar* leftFoundation;
    gchar* rightFoundation;
    gulong handle;
} IceNewSelectedPairHelperParameters;


static void onIceGatheringDoneHelperMT(void* pIceGatheringDoneHelperParameters);
static void onIceStateChangeHelperMT(void* pIceStateChangeHelperParameters);
static void onIceNewSelectedPairHelperMT(void* pIceNewSelectedPairHelperParameters);

static void onIceGatheringDoneHelper(NiceAgent*, guint streamId, gpointer user_data);
static void onIceStateChangeHelper(NiceAgent *agent, guint streamId, guint componentId, guint state, gpointer user_data);
static void onIceNewSelectedPairHelper(NiceAgent *agent, guint streamId, guint componentId, gchar* leftFoundation, gchar* rightFoundation,gpointer user_data);

static void onIceReceiveHelper(NiceAgent *agent, guint streamId, guint componentId, guint len, gchar *buf, gpointer user_data);
static void onIceIgnoreHelper(NiceAgent *agent, guint streamId, guint componentId, guint len, gchar *buf, gpointer user_data);

IceAgent::ClientMap IceAgent::clientMap;

IceAgent::IceAgent()
    : m_client(0)
{
    ASSERT(isMainThread());
    m_gMainContext = g_main_context_default();
    if (!m_gMainContext)
        RTC_LOG("GMainContext is NULL!");

    m_niceAgent = nice_agent_new(m_gMainContext, NICE_COMPATIBILITY_RFC5245);
    g_object_set(m_niceAgent, "upnp", FALSE, NULL);
#if 0
    NiceAddress* na = nice_address_new();
    nice_address_set_from_string(na, "127.0.0.1");
    nice_agent_add_local_address(m_niceAgent, na);
#endif
    m_handle = save(this);
    m_gatheringDoneSignalHandler = g_signal_connect (G_OBJECT(m_niceAgent), "candidate-gathering-done",
                          G_CALLBACK (onIceGatheringDoneHelper), (gpointer)m_handle);
    m_stateChangedSignalHandler = g_signal_connect (G_OBJECT(m_niceAgent), "component-state-changed",
                          G_CALLBACK (onIceStateChangeHelper), (gpointer)m_handle);
    m_newSelectedPairSignalHandler = g_signal_connect (G_OBJECT(m_niceAgent), "new-selected-pair",
                          G_CALLBACK (onIceNewSelectedPairHelper), (gpointer)m_handle);
}

IceAgent::~IceAgent()
{
    ASSERT(isMainThread());
    remove(m_handle);
    g_signal_handler_disconnect(m_niceAgent, m_gatheringDoneSignalHandler);
    g_signal_handler_disconnect(m_niceAgent, m_stateChangedSignalHandler);
    g_signal_handler_disconnect(m_niceAgent, m_newSelectedPairSignalHandler);
    g_object_unref(m_niceAgent);
    m_niceAgent = 0;
}

void IceAgent::setStunServer(const char* addr, int port)
{
    ASSERT(m_niceAgent);
    ASSERT(addr);

    if (addr && port)
        g_object_set(m_niceAgent, "stun-server", addr, "stun-server-port", port, NULL);
}

gboolean IceAgent::setRelayInfo(guint streamId,
        guint componentId,
        const gchar *server_ip,
        guint server_port,
        const gchar *username,
        const gchar *password)
{
    return nice_agent_set_relay_info(m_niceAgent, streamId, componentId, server_ip, server_port, username, password, NICE_RELAY_TYPE_TURN_UDP);
}

guint IceAgent::addStream(guint n_components)
{
    guint id = nice_agent_add_stream(m_niceAgent, n_components);
#if 0
    static guint rtpPort = 0;

    if (rtpPort == 0) {
        const gchar* p = g_getenv("WEBKIT_PC_FIXEDRTCPPORT");
        if (p) rtpPort= strtol(p, NULL, 10);
    }

    if (rtpPort >= 49152) {
        for(guint i = 0; i < n_components; i++) {
            nice_agent_set_port_range(m_niceAgent, id, i + 1, rtpPort + i, rtpPort + i);
        }
        rtpPort += 2;
    }
#endif
    return id;
}

void IceAgent::removeStream(guint streamId)
{
    nice_agent_remove_stream(m_niceAgent, streamId);
}

gboolean IceAgent::attachReceive(guint streamId,
                                 guint componentId)
{
    return nice_agent_attach_recv(m_niceAgent, streamId, componentId, m_gMainContext, onIceReceiveHelper, (gpointer)m_handle);
}

gboolean IceAgent::attachIgnore(guint streamId,
                                guint componentId)
{
    return nice_agent_attach_recv(m_niceAgent, streamId, componentId, m_gMainContext, onIceIgnoreHelper, (gpointer)m_handle);
}

gboolean IceAgent::gatherCandidates(guint streamId)
{
    return nice_agent_gather_candidates(m_niceAgent, streamId);
}

gboolean IceAgent::setRemoteCredentials(guint streamId,
                                  const gchar *ufrag,
                                  const gchar *pwd)
{
    return nice_agent_set_remote_credentials(m_niceAgent, streamId, ufrag, pwd);
}

gboolean IceAgent::getLocalCredentials(guint streamId,
                                 gchar **ufrag,
                                 gchar **pwd)
{
    return nice_agent_get_local_credentials(m_niceAgent, streamId, ufrag, pwd);
}

static NiceCandidateType typeToNiceType(IceAgent::CandidateType type) {
    switch(type) {
    case IceAgent::CANDIDATE_TYPE_HOST: return NICE_CANDIDATE_TYPE_HOST;
    case IceAgent::CANDIDATE_TYPE_SERVER_REFLEXIVE: return NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE;
    case IceAgent::CANDIDATE_TYPE_PEER_REFLEXIVE: return NICE_CANDIDATE_TYPE_PEER_REFLEXIVE;
    case IceAgent::CANDIDATE_TYPE_RELAYED: return NICE_CANDIDATE_TYPE_RELAYED;
    }
    ASSERT_NOT_REACHED();
    return NICE_CANDIDATE_TYPE_HOST;
}

static IceAgent::CandidateType niceTypeToType(NiceCandidateType type) {
    switch(type) {
    case NICE_CANDIDATE_TYPE_HOST: return IceAgent::CANDIDATE_TYPE_HOST;
    case NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE: return IceAgent::CANDIDATE_TYPE_SERVER_REFLEXIVE;
    case NICE_CANDIDATE_TYPE_PEER_REFLEXIVE: return IceAgent::CANDIDATE_TYPE_PEER_REFLEXIVE;
    case NICE_CANDIDATE_TYPE_RELAYED: return IceAgent::CANDIDATE_TYPE_RELAYED;
    }
    ASSERT_NOT_REACHED();
    return IceAgent::CANDIDATE_TYPE_HOST;
}

int IceAgent::setRemoteCandidates(guint streamId,
                                  guint componentId,
                                  CandidateVector& candidates)
{
    GSList *remoteCandidates = NULL;
    CandidateVector::iterator candidateIt = candidates.begin();
    while (candidateIt != candidates.end()) {
        Candidate candidate = *candidateIt;
        candidateIt++;
        NiceCandidate *nicecandidate = nice_candidate_new(typeToNiceType(candidate.type));
        nice_address_set_from_string(&nicecandidate->addr, candidate.address.utf8().data());
        nice_address_set_port(&nicecandidate->addr,(unsigned short) candidate.port);
        nicecandidate->priority = (guint) candidate.prio;
        strncpy(nicecandidate->foundation, candidate.foundation.utf8().data(), NICE_CANDIDATE_MAX_FOUNDATION);
        remoteCandidates = g_slist_append(remoteCandidates, nicecandidate);
    }
    int r = nice_agent_set_remote_candidates(m_niceAgent, streamId, componentId, remoteCandidates);
    g_slist_free(remoteCandidates);
    return r;
}

IceAgent::CandidateVector IceAgent::getRemoteCandidates(guint streamId,
                                                        guint componentId)
{
    CandidateVector remoteCandidates;
    GSList *item = nice_agent_get_remote_candidates(m_niceAgent, streamId, componentId);
    guint size = g_slist_length(item);
    if (size != 0) {
        for (; item ; item = g_slist_next(item)) {
           NiceCandidate *niceCandidate = (NiceCandidate *) item->data;

           if (!niceCandidate || niceCandidate->addr.s.addr.sa_family != AF_INET)
               continue; // FIXME: we avoid non-ipv4 for now

           Candidate candidate;

           char address_string[NICE_ADDRESS_STRING_LEN];
           nice_address_to_string(&niceCandidate->addr, address_string);
           unsigned short port = nice_address_get_port(&niceCandidate->addr);

           candidate.address = address_string;
           candidate.port = port;
           candidate.foundation = niceCandidate->foundation;
           candidate.prio = niceCandidate->priority;
           candidate.type = niceTypeToType(niceCandidate->type);
           remoteCandidates.append(candidate);
        }
    }    return remoteCandidates;
}

IceAgent::CandidateVector IceAgent::getLocalCandidates(guint streamId,
                                                        guint componentId)
{
    CandidateVector localCandidates;
    GSList *item = nice_agent_get_local_candidates(m_niceAgent, streamId, componentId);
    guint size = g_slist_length(item);
    if (size != 0) {
        for (; item ; item = g_slist_next(item)) {
           NiceCandidate *niceCandidate = (NiceCandidate *) item->data;

           if (!niceCandidate || niceCandidate->addr.s.addr.sa_family != AF_INET)
               continue; // FIXME: we avoid non-ipv4 for now

           Candidate candidate;

           char address_string[NICE_ADDRESS_STRING_LEN];
           nice_address_to_string(&niceCandidate->addr, address_string);
           unsigned short port = nice_address_get_port(&niceCandidate->addr);

           candidate.address = address_string;
           candidate.port = port;
           candidate.foundation = niceCandidate->foundation;
           candidate.prio = niceCandidate->priority;
           candidate.type = niceTypeToType(niceCandidate->type);
           localCandidates.append(candidate);
        }
    }
    return localCandidates;
}


gint IceAgent::send(guint streamId,
              guint componentId,
              guint len,
              const gchar *buf)
{
    return nice_agent_send(m_niceAgent, streamId, componentId, len, buf);
}

gboolean IceAgent::controllingMode()
{
    gboolean controllingMode = false;
    g_object_get(m_niceAgent, "controlling-mode", &controllingMode, NULL);
    return controllingMode;
}

GstElement* IceAgent::createGstSink(gchar* name, guint streamId, guint componentId)
{
    RTC_LOG_FENTER();
    GstElement* sink = gst_element_factory_make("nicesink", name);
    g_object_set(sink, "agent", m_niceAgent, "stream", streamId, "component", componentId, "async", FALSE, "sync", FALSE, NULL);
    RTC_LOG_FLEAVE();
    return sink;
}

GstElement* IceAgent::createGstSrc(gchar* name, guint streamId, guint componentId)
{
    GstElement* src = gst_element_factory_make("nicesrc", 0);
    //src = gst_bufferednicesrc_new(name);
    g_object_set(src, "agent", m_niceAgent, "stream", streamId, "component", componentId, NULL);
    return src;
}

gboolean IceAgent::setSelectedRemoteCandidate(guint streamId,
                                    guint componentId,
                                    Candidate candidate)
{
    NiceCandidate *nicecandidate = nice_candidate_new(typeToNiceType(candidate.type));
    nice_address_set_from_string(&nicecandidate->addr, candidate.address.utf8().data());
    nice_address_set_port(&nicecandidate->addr,(unsigned short) candidate.port);
    return nice_agent_set_selected_remote_candidate(m_niceAgent, streamId, componentId, nicecandidate);
}

void IceAgent::onIceGatheringDone(guint streamId)
{
    ASSERT(isMainThread());
    if (m_client) m_client->onIceGatheringDone(streamId);
}

void IceAgent::onIceStateChanged(guint streamId, guint componentId, guint state)
{
    ASSERT(isMainThread());
    if (m_client) m_client->onIceStateChanged(streamId, componentId, state);
}

void IceAgent::onIceNewSelectedPair(guint streamId, guint componentId, gchar *leftFoundation, gchar *rightFoundation)
{
    ASSERT(isMainThread());
    if (m_client) m_client->onIceNewSelectedPair(streamId, componentId, leftFoundation, rightFoundation);
}

void IceAgent::onIceReceive(guint streamId, guint componentId, guint len, gchar *buf)
{
    ASSERT(isMainThread());
    if (m_client) m_client->onIceReceive(streamId, componentId, len, buf);
}

static void onIceGatheringDoneHelperMT(void* pIceGatheringDoneHelperParameters)
{
    ASSERT(isMainThread());
    IceGatheringDoneHelperParameters* params = (IceGatheringDoneHelperParameters*)pIceGatheringDoneHelperParameters;
    IceAgent* pThis = IceAgent::load(params->handle);
    if (pThis) pThis->onIceGatheringDone(params->streamId);
    delete params;
}

static void onIceStateChangeHelperMT(void* pIceStateChangeHelperParameters)
{
    ASSERT(isMainThread());
    IceStateChangeHelperParameters* params = (IceStateChangeHelperParameters*)pIceStateChangeHelperParameters;
    IceAgent* pThis = IceAgent::load(params->handle);
    if (pThis) pThis->onIceStateChanged(params->streamId, params->componentId, params->state);
    delete params;
}

static void onIceNewSelectedPairHelperMT(void* pIceNewSelectedPairHelperParameters)
{
    ASSERT(isMainThread());
    IceNewSelectedPairHelperParameters* params = (IceNewSelectedPairHelperParameters*)pIceNewSelectedPairHelperParameters;
    IceAgent* pThis = IceAgent::load(params->handle);
    if (pThis) pThis->onIceNewSelectedPair(params->streamId, params->componentId, params->leftFoundation, params->rightFoundation);
    delete params;
}

static void onIceGatheringDoneHelper(NiceAgent*, guint streamId, gpointer user_data)
{
    IceGatheringDoneHelperParameters* params = new IceGatheringDoneHelperParameters();

    params->streamId =  streamId;
    params->handle = (gulong) user_data;

    callOnMainThread(onIceGatheringDoneHelperMT, (void*)params);
}

static void onIceStateChangeHelper(NiceAgent*, guint streamId, guint componentId, guint state, gpointer user_data)
{
    IceStateChangeHelperParameters* params = new IceStateChangeHelperParameters();

    params->streamId =  streamId;
    params->componentId =  componentId;
    params->state =  state;
    params->handle = (gulong) user_data;

    callOnMainThread(onIceStateChangeHelperMT, (void*)params);
}

static void onIceNewSelectedPairHelper(NiceAgent*, guint streamId, guint componentId, gchar* leftFoundation, gchar* rightFoundation,gpointer user_data)
{
    IceNewSelectedPairHelperParameters* params = new IceNewSelectedPairHelperParameters();

    params->streamId =  streamId;
    params->componentId =  componentId;
    params->leftFoundation =  leftFoundation;
    params->rightFoundation =  rightFoundation;
    params->handle = (gulong) user_data;

    callOnMainThread(onIceNewSelectedPairHelperMT, (void*)params);
}

static void onIceReceiveHelper(NiceAgent*, guint streamId, guint componentId, guint len, gchar *buf, gpointer user_data)
{
    ASSERT(isMainThread());
    IceAgent* pThis = IceAgent::load((gulong)user_data);
    if (pThis) pThis->onIceReceive(streamId, componentId, len, buf);
}

static void onIceIgnoreHelper(NiceAgent*, guint streamId, guint componentId, guint length, gchar* buffer, gpointer userData)
{
    UNUSED_PARAM(streamId);
    UNUSED_PARAM(componentId);
    UNUSED_PARAM(length);
    UNUSED_PARAM(buffer);
    UNUSED_PARAM(userData);
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(NICE)
