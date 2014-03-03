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

#ifndef CallbackProxy_h
#define CallbackProxy_h

#if ENABLE(MEDIA_STREAM)

#include <gst/gst.h>
#include <wtf/HashMap.h>
#include <wtf/MainThread.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

class CentralPipelineUnit;
class PeerHandlerPrivateGStreamer;

class CallbackProxyClient
{
public:
    virtual void sourceInserted(GstElement* source, GstPad* pad) = 0;
    virtual void onPadAdded(GstElement* element, GstPad* newPad) = 0;
    virtual void onPadUnlinked(GstPad* pad, GstPad* peer) = 0;
    virtual GstCaps* onRequestPtMap(GstElement* element, guint session, guint pt) = 0;
    virtual gboolean onEnableOrdering(guint session, guint ssrc) = 0;
    virtual guint onEnableRtx(guint session, guint ssrc, guint pt) = 0;
    virtual void onFir(GstElement* bin, gint id, guint32 media_ssrc, guint32 csqn) = 0;
};

class CallbackProxy : private CallbackProxyClient
{
public:
    CallbackProxy(CallbackProxyClient*);
    ~CallbackProxy();

    void insertSource(CentralPipelineUnit*, GstElement* source, GstPad* pad, const String& sourceId);
    gulong connectPadAddedSignal(GstElement*);
    gulong connectPadUnlinkedSignal(GstPad*);
    gulong connectRequestPtMapSignal(GstElement*);
    gulong connectOnEnableOrdering(GstElement*);
    gulong connectOnEnableRtx(GstElement*);
    gulong connectOnFirSignal(GstElement*);
    gulong connectToRequestEncryptionContextSignal(GstElement*);

private:
    CallbackProxyClient* m_client;
    gulong m_handle;

public:
    void sourceInserted(GstElement* source, GstPad* pad);
    void onPadAdded(GstElement* element, GstPad* newPad);
    void onPadUnlinked(GstPad* pad, GstPad* peer);
    GstCaps* onRequestPtMap(GstElement* element, guint session, guint pt);
    gboolean onEnableOrdering(guint session, guint ssrc);
    guint onEnableRtx(guint session, guint ssrc, guint pt);
    void onFir(GstElement* bin, gint id, guint32 media_ssrc, guint32 csqn);

public:
    typedef HashMap<int, CallbackProxy*> CallbackProxyMap;
    static CallbackProxyMap clientMap;
    static gulong save(CallbackProxy* client) {
        ASSERT(isMainThread());
        static gulong handle = 1;
        clientMap.add(handle, client);
        return handle++;
    }
    static CallbackProxy* load(gulong handle) {
        ASSERT(isMainThread());
        return clientMap.get(handle);
    }
    static CallbackProxy* remove(gulong handle) {
        ASSERT(isMainThread());
        return clientMap.take(handle);
    }

};

}

#endif // ENABLE(MEDIA_STREAM)

#endif // CallbackProxy_h
