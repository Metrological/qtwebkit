/*
 * Copyright (C) 2011 Ericsson AB. All rights reserved.
 * Copyright (C) 2012 Google Inc. All rights reserved.
 * Copyright (C) 2013 Igalia S.L. All rights reserved.
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

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#include "MediaStreamCenterGStreamer.h"

#include "MediaStreamCenterPrivateGStreamer.h"
#include "MediaStreamCreationClient.h"
#include "MediaStreamPrivate.h"
#include "MediaStreamSourceCapabilities.h"
#include "MediaStreamSource.h"
#include "MediaStreamTrackSourcesRequestClient.h"
#include <wtf/MainThread.h>

namespace WebCore {

MediaStreamCenter& MediaStreamCenter::platformCenter()
{
    ASSERT(isMainThread());
    DEFINE_STATIC_LOCAL(MediaStreamCenterGStreamer, center, ());
    return center;
}

MediaStreamCenterGStreamer::MediaStreamCenterGStreamer()
{
    m_private = MediaStreamCenterPrivateGStreamer::create();
}

MediaStreamCenterGStreamer::~MediaStreamCenterGStreamer()
{
}

void MediaStreamCenterGStreamer::validateRequestConstraints(PassRefPtr<MediaStreamCreationClient> prpClient, PassRefPtr<MediaConstraints> audioConstraints, PassRefPtr<MediaConstraints> videoConstraints)
{
    RefPtr<MediaStreamCreationClient> client = prpClient;
    ASSERT(client);

    // TODO: validate constraints with available devices, see bug #123345.
    UNUSED_PARAM(audioConstraints);
    UNUSED_PARAM(videoConstraints);
    client->constraintsValidated();
}

void MediaStreamCenterGStreamer::createMediaStream(PassRefPtr<MediaStreamCreationClient> prpQueryClient, PassRefPtr<MediaConstraints> audioConstraints, PassRefPtr<MediaConstraints> videoConstraints)
{
    RefPtr<MediaStreamCreationClient> client = prpQueryClient;
    ASSERT(client);

    UNUSED_PARAM(audioConstraints);
    UNUSED_PARAM(videoConstraints);

    Vector<RefPtr<MediaStreamSource>> audioSources;
    Vector<RefPtr<MediaStreamSource>> videoSources;

    if (audioConstraints) {
        // TODO: verify constraints according to registered
        // sources. For now, unconditionally pick the first source, see bug #123345.
        RefPtr<MediaStreamSource> audioSource = m_private->firstSource(MediaStreamSource::Audio);
        if (audioSource) {
            audioSource->reset();
            audioSource->setReadyState(MediaStreamSource::Live);
            audioSources.append(audioSource.release());
        }
    }

    if (videoConstraints) {
        // TODO: verify constraints according to registered
        // sources. For now, unconditionally pick the first source, see bug #123345.
        RefPtr<MediaStreamSource> videoSource = m_private->firstSource(MediaStreamSource::Video);
        if (videoSource) {
            videoSource->reset();
            videoSource->setReadyState(MediaStreamSource::Live);
            videoSources.append(videoSource.release());
        }
    }

    client->didCreateStream(MediaStreamPrivate::create(audioSources, videoSources));
}

bool MediaStreamCenterGStreamer::getMediaStreamTrackSources(PassRefPtr<MediaStreamTrackSourcesRequestClient>)
{
    return false;
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
