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

#include "CallbackProxy.h"

#include "CentralPipelineUnit.h"
#include "Logging.h"
#include <stdio.h>

#define RTC_LOG(fmt, args...) printf(fmt "\n", ##args)
#define RTC_LOG_FENTER() (void) 0
#define RTC_LOG_FLEAVE() (void) 0

namespace WebCore {

typedef struct {
    GstElement* source;
    GstPad* pad;
    gulong handle;
} SourceInsertedParameters;

typedef struct {
    GstElement* element;
    GstPad* pad;
    gulong handle;
} PadAddedParameters;

typedef struct {
    GstPad* pad;
    GstPad* peer;
    gulong handle;
} PadUnlinkedParameters;

typedef struct {
    GstElement* element;
    guint session;
    guint pt;
    gulong handle;
    GstCaps* requestedPtMap;
} OnRequestPtMapParameters;

typedef struct {
    guint session;
    guint ssrc;
    gulong handle;
    gboolean enableOrdering;
} OnEnableOrderingParameters;

typedef struct {
    guint session;
    guint ssrc;
    guint pt;
    gulong handle;
    guint rtxpt;
} OnEnableRtxParameters;

typedef struct {
    GstElement* bin;
    gint id;
    guint32 media_ssrc;
    guint32 csqn;
    gulong handle;
} FirParameters;

static void onSourceInsertedHelper(GstElement* source, GstPad* pad, gpointer userData);
static void callSourceInsertedMT(void* pSourceInsertedParameters);

static void onPadAddedHelper(GstElement* element, GstPad* newPad, gpointer userData);
static void callOnPadAddedMT(void* pPadAddedParameters);

static void onPadUnlinkedHelper(GstPad* pad, GstPad* peer, gpointer data);
static void callOnPadUnlinkedMT(void* pPadUnlinkedParameters);

static GstCaps* onRequestPtMapHelper(GstElement* element, guint session, guint pt, gpointer data);
static gboolean onEnableOrderingHelper(GstElement* yarsrtpbin, guint session, guint ssrc, gpointer data);
static guint onEnableRtxHelper(GstElement* yarsrtpbin, guint session, guint ssrc, guint pt, gpointer data);
static void onFirHelper(GstElement* bin, gint id, guint32 media_ssrc, guint32 csqn, gpointer data);
static void callOnFirMT(void* pFirParameters);

static void padBlockedHelper(GstPad *, gboolean blocked, gpointer user_data)
{

}

CallbackProxy::CallbackProxyMap CallbackProxy::clientMap;

CallbackProxy::CallbackProxy(CallbackProxyClient* client)
    : m_client(client)
{
    RTC_LOG_FENTER();
    m_handle = save(this);
    RTC_LOG_FLEAVE();
}

CallbackProxy::~CallbackProxy()
{
    RTC_LOG_FENTER();
    remove(m_handle);
    m_client = 0;
    RTC_LOG_FLEAVE();
}

void CallbackProxy::insertSource(CentralPipelineUnit* centralPipelineUnit, GstElement* source, GstPad* pad, const String& sourceId)
{
    RTC_LOG_FENTER();
    centralPipelineUnit->insertSource(source, pad, sourceId, onSourceInsertedHelper, (void*)m_handle);
    RTC_LOG_FLEAVE();
}

gulong CallbackProxy::connectPadAddedSignal(GstElement* element)
{
    return g_signal_connect(G_OBJECT(element), "pad-added", G_CALLBACK (onPadAddedHelper), (void*)m_handle);
}

gulong CallbackProxy::connectPadUnlinkedSignal(GstPad* pad)
{
    return g_signal_connect (G_OBJECT(pad), "unlinked", G_CALLBACK (onPadUnlinkedHelper), (void*)m_handle);
}

gulong CallbackProxy::connectRequestPtMapSignal(GstElement* element)
{
    return g_signal_connect(G_OBJECT(element), "request-pt-map", G_CALLBACK (onRequestPtMapHelper), (void*)m_handle);
}


gulong CallbackProxy::connectOnEnableOrdering(GstElement* element)
{
    RTC_LOG_FENTER();
    gulong handle = g_signal_connect(G_OBJECT(element), "on-enable-ordering", G_CALLBACK (onEnableOrderingHelper), (void*)m_handle);
    RTC_LOG_FLEAVE();
    return handle;
}

gulong CallbackProxy::connectOnEnableRtx(GstElement* element)
{
    RTC_LOG_FENTER();
    gulong handle = g_signal_connect(G_OBJECT(element), "on-enable-rtx", G_CALLBACK (onEnableRtxHelper), (void*)m_handle);
    RTC_LOG_FLEAVE();
    return handle;
}

gulong CallbackProxy::connectOnFirSignal(GstElement* element)
{
    return 0;
    //return g_signal_connect(G_OBJECT(element), "on-fir", (GCallback) onFirHelper, (void*)m_handle);
}


static void onSourceInsertedHelper(GstElement* source, GstPad* pad, gpointer userData)
{
    RTC_LOG_FENTER();
    SourceInsertedParameters* param = new SourceInsertedParameters();
    param->source = source;
    param->pad = pad;
    param->handle = (ulong)userData;
    callOnMainThread(callSourceInsertedMT, (void*)param);
    RTC_LOG_FLEAVE();
}

static void callSourceInsertedMT(void* pSourceInsertedParameters)
{
    RTC_LOG_FENTER();
    ASSERT(isMainThread());
    SourceInsertedParameters* param = (SourceInsertedParameters*)pSourceInsertedParameters;
    CallbackProxy* pThis = CallbackProxy::load(param->handle);
    if (pThis) pThis->sourceInserted(param->source, param->pad);
    RTC_LOG_FLEAVE();
    delete param;
}

void CallbackProxy::sourceInserted(GstElement* source, GstPad* pad)
{
    RTC_LOG_FENTER();
    ASSERT(isMainThread());
    if (m_client) m_client->sourceInserted(source, pad);
    RTC_LOG_FLEAVE();
}

static void onPadAddedHelper(GstElement* element, GstPad* newPad, gpointer userData)
{
    RTC_LOG_FENTER();
    // FIXMEPHIL
    // gst_pad_set_blocked_async(newPad, TRUE, padBlockedHelper, 0);
    PadAddedParameters* param = new PadAddedParameters();
    param->element = element;
    param->pad = newPad;
    param->handle = (gulong) userData;
    callOnMainThread(callOnPadAddedMT, (void*)param);
    RTC_LOG_FLEAVE();
}

static void callOnPadAddedMT(void* pPadAddedParameters)
{
    RTC_LOG_FENTER();
    ASSERT(isMainThread());
    PadAddedParameters* param = (PadAddedParameters*)pPadAddedParameters;
    CallbackProxy* pThis = CallbackProxy::load(param->handle);
    if (pThis) pThis->onPadAdded(param->element, param->pad);
    delete param;
    RTC_LOG_FLEAVE();
}

void CallbackProxy::onPadAdded(GstElement* element, GstPad* newPad)
{
    RTC_LOG_FENTER();
    ASSERT(isMainThread());
    if (m_client) m_client->onPadAdded(element, newPad);
    RTC_LOG_FLEAVE();
}

static void onFirHelper(GstElement* bin, gint id, guint32 media_ssrc, guint32 csqn, gpointer userData)
{
    RTC_LOG_FENTER();
    FirParameters* param = new FirParameters();
    param->bin = bin;
    param->id = id;
    param->media_ssrc = media_ssrc;
    param->csqn = csqn;
    param->handle = (gulong) userData;
    callOnMainThread(callOnFirMT, (void*)param);
    RTC_LOG_FLEAVE();
}

static void callOnFirMT(void* pFirParameters)
{
    RTC_LOG_FENTER();
    ASSERT(isMainThread());
    FirParameters* param = (FirParameters*)pFirParameters;
    CallbackProxy* pThis = CallbackProxy::load(param->handle);
    if (pThis) pThis->onFir(param->bin, param->id, param->media_ssrc, param->csqn);
    delete param;
    RTC_LOG_FLEAVE();
}

void CallbackProxy::onFir(GstElement* bin, gint id, guint32 media_ssrc, guint32 csqn)
{
    RTC_LOG_FENTER();
    ASSERT(isMainThread());
    if (m_client) m_client->onFir(bin, id, media_ssrc, csqn);
    RTC_LOG_FLEAVE();
}

static void onPadUnlinkedHelper(GstPad* pad, GstPad* peer, gpointer data)
{
    RTC_LOG_FENTER();
    PadUnlinkedParameters* param = new PadUnlinkedParameters();
    param->pad = pad;
    param->peer = peer;
    param->handle = (gulong)data;
    callOnMainThread(callOnPadUnlinkedMT, (void*)param);
    RTC_LOG_FLEAVE();
}

static void callOnPadUnlinkedMT(void* pPadUnlinkedParameters)
{
    RTC_LOG_FENTER();
    ASSERT(isMainThread());
    PadUnlinkedParameters* param = (PadUnlinkedParameters*)pPadUnlinkedParameters;
    CallbackProxy* pThis = CallbackProxy::load(param->handle);
    if (pThis) pThis->onPadUnlinked(param->pad, param->peer);
    delete param;
    RTC_LOG_FLEAVE();
}


void CallbackProxy::onPadUnlinked(GstPad* pad, GstPad* peer)
{
    RTC_LOG_FENTER();
    ASSERT(isMainThread());
    if (m_client) m_client->onPadUnlinked(pad, peer);
    RTC_LOG_FLEAVE();
}

void onRequestPtMapHelperMT(void* data)
{
    OnRequestPtMapParameters* param = static_cast<OnRequestPtMapParameters*>(data);
    CallbackProxy* pThis = CallbackProxy::load(param->handle);
    if (pThis)
        param->requestedPtMap = pThis->onRequestPtMap(param->element, param->session, param->pt);
    else
        param->requestedPtMap = 0;
}

static GstCaps* onRequestPtMapHelper(GstElement* element, guint session, guint pt, gpointer data)
{
    RTC_LOG_FENTER();
    OnRequestPtMapParameters* param = new OnRequestPtMapParameters();
    param->element = element;
    param->session = session;
    param->pt = pt;
    param->handle = (gulong)data;
    param->requestedPtMap = NULL;
    callOnMainThreadAndWait(onRequestPtMapHelperMT, param);
    GstCaps* requestedPtMap = param->requestedPtMap;
    delete param;
    RTC_LOG_FLEAVE();
    return requestedPtMap;
}

GstCaps* CallbackProxy::onRequestPtMap(GstElement* element, guint session, guint pt)
{
    RTC_LOG_FENTER();
    ASSERT(isMainThread());
    GstCaps* ptMap = 0;
    if (m_client) ptMap = m_client->onRequestPtMap(element, session, pt);
    RTC_LOG_FLEAVE();
    return ptMap;
}

void onEnableOrderingHelperMT(void* data)
{
    OnEnableOrderingParameters* param = static_cast<OnEnableOrderingParameters*>(data);
    CallbackProxy* pThis = CallbackProxy::load(param->handle);
    if (pThis)
        param->enableOrdering = pThis->onEnableOrdering(param->session, param->ssrc);
    else {
        RTC_LOG("Invalid handle");
        param->enableOrdering = FALSE;
    }
}

static gboolean onEnableOrderingHelper(GstElement* yarsrtpbin, guint session, guint ssrc, gpointer data)
{
    RTC_LOG_FENTER();
    gboolean enableOrdering = FALSE;
    OnEnableOrderingParameters* param = new OnEnableOrderingParameters();
    param->session = session;
    param->ssrc = ssrc;
    param->handle = (gulong)data;
    param->enableOrdering = FALSE;
    callOnMainThreadAndWait(onEnableOrderingHelperMT, param);
    enableOrdering = param->enableOrdering;
    delete param;
    RTC_LOG_FLEAVE();
    return enableOrdering;
}

gboolean CallbackProxy::onEnableOrdering(guint session, guint ssrc)
{
    RTC_LOG_FENTER();
    gboolean enableOrdering = FALSE;
    if (m_client)
        enableOrdering = m_client->onEnableOrdering(session, ssrc);
    RTC_LOG_FLEAVE();
    return enableOrdering;
}

void onEnableRtxHelperMT(void* data)
{
    OnEnableRtxParameters* param = static_cast<OnEnableRtxParameters*>(data);
    CallbackProxy* pThis = CallbackProxy::load(param->handle);
    if (pThis) {
        param->rtxpt = pThis->onEnableRtx(param->session, param->ssrc, param->pt);
    } else {
        RTC_LOG("Invalid handle");
        param->rtxpt = 0;
    }
}

static guint onEnableRtxHelper(GstElement* yarsrtpbin, guint session, guint ssrc, guint pt, gpointer data)
{
    RTC_LOG_FENTER();
    RTC_LOG("session = %u, ssrc = %u, pt = %u", session, ssrc, pt);
    guint rtxpt = 0;
    OnEnableRtxParameters* param = new OnEnableRtxParameters();
    param->session = session;
    param->ssrc = ssrc;
    param->pt = pt;
    param->handle = (gulong)data;
    param->rtxpt = 0;
    callOnMainThreadAndWait(onEnableRtxHelperMT, param);
    rtxpt = param->rtxpt;
    delete param;
    RTC_LOG_FLEAVE();
    return rtxpt;
}

guint CallbackProxy::onEnableRtx(guint session, guint ssrc, guint pt)
{
    RTC_LOG_FENTER();
    gboolean rtxpt = 0;
    if (m_client)
        rtxpt = m_client->onEnableRtx(session, ssrc, pt);
    RTC_LOG_FLEAVE();
    return rtxpt;
}

}

#endif // CallbackProxy_h
