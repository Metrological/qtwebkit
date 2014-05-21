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

#include "MediaStreamCenterPrivateGStreamer.h"

#include "CentralPipelineUnit.h"
#include "GStreamerUtilities.h"
#include "GstMediaStream.h"
#include <gst/gst.h>
#include <wtf/gobject/GOwnPtr.h>
#include <wtf/text/CString.h>

GST_DEBUG_CATEGORY(webkit_media_stream_debug);
#define GST_CAT_DEFAULT webkit_media_stream_debug

namespace WebCore {

static GRefPtr<GstElement> findDeviceSource(const char* elementName)
{
    GRefPtr<GstElement> element = gst_element_factory_make(elementName, 0);
    if (!element)
        return 0;

    if (!GST_IS_CHILD_PROXY(element.get()))
        return element;

    GstStateChangeReturn stateChangeResult = gst_element_set_state(element.get(), GST_STATE_READY);
    if (stateChangeResult != GST_STATE_CHANGE_SUCCESS)
        return 0;

    GRefPtr<GstElement> deviceSource;
    GstChildProxy* childProxy = GST_CHILD_PROXY(element.get());
    if (gst_child_proxy_get_children_count(childProxy))
        deviceSource = adoptGRef(GST_ELEMENT(gst_child_proxy_get_child_by_index(childProxy, 0)));

    gst_element_set_state(element.get(), GST_STATE_NULL);
    return deviceSource;
}

static RefPtr<MediaStreamSourceGStreamer> probeSource(GstElement* source, MediaStreamSource::Type type)
{
    // TODO: gstreamer 1.0 doesn't have an equivalent to GstPropertyProbe,
    // so we don't support choosing a particular device yet.
    // see https://bugzilla.gnome.org/show_bug.cgi?id=678402

    GstElementFactory* elementFactory = gst_element_get_factory(source);
    GOwnPtr<gchar> factoryName(gst_element_get_name(elementFactory));
    String strFactoryName(factoryName.get());

    String deviceId = strFactoryName;
    deviceId.append(";default");

    GOwnPtr<gchar> deviceName;
    if (!g_getenv("VIDEOTEST"))
        g_object_get(source, "device-name", &deviceName.outPtr(), 0);
    else
        deviceName.outPtr() = g_strdup("test");

    RefPtr<MediaStreamSourceGStreamer> mediaStreamSource = adoptRef(new MediaStreamSourceGStreamer(GstMediaStream::Local, deviceId, deviceName.get(), type, "default", strFactoryName, 0));

    // TODO: fill source capabilities and states, see bug #123345.

    return mediaStreamSource;
}


MediaStreamCenterPrivateGStreamer::MediaStreamCenterPrivateGStreamer()
{
    initializeGStreamer();

    static bool debugCategoryRegistered = false;
    if (!debugCategoryRegistered) {
        GST_DEBUG_CATEGORY_INIT(webkit_media_stream_debug, "webkitmediastream", 0, "WebKit mediastream");
        debugCategoryRegistered = true;
    }

    LOG_MEDIA_MESSAGE("Discovering media source devices");
    discoverDevices(MediaStreamSource::Audio);
    discoverDevices(MediaStreamSource::Video);
}

void MediaStreamCenterPrivateGStreamer::discoverDevices(MediaStreamSource::Type type)
{
    const char* elementName = 0;
    switch (type) {
    case MediaStreamSource::Audio:
        //elementName = g_getenv("VIDEOTEST") ? "videotestsrc" : "autoaudiosrc";
        elementName = "autoaudiosrc";
        break;
    case MediaStreamSource::Video:
        elementName = g_getenv("VIDEOTEST") ? "videotestsrc" : "autovideosrc";
        break;
    case MediaStreamSource::None:
        ASSERT_NOT_REACHED();
        break;
    }

    GRefPtr<GstElement> deviceSource = findDeviceSource(elementName);
    if (!deviceSource)
        return;

    if (type == MediaStreamSource::Video) {
        static int patternNumber = 0;
        if (!strcmp(elementName, "videotestsrc"))
            g_object_set(deviceSource.get(), "pattern", patternNumber, NULL);
        patternNumber++;
    }

    RefPtr<MediaStreamSourceGStreamer> source = probeSource(deviceSource.get(), type);
    String id = source->id();

    MediaStreamSourceGStreamerMap::iterator sourceIterator = m_sourceMap.find(id);
    if (sourceIterator == m_sourceMap.end())
        m_sourceMap.add(id, source);
}

PassRefPtr<MediaStreamSource> MediaStreamCenterPrivateGStreamer::firstSource(MediaStreamSource::Type type)
{
    for (MediaStreamSourceGStreamerMap::iterator iter = m_sourceMap.begin(); iter != m_sourceMap.end(); ++iter) {
        RefPtr<MediaStreamSource> source = iter->value;
        if (source->type() == type)
            return source;
    }

    return 0;
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
