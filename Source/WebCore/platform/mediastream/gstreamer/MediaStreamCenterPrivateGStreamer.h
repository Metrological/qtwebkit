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


#ifndef MediaStreamCenterPrivateGStreamer_h
#define MediaStreamCenterPrivateGStreamer_h

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#include "MediaStreamSourceGStreamer.h"
#include <wtf/PassOwnPtr.h>

namespace WebCore {

class MediaStreamCenterPrivateGStreamer {
public:
    static PassOwnPtr<MediaStreamCenterPrivateGStreamer> create()
    {
        return adoptPtr(new MediaStreamCenterPrivateGStreamer());
    }

    PassRefPtr<MediaStreamSource> firstSource(MediaStreamSource::Type);

private:
    MediaStreamCenterPrivateGStreamer();

    void discoverDevices(MediaStreamSource::Type);

    MediaStreamSourceGStreamerMap m_sourceMap;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#endif // MediaStreamCenterPrivateGStreamer_h
