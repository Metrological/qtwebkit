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

#include "config.h"

#if ENABLE(MEDIA_STREAM)

#include "SignalingMessage.h"

#include <glib.h>

#include <wtf/text/CString.h>
#include <wtf/text/StringBuilder.h>

//#define RTC_LOG(fmt, args...) printf(fmt "\n", ##args)
#define RTC_LOG(fmt, args...) void(0)
#define RTC_LOG_FENTER() (void) 0
#define RTC_LOG_FLEAVE() (void) 0

namespace WebCore {

SignalingMessage::SignalingMessage(long sessionId, long sessionVersion)
    : m_username(g_get_user_name())
    , m_sessionId(sessionId)
    , m_sessionVersion(sessionVersion)
    , m_networkType("IN")
{
}

Vector<String> SignalingMessage::labels()
{
    Vector<String> labels;
    for(size_t i = 0; i < m_mediaDescriptions.size(); i++) {
        if (!labels.contains(m_mediaDescriptions[i]->label()))
            labels.append(m_mediaDescriptions[i]->label());
    }
    return labels;
}

Vector<MediaDescription*> SignalingMessage::mediaDescriptions(String label)
{
    Vector<MediaDescription*> mds;
    for(size_t i = 0; i < m_mediaDescriptions.size(); i++) {
        if (label == m_mediaDescriptions[i]->label()) {
            mds.append(m_mediaDescriptions[i]);
        }
    }
    return mds;
}

void SignalingMessage::address(String& addrType, String& address) const
{
    if (!m_addrType.isEmpty() && !m_address.isEmpty()) {
        addrType = m_addrType;
        address = m_address;
        return;
    }

    if (m_mediaDescriptions.isEmpty())
        return;

    address = m_mediaDescriptions.first()->connectionAddress();
    addrType = (strchr(address.utf8().data(), ':') == 0) ?  "IP4" : "IP6";
}

static bool matchCR(UChar ch) {
    if (ch == 0x00D)
        return true;
    return false;
}

bool SignalingMessage::setOrigin(const String& line)
{
    RTC_LOG_FENTER();
    if (line[0] != 'o' || line[1] != '=') {
        g_warning("Not a valid a line: %s\n", line.ascii().data());
        return false;
    }
    String suffix = line.substring(2);
    Vector<String> tokens;
    suffix.split(" ", tokens);
    if (tokens.size() != 6) {
        g_warning("Wrong number of fields for origin: %zi <%s>", tokens.size(), suffix.ascii().data());
        return false;
    }
    m_username = tokens[0];
    bool res;
    m_sessionId = tokens[1].toInt64(&res);
    if (!res) {
        g_warning("invalid session id: %s", tokens[1].ascii().data());
    }

    m_sessionVersion = tokens[2].toInt64(&res);
    if (!res) {
        g_warning("invalid session version: %s", tokens[2].ascii().data());
    }
    // network type should always be "IN"
    g_warn_if_fail(tokens[3] == "IN");
    m_addrType = tokens[4];
    m_address = tokens[5];

    RTC_LOG_FLEAVE();
    return true;
}

SignalingMessage* createSignalingMessage(const String& msg)
{
    RTC_LOG_FENTER();

    // FIXME: temporary?
    String sdp = msg.removeCharacters(matchCR);
    String cLine = "";

    SignalingMessage* signalingMessage = new SignalingMessage();
    MediaDescription* mediaDescription = 0;
    bool succeeded = true;
    Vector<String> lines;
    //sdp.split("\r\n", lines);
    sdp.split("\n", lines);
    RTC_LOG("Number of lines in SDP: %d\n", (int)lines.size());
    if (lines.size() == 0)
        succeeded = false;
    Vector<String>::iterator lineIt = lines.begin();
    Vector<String>::iterator linesEndIt = lines.end();
    for (; lineIt != linesEndIt && succeeded; ++lineIt) {
        String& line = *lineIt;
        if (line[1] != '=') {
            RTC_LOG("Failed parsing line in sdp: %s\n", line.ascii().data());
            succeeded = false;
            break;
        }
        UChar token = line[0];
        switch(token) {
        case 'm':
            mediaDescription = createMediaDescription(line);
            if (mediaDescription) {
                signalingMessage->mediaDescriptions().append(mediaDescription);
                if (cLine != "")
                    setConnection(mediaDescription, cLine);
            } else
                succeeded = false;
            break;
        case 'a':
            if (!mediaDescription)
                continue;
            succeeded = addAttribute(mediaDescription, line);
            break;
        case 'c':
            if (!mediaDescription) {
                cLine = line;
                continue;
            }
            succeeded = setConnection(mediaDescription, line);
            break;
        case 'b':
            if (!mediaDescription) // FIXME: actually we should set if b=CT here..
                continue;
            succeeded = setMediaBandwidth(mediaDescription, line);
            break;
        case 'o':
            signalingMessage->setOrigin(line);
            break;
        default:
            RTC_LOG("Found unrecognized token. Ignoring\n");
            //IGNORE
            break;
        }

        if (!succeeded)
            break;
    }

    if (!succeeded) {
        RTC_LOG("FAILED PARSING\n");
        if (lines.size()) RTC_LOG("Failed parsing line in sdp: %s\n", (*lineIt).ascii().data());
        if (mediaDescription) delete mediaDescription;
        return 0;
    }

    // FIXME: cleanup when failing..

    RTC_LOG_FLEAVE();
    return signalingMessage;
}


String formatSdp(SignalingMessage* signalingMessage)
{
    StringBuilder builder;
    builder.append("v=0\r\n");

    String addr, addrType;
    signalingMessage->address(addrType, addr);
    builder.append(String::format("o=%s %li %li IN %s %s\n", signalingMessage->username().ascii().data(), signalingMessage->sessionId(), signalingMessage->sessionVersion(), addrType.ascii().data(), addr.ascii().data()));
    builder.append("s= \n");
    builder.append(String::format("c=IN %s %s\n", addrType.ascii().data(), addr.ascii().data()));
    builder.append("t=0 0\n");

    Vector<MediaDescription*>::iterator mdIt = signalingMessage->mediaDescriptions().begin();
    while (mdIt != signalingMessage->mediaDescriptions().end()) {
        MediaDescription* md = *mdIt;
        mdIt++;

        builder.append(formatMediaDescription(md));
    }

    String sdp = builder.toString();
    RTC_LOG("Created sdp:\n%s", sdp.utf8().data());

    return sdp;
}


}

#endif
