/*
 * Copyright (C) 2013 Apple Inc. All rights reserved.
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
 * 3. Neither the name of Google Inc. nor the names of its contributors
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

#if ENABLE(MEDIA_STREAM)

#include "MediaStreamSourceStates.h"

namespace WebCore {

const String& MediaStreamSourceStates::facingMode(MediaStreamSourceStates::VideoFacingMode mode)
{
    DEFINE_STATIC_LOCAL(String, userFacing, ("user", AtomicString::ConstructFromLiteral));
    DEFINE_STATIC_LOCAL(String, environmentFacing, ("environment", AtomicString::ConstructFromLiteral));
    DEFINE_STATIC_LOCAL(String, leftFacing, ("left", AtomicString::ConstructFromLiteral));
    DEFINE_STATIC_LOCAL(String, rightFacing, ("right", AtomicString::ConstructFromLiteral));
    
    switch (mode) {
    case MediaStreamSourceStates::User:
        return userFacing;
    case MediaStreamSourceStates::Environment:
        return environmentFacing;
    case MediaStreamSourceStates::Left:
        return leftFacing;
    case MediaStreamSourceStates::Right:
        return rightFacing;
    case MediaStreamSourceStates::Unknown:
        return emptyString();
    }
    
    ASSERT_NOT_REACHED();
    return emptyString();
}

const String& MediaStreamSourceStates::sourceType(MediaStreamSourceStates::SourceType sourceType)
{
    DEFINE_STATIC_LOCAL(String, none, ("none", AtomicString::ConstructFromLiteral));
    DEFINE_STATIC_LOCAL(String, camera, ("camera", AtomicString::ConstructFromLiteral));
    DEFINE_STATIC_LOCAL(String, microphone, ("microphone", AtomicString::ConstructFromLiteral));
    
    switch (sourceType) {
    case MediaStreamSourceStates::None:
        return none;
    case MediaStreamSourceStates::Camera:
        return camera;
    case MediaStreamSourceStates::Microphone:
        return microphone;
    }
    
    ASSERT_NOT_REACHED();
    return emptyString();
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)
