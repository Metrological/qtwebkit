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

#ifndef MediaStreamSourceGStreamer_h
#define MediaStreamSourceGStreamer_h

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#include "GRefPtrGStreamer.h"
#include "MediaStreamSource.h"
#include <wtf/HashMap.h>
#include <wtf/RefPtr.h>
#include "GstMediaStream.h"

namespace WebCore {

class MediaStreamSourceCapabilities;

class MediaStreamSourceGStreamer final : public MediaStreamSource {
public:
MediaStreamSourceGStreamer(GstMediaStream::StreamType streamType, const AtomicString& id, const AtomicString& name, MediaStreamSource::Type type, const String& device, const String& factoryKey, GstElement* element)
    : MediaStreamSource(id, type, name)
    , m_streamType(streamType)
    , m_factoryKey(factoryKey)
    , m_device(device)
    , m_element(element)
    {
    }

    virtual ~MediaStreamSourceGStreamer() { }

    virtual RefPtr<MediaStreamSourceCapabilities> capabilities() const { return m_capabilities; }
    virtual const MediaStreamSourceStates& states() { return m_currentStates; }

    const String& factoryKey() const { return m_factoryKey; }
    const String& device() const { return m_device; }
    GstMediaStream::StreamType streamType() const { return m_streamType; }

    GRefPtr<GstElement> createGStreamerElement(GRefPtr<GstPad>& sourcePad);

private:
    RefPtr<MediaStreamSourceCapabilities> m_capabilities;
    MediaStreamSourceStates m_currentStates;
    GstMediaStream::StreamType m_streamType;
    String m_factoryKey;
    String m_device;
    GstElement* m_element;
};

typedef HashMap<String, RefPtr<MediaStreamSourceGStreamer>> MediaStreamSourceGStreamerMap;

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#endif // MediaStreamSourceGStreamer_h
