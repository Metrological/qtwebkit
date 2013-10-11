/*
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

#include "MediaStreamSourceGStreamer.h"

#include "GStreamerUtilities.h"
#include <wtf/text/CString.h>

GST_DEBUG_CATEGORY_EXTERN(webkit_media_stream_debug);
#define GST_CAT_DEFAULT webkit_media_stream_debug

namespace WebCore {

static GRefPtr<GstElement> createAudioSourceBin(GRefPtr<GstElement> source)
{
    GRefPtr<GstElement> audioconvert = gst_element_factory_make("audioconvert", nullptr);
    if (!audioconvert) {
        LOG_MEDIA_MESSAGE("ERROR, Got no audioconvert element for audio source pipeline");
        return nullptr;
    }

    // FIXME: The caps should be coherent with device capabilities, see bug #123345.
    GRefPtr<GstCaps> audiocaps;
    audiocaps = adoptGRef(gst_caps_new_simple("audio/x-raw", "channels", G_TYPE_INT, 1, NULL));
    if (!audiocaps) {
        LOG_MEDIA_MESSAGE("ERROR, Unable to create filter caps for audio source pipeline");
        return nullptr;
    }

    GRefPtr<GstElement> audioSourceBin = gst_bin_new(nullptr);

    gst_bin_add_many(GST_BIN(audioSourceBin.get()), source.get(), audioconvert.get(), NULL);

    if (!gst_element_link_filtered(source.get(), audioconvert.get(), audiocaps.get())) {
        LOG_MEDIA_MESSAGE("ERROR, Cannot link audio source elements");
        return nullptr;
    }

    GstPad* srcPad = gst_element_get_static_pad(audioconvert.get(), "src");
    gst_element_add_pad(audioSourceBin.get(), gst_ghost_pad_new("src", srcPad));

    return audioSourceBin;
}

static GRefPtr<GstElement> createVideoSourceBin(GRefPtr<GstElement> source)
{
    GRefPtr<GstElement> colorspace = gst_element_factory_make("videoconvert", nullptr);
    if (!colorspace) {
        LOG_MEDIA_MESSAGE("ERROR, Got no videoconvert element for video source pipeline");
        return nullptr;
    }
    GRefPtr<GstElement> videoscale = gst_element_factory_make("videoscale", nullptr);
    if (!videoscale) {
        LOG_MEDIA_MESSAGE("ERROR, Got no videoscale element for video source pipeline");
        return nullptr;
    }

    // FIXME: The caps should be coherent with device capabilities, see bug #123345.
    GRefPtr<GstCaps> videocaps = gst_caps_new_simple("video/x-raw", "width", G_TYPE_INT, 320, "height", G_TYPE_INT, 240, NULL);
    if (!videocaps) {
        LOG_MEDIA_MESSAGE("ERROR, Unable to create filter caps for video source pipeline");
        return nullptr;
    }

    GstElement* videoSourceBin = gst_bin_new(nullptr);

    gst_bin_add_many(GST_BIN(videoSourceBin), source.get(), videoscale.get(), colorspace.get(), NULL);

    if (!gst_element_link_many(source.get(), videoscale.get(), NULL)
        || !gst_element_link_filtered(videoscale.get(), colorspace.get(), videocaps.get())) {
        LOG_MEDIA_MESSAGE("ERROR, Cannot link video source elements");
        gst_object_unref(videoSourceBin);
        return nullptr;
    }

    GRefPtr<GstPad> srcPad = gst_element_get_static_pad(colorspace.get(), "src");
    gst_element_add_pad(videoSourceBin, gst_ghost_pad_new("src", srcPad.get()));

    return videoSourceBin;
}

GRefPtr<GstElement> MediaStreamSourceGStreamer::createGStreamerElement(GRefPtr<GstPad>& srcPad)
{
    LOG_MEDIA_MESSAGE("Creating source with id=%s", id().utf8().data());

    GRefPtr<GstElementFactory> factory = gst_element_factory_find(factoryKey().utf8().data());
    if (!factory)
        return nullptr;

    GRefPtr<GstElement> source(gst_element_factory_create(factory.get(), "device_source"));
    if (!source)
        return nullptr;

    LOG_MEDIA_MESSAGE("sourceInfo.m_device=%s", device().utf8().data());
    // TODO: Device choosing is not implemented for gstreamer 1.0 yet.

    if (type() == MediaStreamSource::Audio)
        source = createAudioSourceBin(source);
    else if (type() == MediaStreamSource::Video)
        source = createVideoSourceBin(source);

    srcPad = adoptGRef(gst_element_get_static_pad(source.get(), "src"));

    return source;
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
