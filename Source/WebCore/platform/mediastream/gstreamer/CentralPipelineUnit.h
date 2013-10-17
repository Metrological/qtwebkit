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


#ifndef CentralPipelineUnit_h
#define CentralPipelineUnit_h

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#include <wtf/HashMap.h>
#include <wtf/gobject/GRefPtr.h>
#include <wtf/text/StringHash.h>

typedef struct _GstElement GstElement;
typedef struct _GstMessage GstMessage;
typedef struct _GstPad GstPad;

namespace WebCore {
class MediaStreamSourceGStreamer;

class CentralPipelineUnit {
    WTF_MAKE_NONCOPYABLE(CentralPipelineUnit);
protected:
    CentralPipelineUnit();
public:
    virtual ~CentralPipelineUnit();

    static CentralPipelineUnit& instance();

    bool handleMessage(GstMessage*);

private:
    class Source {
    public:
        Source()
            : m_removeWhenNotUsed(true)
        { }

        Source(GRefPtr<GstElement> sourceElement, GRefPtr<GstPad> sourcePad, bool removeWhenNotUsed)
            : m_sourceElement(sourceElement)
            , m_sourcePad(sourcePad)
            , m_removeWhenNotUsed(removeWhenNotUsed)
        { }

        GRefPtr<GstElement> m_sourceElement;
        GRefPtr<GstPad> m_sourcePad;
        bool m_removeWhenNotUsed;
    };

    typedef HashMap<String, Source> PipelineMap;
    typedef HashMap<String, String> SourceIdLookupMap;

    GRefPtr<GstElement> m_pipeline;
    PipelineMap m_pipelineMap;
    SourceIdLookupMap m_sourceIdLookupMap;
};

}

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#endif // CentralPipelineUnit_h
