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
#include <gst/gst.h>
#include <wtf/gobject/GUniquePtr.h>
#include <wtf/text/CString.h>

GST_DEBUG_CATEGORY(webkit_media_stream_debug);
#define GST_CAT_DEFAULT webkit_media_stream_debug

namespace WebCore {

static GRefPtr<GstElement> findDeviceSource(const char* elementName)
{
    GRefPtr<GstElement> element = gst_element_factory_make(elementName, nullptr);
    if (!element)
        return nullptr;

    if (!GST_IS_CHILD_PROXY(element.get()))
        return element;

    GstStateChangeReturn stateChangeResult = gst_element_set_state(element.get(), GST_STATE_READY);
    if (stateChangeResult != GST_STATE_CHANGE_SUCCESS)
        return nullptr;

    GRefPtr<GstElement> deviceSource;
    GstChildProxy* childProxy = GST_CHILD_PROXY(element.get());
    if (gst_child_proxy_get_children_count(childProxy))
        deviceSource = adoptGRef(GST_ELEMENT(gst_child_proxy_get_child_by_index(childProxy, 0)));

    gst_element_set_state(element.get(), GST_STATE_NULL);
    return deviceSource;
}

static RefPtr<MediaStreamSourceGStreamer> probeSource(GstElement* source, MediaStreamSource::Type type)
{
    // FIXME: gstreamer 1.0 doesn't have an equivalent to GstPropertyProbe,
    // so we don't support choosing a particular device yet.
    // see https://bugzilla.gnome.org/show_bug.cgi?id=678402

    GstElementFactory* elementFactory = gst_element_get_factory(source);
    GUniquePtr<gchar> factoryName(gst_element_get_name(elementFactory));
    String strFactoryName(factoryName.get());

    String deviceId = strFactoryName;
    deviceId.append(";default");

    GUniqueOutPtr<gchar> deviceName;
    g_object_get(source, "device-name", &deviceName.outPtr(), nullptr);

    RefPtr<MediaStreamSourceGStreamer> mediaStreamSource = adoptRef(new MediaStreamSourceGStreamer(deviceId, deviceName.get(), type, "default", strFactoryName));

    // TODO: fill source capabilities and states.

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
    const char* elementName = nullptr;
    switch (type) {
    case MediaStreamSource::Audio:
        elementName = "autoaudiosrc";
        break;
    case MediaStreamSource::Video:
        elementName = "autovideosrc";
        break;
    case MediaStreamSource::None:
        ASSERT_NOT_REACHED();
        break;
    }

    GRefPtr<GstElement> deviceSource = findDeviceSource(elementName);
    if (!deviceSource)
        return;

    RefPtr<MediaStreamSourceGStreamer> source = probeSource(deviceSource.get(), type);
    String id = source->id();

    MediaStreamSourceGStreamerMap::iterator sourceIterator = m_sourceMap.find(id);
    if (sourceIterator == m_sourceMap.end())
        m_sourceMap.add(id, source);
}

PassRefPtr<MediaStreamSource> MediaStreamCenterPrivateGStreamer::firstSource(MediaStreamSource::Type type)
{
    for (auto iter = m_sourceMap.begin(); iter != m_sourceMap.end(); ++iter) {
        RefPtr<MediaStreamSource> source = iter->value;
        if (source->type() == type)
            return source;
    }

    return nullptr;
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
