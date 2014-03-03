/*
 * Copyright (C) 2011 Ericsson AB. All rights reserved.
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

#ifndef SignalingMessage_h
#define SignalingMessage_h

#if ENABLE(MEDIA_STREAM)

#include "MediaDescription.h"

#include <wtf/text/WTFString.h>
#include <wtf/Vector.h>

namespace WebCore {

class SignalingMessage {
    friend SignalingMessage* createSignalingMessage(const String& sdp);
public:

    SignalingMessage(long sessionId = 0, long sessionVersion = 0);

    //FIXME: delete all MediaDescription objects
    ~SignalingMessage() {};

    Vector<String> labels();
    Vector<MediaDescription*> mediaDescriptions(String label);
    Vector<MediaDescription*>& mediaDescriptions() { return m_mediaDescriptions; };

    bool setOrigin(const String& line);

    long sessionId() const { return m_sessionId; }
    long sessionVersion() const { return m_sessionVersion; }
    const String networkType() const { return m_networkType; }
    const String username() const { return m_username; }
    void address(String& addrType, String& address) const;

private:
    Vector<MediaDescription*> m_mediaDescriptions;
    String m_username;
    int64_t m_sessionId;
    int64_t m_sessionVersion;
    const String m_networkType;
    String m_addrType;
    String m_address;

    // TODO: Connection stuff here as well?

};

SignalingMessage* createSignalingMessage(const String& sdp);
String formatSdp(SignalingMessage* signalingMessage);

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

#endif // SignalingMessage_h
