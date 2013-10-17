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
#include <wtf/gobject/GUniquePtr.h>
#include <wtf/text/CString.h>

GST_DEBUG_CATEGORY_EXTERN(webkit_media_stream_debug);
#define GST_CAT_DEFAULT webkit_media_stream_debug

namespace WebCore {

static gboolean messageCallback(GstBus*, GstMessage* message, CentralPipelineUnit* cpu)
{
    return cpu->handleMessage(message);
}

CentralPipelineUnit& CentralPipelineUnit::instance()
{
    ASSERT(isMainThread());
    DEFINE_STATIC_LOCAL(CentralPipelineUnit, instance, ());
    return instance;
}

CentralPipelineUnit::CentralPipelineUnit()
{
    initializeGStreamer();

    m_pipeline = gst_pipeline_new("mediastream_pipeline");
    GRefPtr<GstBus> bus = webkitGstPipelineGetBus(GST_PIPELINE(m_pipeline.get()));
    gst_bus_add_signal_watch(bus.get());
    gst_bus_set_sync_handler(bus.get(), gst_bus_sync_signal_handler, 0, 0);
    g_signal_connect(bus.get(), "sync-message", G_CALLBACK(messageCallback), this);

    gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);
}

CentralPipelineUnit::~CentralPipelineUnit()
{
    GRefPtr<GstBus> bus = webkitGstPipelineGetBus(GST_PIPELINE(m_pipeline.get()));
    gst_bus_remove_signal_watch(bus.get());
}

bool CentralPipelineUnit::handleMessage(GstMessage* message)
{
    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
        GUniqueOutPtr<GError> error;
        GUniqueOutPtr<gchar> debug;

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

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
