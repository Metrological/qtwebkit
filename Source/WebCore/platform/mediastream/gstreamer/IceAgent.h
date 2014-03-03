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


#ifndef IceAgent_h
#define IceAgent_h

#include <gst/gst.h>
#include <wtf/HashMap.h>
#include <wtf/MainThread.h>
#include <wtf/text/WTFString.h>

#if ENABLE(MEDIA_STREAM) && USE(NICE)


struct _NiceAgent;

namespace WebCore
{

class IceAgentClient
{
public:
    virtual void onIceGatheringDone(guint streamId) = 0;
    virtual void onIceStateChanged(guint streamId, guint componentId, guint state) = 0;
    virtual void onIceNewSelectedPair(guint streamId, guint componentId, gchar *leftFoundation, gchar *rightFoundation) = 0;
    virtual void onIceReceive(guint streamId, guint componentId, guint length, gchar* buffer) = 0;
};

class IceAgent
{
public:

    enum {
        RTP_COMPONENT = 1,
        RTCP_COMPONENT = 2
    };

    typedef enum
    {
      CANDIDATE_TYPE_HOST,
      CANDIDATE_TYPE_SERVER_REFLEXIVE,
      CANDIDATE_TYPE_PEER_REFLEXIVE,
      CANDIDATE_TYPE_RELAYED,
    } CandidateType;

    struct Candidate {
        Candidate()
        : port(0)
        , type(CANDIDATE_TYPE_HOST)
        , prio(0)
        {}
        String foundation;
        String address;
        guint port;
        CandidateType type;
        guint prio;
    };
    typedef Vector<Candidate> CandidateVector;

    IceAgent();
    virtual ~IceAgent();

    void setClient(IceAgentClient *client)
    {
        m_client = client;
    }

    void setStunServer(const char* addr, int port);
    gboolean setRelayInfo(guint stream_id,
                          guint component_id,
                          const gchar *server_ip,
                          guint server_port,
                          const gchar *username,
                          const gchar *password);

    guint addStream(guint n_components);
    void removeStream(guint stream_id);

    gboolean attachReceive(guint stream_id,
                           guint component_id);
    gboolean attachIgnore(guint stream_id,
                          guint component_id);

    gboolean gatherCandidates(guint stream_id);
    gboolean setRemoteCredentials(guint stream_id,
                                  const gchar *ufrag,
                                  const gchar *pwd);
    gboolean getLocalCredentials(guint stream_id,
                                 gchar **ufrag,
                                 gchar **pwd);

    int setRemoteCandidates(guint stream_id,
                            guint component_id,
                            CandidateVector& candidates);
    CandidateVector getRemoteCandidates(guint stream_id,
                                        guint component_id);
    CandidateVector getLocalCandidates(guint stream_id,
                                       guint component_id);

    gint send(guint stream_id,
              guint component_id,
              guint len,
              const gchar *buf);

    gboolean setSelectedRemoteCandidate(guint stream_id,
                                        guint component_id,
                                        Candidate candidate);

    gboolean controllingMode();

    GstElement* createGstSink(gchar* name, guint stream_id, guint component_id);
    GstElement* createGstSrc(gchar* name, guint stream_id, guint component_id);

public:
    void onIceGatheringDone(guint stream_id);
    void onIceStateChanged(guint stream_id, guint component_id, guint state);
    void onIceNewSelectedPair(guint stream_id, guint component_id, gchar *lfoundation, gchar *rfoundation);
    void onIceReceive(guint stream_id, guint component_id, guint len, gchar *buf);
private:
    struct _NiceAgent* m_niceAgent;
    GMainContext* m_gMainContext;
    IceAgentClient* m_client;
    gulong m_handle;

    gulong m_gatheringDoneSignalHandler;
    gulong m_stateChangedSignalHandler;
    gulong m_newSelectedPairSignalHandler;
public:
    typedef HashMap<int, IceAgent*> ClientMap;
    static ClientMap clientMap;
    static gulong save(IceAgent* client) {
        ASSERT(isMainThread());
        static gulong handle = 1;
        clientMap.add(handle, client);
        return handle++;
    }
    static IceAgent* load(gulong handle) {
        ASSERT(isMainThread());
        return clientMap.get(handle);
    }
    static IceAgent* remove(gulong handle) {
        ASSERT(isMainThread());
        return clientMap.take(handle);
    }
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(NICE)

#endif // IceAgent_h
