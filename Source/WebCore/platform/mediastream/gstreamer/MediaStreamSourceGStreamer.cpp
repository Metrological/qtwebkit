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
    GRefPtr<GstElement> audioconvert = gst_element_factory_make("audioconvert", 0);
    if (!audioconvert) {
        LOG_MEDIA_MESSAGE("ERROR, Got no audioconvert element for audio source pipeline");
        return 0;
    }

    // FIXME: The caps should be coherent with device capabilities, see bug #123345.
    GRefPtr<GstCaps> audiocaps;
    audiocaps = adoptGRef(gst_caps_new_simple("audio/x-raw", "channels", G_TYPE_INT, 1, NULL));
    if (!audiocaps) {
        LOG_MEDIA_MESSAGE("ERROR, Unable to create filter caps for audio source pipeline");
        return 0;
    }

    GRefPtr<GstElement> audioSourceBin = gst_bin_new(0);

    gst_bin_add_many(GST_BIN(audioSourceBin.get()), source.get(), audioconvert.get(), NULL);

    if (!gst_element_link_filtered(source.get(), audioconvert.get(), audiocaps.get())) {
        LOG_MEDIA_MESSAGE("ERROR, Cannot link audio source elements");
        return 0;
    }

    GstPad* srcPad = gst_element_get_static_pad(audioconvert.get(), "src");
    gst_element_add_pad(audioSourceBin.get(), gst_ghost_pad_new("src", srcPad));

    return audioSourceBin;
}

static GRefPtr<GstElement> createVideoSourceBin(GRefPtr<GstElement> source)
{
    GstElement* videoSourceBin = gst_bin_new(0);
    GRefPtr<GstElement> capsfilter = gst_element_factory_make("capsfilter", 0);
    GRefPtr<GstPad> srcPad;
    const gchar* videoTest = g_getenv("VIDEOTEST");
    const gchar* rpi = g_getenv("RPI");

    // FIXME: The caps should be coherent with device capabilities, see bug #123345.
    GRefPtr<GstCaps> videocaps;
    int width = 320;
    int height = 240;

    gst_bin_add_many(GST_BIN(videoSourceBin), source.get(), capsfilter.get(), NULL);

    if (videoTest) {
        if (rpi) {
            // videotestsrc ! queue ! omxh264enc ! capsfilter
            GRefPtr<GstElement> queue = gst_element_factory_make("queue", 0);
            GRefPtr<GstElement> enc = gst_element_factory_make("omxh264enc", 0);

            videocaps = gst_caps_new_simple("video/x-h264", "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);
            g_object_set(capsfilter.get(), "caps", videocaps.get(), NULL);

            gst_bin_add_many(GST_BIN(videoSourceBin), queue.get(), enc.get(), NULL);
            if (!gst_element_link_many(source.get(), queue.get(), enc.get(), capsfilter.get(), NULL)) {
                LOG_MEDIA_MESSAGE("ERROR, Cannot link video source elements");
                gst_object_unref(videoSourceBin);
                return 0;
            }

            srcPad = gst_element_get_static_pad(capsfilter.get(), "src");
        } else {
            // videotestsrc ! videoscale ! capsfilter ! videoconvert
            GRefPtr<GstElement> colorspace;
            GRefPtr<GstElement> videoscale;

            colorspace = gst_element_factory_make("videoconvert", 0);
            if (!colorspace) {
                LOG_MEDIA_MESSAGE("ERROR, Got no videoconvert element for video source pipeline");
                return 0;
            }
            videoscale = gst_element_factory_make("videoscale", 0);
            if (!videoscale) {
                LOG_MEDIA_MESSAGE("ERROR, Got no videoscale element for video source pipeline");
                return 0;
            }

            videocaps = gst_caps_new_simple("video/x-raw", "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);
            g_object_set(capsfilter.get(), "caps", videocaps.get(), NULL);

            gst_bin_add_many(GST_BIN(videoSourceBin), videoscale.get(), colorspace.get(), NULL);
            if (!gst_element_link_many(source.get(), videoscale.get(), capsfilter.get(), colorspace.get(), NULL)) {
                LOG_MEDIA_MESSAGE("ERROR, Cannot link video source elements");
                gst_object_unref(videoSourceBin);
                return 0;
            }
            srcPad = gst_element_get_static_pad(colorspace.get(), "src");
        }
    } else {
        const char* mediaType;

        if (rpi) {
            // rpicamsrc ! capsfilter
            g_object_set(source.get(), "fullscreen", FALSE, "preview", FALSE, NULL);
            mediaType = "video/x-h264";
        } else {
            // autovideosrc ! capsfilter
            mediaType = "video/x-raw";
        }

        videocaps = gst_caps_new_simple(mediaType, "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);
        g_object_set(capsfilter.get(), "caps", videocaps.get(), NULL);

        if (!gst_element_link(source.get(), capsfilter.get())) {
            LOG_MEDIA_MESSAGE("ERROR, Cannot link video source elements");
            gst_object_unref(videoSourceBin);
            return 0;
        }
        srcPad = gst_element_get_static_pad(capsfilter.get(), "src");
    }

    gst_element_add_pad(videoSourceBin, gst_ghost_pad_new("src", srcPad.get()));
    return videoSourceBin;
}

GRefPtr<GstElement> MediaStreamSourceGStreamer::createGStreamerElement(GRefPtr<GstPad>& srcPad)
{
    LOG_MEDIA_MESSAGE("Creating source with id=%s factoryKey=%s", id().utf8().data(), factoryKey().utf8().data());

    if (m_element && (m_streamType == GstMediaStream::Remote)) {
        return m_element;
    }

    GRefPtr<GstElementFactory> factory = gst_element_factory_find(factoryKey().utf8().data());
    if (!factory)
        return 0;

    GRefPtr<GstElement> source(gst_element_factory_create(factory.get(), "device_source"));
    if (!source)
        return 0;

    LOG_MEDIA_MESSAGE("sourceInfo.m_device=%s", device().utf8().data());
    // TODO: Device choosing is not implemented for gstreamer 1.0 yet.

    GRefPtr<GstElement> bin;
    if (type() == MediaStreamSource::Audio)
        bin = createAudioSourceBin(source);
    else if (type() == MediaStreamSource::Video)
        bin = createVideoSourceBin(source);

    srcPad = adoptGRef(gst_element_get_static_pad(bin.get(), "src"));
    return bin;
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
