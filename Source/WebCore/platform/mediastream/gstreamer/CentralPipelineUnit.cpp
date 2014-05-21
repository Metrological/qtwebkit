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

#include "config.h"

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#include "CentralPipelineUnit.h"

#include "GStreamerUtilities.h"
#include "MediaStreamSourceGStreamer.h"
#include <wtf/MainThread.h>
#include <wtf/gobject/GOwnPtr.h>
#include <wtf/text/CString.h>
#include <signal.h>
#include <stdio.h>

#define RTC_LOG(fmt, args...) printf(fmt "\n", ##args)
//#define RTC_LOG(fmt, args...) void(0)

GST_DEBUG_CATEGORY_EXTERN(webkit_media_stream_debug);
#define GST_CAT_DEFAULT webkit_media_stream_debug

namespace WebCore {

static String generateElementPadId(GstElement* element, GstPad* pad)
{
    String id(String::number(gulong(element)));
    id.append("_");
    id.append(String::number(gulong(pad)));
    return id;
}

static gboolean messageCallback(GstBus*, GstMessage* message, CentralPipelineUnit* cpu)
{
    return cpu->handleMessage(message);
}

static gboolean doDump(gpointer data)
{
    CentralPipelineUnit* cpu = reinterpret_cast<CentralPipelineUnit*>(data);
    cpu->dumpPipeline();
    return FALSE;
}

CentralPipelineUnit::CentralPipelineUnit(const String& id)
{
    initializeGStreamer();

    m_pipeline = gst_pipeline_new(id.utf8().data());
    GRefPtr<GstBus> bus = adoptGRef(gst_pipeline_get_bus(GST_PIPELINE(m_pipeline.get())));
    gst_bus_add_signal_watch(bus.get());
    gst_bus_set_sync_handler(bus.get(), gst_bus_sync_signal_handler, 0, 0);
    g_signal_connect(bus.get(), "sync-message", G_CALLBACK(messageCallback), this);

    gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);

    g_timeout_add_seconds(10, doDump, this);
}

CentralPipelineUnit::~CentralPipelineUnit()
{
    GRefPtr<GstBus> bus = adoptGRef(gst_pipeline_get_bus(GST_PIPELINE(m_pipeline.get())));
    gst_bus_remove_signal_watch(bus.get());
}

void CentralPipelineUnit::dumpPipeline()
{
    RTC_LOG(">>>>>> dumping pipeline");
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, "central-pipeline-unit");
}

bool CentralPipelineUnit::doesSourceExist(const String& key)
{
    PipelineMap::iterator sourceIterator = m_pipelineMap.find(key);
    return (sourceIterator != m_pipelineMap.end());
}

bool CentralPipelineUnit::handleMessage(GstMessage* message)
{
    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
        GOwnPtr<GError> error;
        GOwnPtr<gchar> debug;

        gst_message_parse_error(message, &error.outPtr(), &debug.outPtr());
        ERROR_MEDIA_MESSAGE("Media error: %d, %s", error->code, error->message);
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, "webkit-mediastream.error");
        break;
    }
    default:
        break;
    }

    return true;
}

GstElement* CentralPipelineUnit::pipeline() const
{
    return m_pipeline.get();
}

float CentralPipelineUnit::currentTime() const
{
    gint64 position = GST_CLOCK_TIME_NONE;
    GstQuery* query= gst_query_new_position(GST_FORMAT_TIME);
    if (gst_element_query(m_pipeline.get(), query))
        gst_query_parse_position(query, 0, &position);
    gst_query_unref(query);

    float result = 0.0f;
    if (static_cast<GstClockTime>(position) != GST_CLOCK_TIME_NONE)
        result = static_cast<double>(position) / GST_SECOND;
    return result;
}

bool CentralPipelineUnit::disconnectFromSource(PassRefPtr<MediaStreamSourceGStreamer> prpSource, GstElement* sink, GstPad* sinkpad)
{
    RefPtr<MediaStreamSourceGStreamer> source = prpSource;
    if (!sink || !source) {
        ERROR_MEDIA_MESSAGE("No sink provided or empty source id");
        return false;
    }

    LOG_MEDIA_MESSAGE("Disconnecting from source with id=%s, sink=%p, sinkpad=%p", source->id().utf8().data(), sink, sinkpad);

    PipelineMap::iterator sourceIterator = m_pipelineMap.find(source->id());
    if (sourceIterator == m_pipelineMap.end()) {
        LOG_MEDIA_MESSAGE("Could not find source with id=%s", source->id().utf8().data());
        return false;
    }

    GRefPtr<GstPad> refSinkPad(sinkpad);
    if (!refSinkPad) {
        LOG_MEDIA_MESSAGE("No pad was given as argument, trying the element static \"sink\" pad.");
        refSinkPad = adoptGRef(gst_element_get_static_pad(sink, "sink"));
        if (!refSinkPad) {
            ERROR_MEDIA_MESSAGE("Unable to retrieve element sink pad");
            return false;
        }
    }

    GRefPtr<GstPad> lQueueSrcPad = adoptGRef(gst_pad_get_peer(refSinkPad.get()));
    GRefPtr<GstElement> lQueue = adoptGRef(gst_pad_get_parent_element(lQueueSrcPad.get()));
    GRefPtr<GstPad> lQueueSinkPad = adoptGRef(gst_element_get_static_pad(lQueue.get(), "sink"));
    GRefPtr<GstPad> lTeeSrcPad = adoptGRef(gst_pad_get_peer(lQueueSinkPad.get()));
    GRefPtr<GstElement> lTee = adoptGRef(gst_pad_get_parent_element(lTeeSrcPad.get()));

    disconnectSinkFromTee(lTee.get(), sink, refSinkPad.get());
    return true;
}

class DisconnectSinkJob {
public:
    DisconnectSinkJob(CentralPipelineUnit* cpu, GstElement* sink, GstPad* pad) :
        m_cpu(cpu)
        , m_sink(sink)
        , m_pad(pad)
    { }

    CentralPipelineUnit* m_cpu;
    GRefPtr<GstElement> m_sink;
    GRefPtr<GstPad> m_pad;
};

static gboolean dropElement(gpointer userData)
{
    DisconnectSinkJob* job = reinterpret_cast<DisconnectSinkJob*>(userData);
    job->m_cpu->disconnectSinkFromPipelinePadBlocked(job->m_sink.get(), job->m_pad.get());
    delete job;
    return FALSE;
}

static GstPadProbeReturn elementDrainedCallback(GstPad*, GstPadProbeInfo*, gpointer userData)
{
    g_idle_add(dropElement, userData);
    return GST_PAD_PROBE_DROP;
}

static GstPadProbeReturn disconnectSinkFromPipelineProbeAddedProxy(GstPad* pad, GstPadProbeInfo* info, gpointer userData)
{
    gst_pad_remove_probe(pad, GST_PAD_PROBE_INFO_ID(info));

    DisconnectSinkJob* job = reinterpret_cast<DisconnectSinkJob*>(userData);
    GRefPtr<GstPad> queueSinkPad = adoptGRef(gst_pad_get_peer(job->m_pad.get()));
    GRefPtr<GstElement> queue = adoptGRef(gst_pad_get_parent_element(queueSinkPad.get()));
    GRefPtr<GstPad> queueSourcePad = adoptGRef(gst_element_get_static_pad(queue.get(), "src"));

    // Install new probe for EOS.
    gst_pad_add_probe(queueSourcePad.get(), static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
        elementDrainedCallback, userData, 0);

    // Push EOS into the element, the probe will be fired when the
    // EOS leaves the queue and it has thus drained all of its data
    gst_pad_send_event(queueSinkPad.get(), gst_event_new_eos());

    return GST_PAD_PROBE_OK;
}

void CentralPipelineUnit::disconnectSinkFromPipelinePadBlocked(GstElement* sink, GstPad* teeSourcePad)
{
    GRefPtr<GstElement> tee = adoptGRef(gst_pad_get_parent_element(teeSourcePad));

    GRefPtr<GstPad> queueSinkPad = adoptGRef(gst_pad_get_peer(teeSourcePad));
    GRefPtr<GstElement> queue = adoptGRef(gst_pad_get_parent_element(queueSinkPad.get()));
    GRefPtr<GstPad> queueSourcePad = adoptGRef(gst_element_get_static_pad(queue.get(), "src"));

    gst_pad_set_active(teeSourcePad, FALSE);
    gst_pad_set_active(queueSourcePad.get(), FALSE);
    gst_element_set_state(queue.get(), GST_STATE_NULL);
    gst_bin_remove(GST_BIN(m_pipeline.get()), queue.get());
    gst_element_release_request_pad(tee.get(), teeSourcePad);

    GstIterator* padsIterator = gst_element_iterate_sink_pads(sink);
    bool done = false;
    bool doRemoveSink = true;
    while (!done) {
        GRefPtr<GstPad> pad;
        GValue item = G_VALUE_INIT;
        GstIteratorResult it = gst_iterator_next(padsIterator, &item);

        switch (it) {
        case GST_ITERATOR_OK: {
            pad = static_cast<GstPad*>(g_value_get_object(&item));
            GRefPtr<GstPad> peer = adoptGRef(gst_pad_get_peer(pad.get()));
            if (peer) {
                doRemoveSink = false;
                done = true;
            }
            break;
        }
        case GST_ITERATOR_RESYNC:
            gst_iterator_resync(padsIterator);
            break;
        case GST_ITERATOR_DONE:
            done = true;
            break;
        case GST_ITERATOR_ERROR:
            LOG_MEDIA_MESSAGE("ERROR, Iterating!");
            done = true;
            break;
        default:
            break;
        }
    }
    gst_iterator_free(padsIterator);

    String sourceAndPadId = generateElementPadId(sink, 0);
    SourceIdLookupMap::iterator sourceIdIterator = m_sourceIdLookupMap.find(generateElementPadId(sink, 0));
    if (sourceIdIterator != m_sourceIdLookupMap.end()) {
        String sourceId = sourceIdIterator->value;
        PipelineMap::iterator sourceInfoIterator = m_pipelineMap.find(sourceId);
        if (sourceInfoIterator != m_pipelineMap.end()) {
            Source sourceInfo = sourceInfoIterator->value;
            doRemoveSink = sourceInfo.m_removeWhenNotUsed;
        }
    }

    if (doRemoveSink) {
        gst_element_set_state(sink, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(m_pipeline.get()), sink);
        LOG_MEDIA_MESSAGE("Sink removed");
        GRefPtr<GstPad> teeSinkPad = adoptGRef(gst_element_get_static_pad(tee.get(), "sink"));
        GRefPtr<GstPad> sourceSrcPad = adoptGRef(gst_pad_get_peer(teeSinkPad.get()));
        GRefPtr<GstElement> source = adoptGRef(gst_pad_get_parent_element(sourceSrcPad.get()));
        disconnectUnusedSource(tee.get());
    } else
        LOG_MEDIA_MESSAGE("Did not remove the sink");

}

void CentralPipelineUnit::disconnectSinkFromTee(GstElement* tee, GstElement* sink, GstPad* pad)
{
    GRefPtr<GstPad> sinkSinkPad = pad;
    if (!pad)
        sinkSinkPad = adoptGRef(gst_element_get_static_pad(sink, "sink"));

    LOG_MEDIA_MESSAGE("disconnecting sink %s from tee", GST_OBJECT_NAME(sink));

    GRefPtr<GstPad> queueSourcePad = adoptGRef(gst_pad_get_peer(sinkSinkPad.get()));
    GRefPtr<GstElement> queue = adoptGRef(gst_pad_get_parent_element(queueSourcePad.get()));
    GRefPtr<GstPad> queueSinkPad = adoptGRef(gst_element_get_static_pad(queue.get(), "sink"));
    GRefPtr<GstPad> teeSourcePad = gst_pad_get_peer(queueSinkPad.get());

    DisconnectSinkJob* job = new DisconnectSinkJob(this, sink, teeSourcePad.get());

    gst_pad_add_probe(teeSourcePad.get(), GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
        disconnectSinkFromPipelineProbeAddedProxy, job, 0);
}

void CentralPipelineUnit::disconnectUnusedSource(GstElement* tee)
{
    GRefPtr<GstPad> teeSinkPad = adoptGRef(gst_element_get_static_pad(tee, "sink"));
    GRefPtr<GstPad> sourceSrcPad = adoptGRef(gst_pad_get_peer(teeSinkPad.get()));
    GRefPtr<GstElement> source = adoptGRef(gst_pad_get_parent_element(sourceSrcPad.get()));

    SourceIdLookupMap::iterator sourceIdIterator = m_sourceIdLookupMap.find(generateElementPadId(source.get(), sourceSrcPad.get()));
    if (sourceIdIterator != m_sourceIdLookupMap.end()) {
        String sourceId = sourceIdIterator->value;

        PipelineMap::iterator sourceInfoIterator = m_pipelineMap.find(sourceId);
        if (sourceInfoIterator != m_pipelineMap.end()) {
            Source sourceInfo = sourceInfoIterator->value;

            if (sourceInfo.m_removeWhenNotUsed) {
                LOG_MEDIA_MESSAGE("Removing source %p with id=%s", source.get(), sourceId.ascii().data());
                gst_element_set_state(source.get(), GST_STATE_NULL);
                gst_element_set_state(tee, GST_STATE_NULL);
                gst_bin_remove(GST_BIN(m_pipeline.get()), source.get());
                gst_bin_remove(GST_BIN(m_pipeline.get()), tee);

                m_pipelineMap.remove(sourceId);
                m_sourceIdLookupMap.remove(generateElementPadId(source.get(), sourceInfo.m_sourcePad.get()));
            }
        }
    }

}

bool CentralPipelineUnit::ensureSourceElement(PassRefPtr<MediaStreamSourceGStreamer> prpSource, const String& id, GRefPtr<GstElement>& sourceElement, GRefPtr<GstPad>& sourcePad)
{
    RefPtr<MediaStreamSourceGStreamer> source = prpSource;
    String sourceId = id.isEmpty() ? source->id() : id;

    LOG_MEDIA_MESSAGE("Connecting to source element %s", sourceId.utf8().data());

    PipelineMap::iterator sourceIterator = m_pipelineMap.find(sourceId);
    if (sourceIterator != m_pipelineMap.end()) {
        Source& storedSource = sourceIterator->value;
        LOG_MEDIA_MESSAGE("Source element %s already in pipeline, using it.", sourceId.utf8().data());
        sourceElement = storedSource.m_sourceElement;
        sourcePad = storedSource.m_sourcePad;
        return false;
    }

    if (source) {
        RTC_LOG("creating source element");
        sourceElement = source->createGStreamerElement(sourcePad);
    } else {
        RTC_LOG("looking up source element");
        SourceExtensionMap::iterator extensionIterator = m_sourceExtensionMap.find(sourceId);
        if (extensionIterator != m_sourceExtensionMap.end())
            sourceElement = extensionIterator->value.extension;
    }

    if (!sourceElement) {
        ERROR_MEDIA_MESSAGE("Unable to create source element");
        return false;
    }
    RTC_LOG("sourceElement name: %s", GST_ELEMENT_NAME(sourceElement.get()));

    if (!sourcePad) {
        LOG_MEDIA_MESSAGE("SourceFactory could not create a source pad, trying the element static \"src\" pad");
        sourcePad = adoptGRef(gst_element_get_static_pad(sourceElement.get(), "src"));
    }

    if (!sourcePad) {
        ERROR_MEDIA_MESSAGE("Unable to retrieve element source pad");
        sourceElement.clear();
        return false;
    }

    m_pipelineMap.add(sourceId, Source(sourceElement, sourcePad, true));
    m_sourceIdLookupMap.add(generateElementPadId(sourceElement.get(), sourcePad.get()), sourceId);
    return true;
}

bool CentralPipelineUnit::connectToSource(PassRefPtr<MediaStreamSourceGStreamer> prpSource, const String& sourceId, GstElement* sink, GstPad* sinkpad)
{
    RefPtr<MediaStreamSourceGStreamer> source = prpSource;
    if (!source && sourceId.isEmpty()) {
        ERROR_MEDIA_MESSAGE("Empty source id");
        return false;
    }

    if (!sink) {
        ERROR_MEDIA_MESSAGE("No sink provided");
        return false;
    }

    GRefPtr<GstElement> sourceElement;
    GRefPtr<GstPad> sourcePad;
    bool wasAddedInPipeline = ensureSourceElement(source, sourceId, sourceElement, sourcePad);

    if (!sourceElement || !sourcePad) {
        ERROR_MEDIA_MESSAGE("Unable to retrieve source element or pad");
        return false;
    }

    LOG_MEDIA_MESSAGE("Connecting source %s to sink %s", GST_ELEMENT_NAME(sourceElement.get()), GST_ELEMENT_NAME(sink));

    GRefPtr<GstPad> refSinkPad(sinkpad);
    if (!refSinkPad) {
        LOG_MEDIA_MESSAGE("No pad was given as argument, trying the element static \"sink\" pad.");
        refSinkPad = adoptGRef(gst_element_get_static_pad(sink, "sink"));
        if (!refSinkPad) {
            ERROR_MEDIA_MESSAGE("Unable to retrieve element sink pad");
            return false;
        }
    }

    GOwnPtr<gchar> sourceName(gst_element_get_name(sourceElement.get()));
    String teeName = String::format("tee_for_source_%s", sourceName.get());
    GRefPtr<GstElement> tee;
    if (wasAddedInPipeline) {
        tee = gst_element_factory_make("tee", teeName.utf8().data());
        gst_bin_add_many(GST_BIN(m_pipeline.get()), sourceElement.get(), tee.get(), NULL);
        GRefPtr<GstPad> teeSinkPad = adoptGRef(gst_element_get_static_pad(tee.get(), "sink"));
        gst_pad_link(sourcePad.get(), teeSinkPad.get());
        gst_element_sync_state_with_parent(sourceElement.get());
        gst_element_sync_state_with_parent(tee.get());
    } else
        tee = gst_bin_get_by_name(GST_BIN(m_pipeline.get()), teeName.utf8().data());

    GRefPtr<GstElement> sinkParent = adoptGRef(GST_ELEMENT(gst_element_get_parent(sink)));
    if (!sinkParent) {
        LOG_MEDIA_MESSAGE("Sink not in pipeline, adding.");
        gst_bin_add(GST_BIN(m_pipeline.get()), sink);
    } else if (sinkParent.get() != m_pipeline.get()) {
        ERROR_MEDIA_MESSAGE("Sink already added to another pipeline: %p. Our pipeline (%p) is now broken!", sinkParent.get(), m_pipeline.get());
        dumpPipeline();
        return false;
    } else
        LOG_MEDIA_MESSAGE("Sink parent: %s", GST_ELEMENT_NAME(sinkParent.get()));

    GRefPtr<GstElement> queue = gst_element_factory_make("queue", 0);
    gst_bin_add(GST_BIN(m_pipeline.get()), queue.get());

    gboolean ret = gst_element_link_pads_full(tee.get(), 0, queue.get(), "sink", GST_PAD_LINK_CHECK_DEFAULT);
    RTC_LOG("tee linked to queue: %d", ret);
    GRefPtr<GstPad> queueSrcPad = adoptGRef(gst_element_get_static_pad(queue.get(), "src"));
    ret = gst_pad_link(queueSrcPad.get(), refSinkPad.get());
    RTC_LOG("queue linked downstream: %d", ret);

    gst_element_sync_state_with_parent(queue.get());
    gst_element_sync_state_with_parent(sink);

    gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);
    return true;
}

gboolean CentralPipelineUnit::insertSource(GstElement* sourceElement, GstPad* pad, const String& sourceId, SourceInsertedCallback callback, gpointer userData)
{
    LOG_MEDIA_MESSAGE("Source = %s, sourceId = %s", GST_OBJECT_NAME(sourceElement), sourceId.ascii().data());

    if (sourceId.isNull() || sourceId.isEmpty() || sourceElement == NULL)
        return FALSE;

    PipelineMap::iterator sourceIt = m_pipelineMap.find(sourceId);
    if (sourceIt != m_pipelineMap.end())
        return FALSE;

    GstElement* parent = (GstElement*)gst_element_get_parent(sourceElement);
    if (!parent) {
        LOG_MEDIA_MESSAGE("Added source element to pipeline %p", m_pipeline.get());
        gst_bin_add(GST_BIN(m_pipeline.get()), sourceElement);
    }

    if (pad) {
        LOG_MEDIA_MESSAGE("Pad available. Adding tee and fakesink to it.");
        GstElement* tee = gst_element_factory_make("tee", NULL);
        gst_bin_add(GST_BIN(m_pipeline.get()), tee);
        GstPad* teeSinkPad = gst_element_get_static_pad(tee, "sink");
        gst_pad_link(pad, teeSinkPad);

        // FIXME: How can we do this without the fakesink? We get link-error if tee is not connected to something.
        // FIXMEPHIL
        GstElement* fakesink = gst_element_factory_make("fakesink", NULL);
        g_object_set(fakesink,
                     "sync", FALSE,
                     "async", FALSE,
                     NULL);
        gst_bin_add(GST_BIN(m_pipeline.get()), fakesink);
        gst_element_link(tee, fakesink);
        LOG_MEDIA_MESSAGE("Trying to sync state");
        gst_element_sync_state_with_parent(sourceElement);
        gst_element_sync_state_with_parent(tee);
        gst_element_sync_state_with_parent(fakesink);
    }
    else {
        LOG_MEDIA_MESSAGE("Trying to sync state");
        gst_element_sync_state_with_parent(sourceElement);
    }

    m_pipelineMap.add(sourceId, Source(sourceElement, pad, false));
    m_sourceIdLookupMap.add(generateElementPadId(sourceElement, pad), sourceId);

    if (callback) {
        callback(sourceElement, pad, userData);
    }

    // FIXMEPHIL
    // if (pad)
    //     gst_pad_set_blocked_async(pad, FALSE, gstPadBlockCallback, 0);

    return true;
}

void CentralPipelineUnit::registerSourceExtension(GstElement* ext, const String& sourceId, const String& extSrcId)
{
    if (m_sourceExtensionMap.find(sourceId) == m_sourceExtensionMap.end()) {
        SourceExtensionInfo sei;
        sei.extension = ext;
        sei.sourceToExtend = sourceId;
        m_sourceExtensionMap.add(extSrcId, sei);
    }
}

gchar* createUniqueName(const gchar* prefix)
{
    static int count = 0;
    gchar* uniqueName = new gchar[50];
    sprintf(uniqueName, "%s%d", prefix, count);
    count++;
    return uniqueName;
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
