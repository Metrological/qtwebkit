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

#ifndef MediaDescription_h
#define MediaDescription_h

#if ENABLE(MEDIA_STREAM)

#include "Candidate.h"
#include "Payload.h"
#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>


namespace WebCore {


class MediaDescription {
    friend MediaDescription* createMediaDescription(const String& mRow);
public:

    enum Direction {
        Unknown,
        SendOnly,
        RecvOnly,
        SendRecv
    };

    enum MediaType {
        Audio,
        Video,
        Text,
        NotSpecified
    };

    enum MediaTransportType {
        UDP,
        RTP_AVP,
        RTP_AVPF
    };

    enum RtcpProfileSpecificParameter {
        ACK,
        NACK,
        FIR
    };

    static const char* RtcpProfileSpecificParameterString[3];


    MediaDescription()
    :   m_direction(Unknown)
    ,   m_label("")
    ,   m_payloads()
    ,   m_candidates()
    ,   m_mediaType(NotSpecified)
    ,   m_port(0)
    ,   m_mediaTransportType(UDP)
    ,   m_connectionType("")
    ,   m_connectionAddress("")
    ,   m_username("")
    ,   m_password("")
    ,   m_maximumBandwidth(0)
    {};
    ~MediaDescription() {};

    Direction direction() const { return m_direction; }
    void setDirection(Direction direction) { m_direction = direction; }

    String label() const { return m_label; }
    void setLabel(const String& label) { m_label = label;}

    MediaType mediaType() const { return m_mediaType; }
    void setMediaType(MediaType mediaType) { m_mediaType = mediaType; }

    unsigned int port() const { return m_port; }
    void setPort(unsigned int port) { m_port = port; }

    MediaTransportType mediaTransportType() const { return m_mediaTransportType; }
    void setMediaTransportType(MediaTransportType mediaTransportType) { m_mediaTransportType = mediaTransportType; }

    Vector<Payload*>& payloads() { return m_payloads; }
    Vector<Candidate*>& candidates() { return m_candidates; }

    String connectionAddress() const { return m_connectionAddress; }
    void setConnectionAddress( String& connectionAddress) {m_connectionAddress = connectionAddress; }

    String username() const { return m_username; }
    void setUsername(const String& username) { m_username = username; }

    String password() const { return m_password; }
    void setPassword(const String& password) { m_password = password; }

    // FIXME: Could add this for each payload as well.
    void addRtcpProfileSpecificParameter(RtcpProfileSpecificParameter parameter);
    const Vector<RtcpProfileSpecificParameter> rtcpProfileSpecificParameters() const { return m_rtcpProfileSpecificParameters; }

    void setMaximumBandwidth(unsigned int maximumBandwidth) { m_maximumBandwidth = maximumBandwidth; }
    unsigned int maximumBandwidth() const { return m_maximumBandwidth; }


private:
    Direction m_direction;
    String m_label;

    Vector<Payload*> m_payloads;
    Vector<Candidate*> m_candidates;

    MediaType m_mediaType;
    unsigned short m_port;
    MediaTransportType m_mediaTransportType;

    String m_connectionType;
    String m_connectionAddress;

    String m_username;
    String m_password;

    Vector<RtcpProfileSpecificParameter> m_rtcpProfileSpecificParameters;

    unsigned int m_maximumBandwidth;
};

MediaDescription* createMediaDescription(const String& mLine);
bool addAttribute(MediaDescription* mediaDescription, const String& aLine);
bool setConnection(MediaDescription* mediaDescription, const String& aLine);
bool setMediaBandwidth(MediaDescription* mediaDescription, const String& aLine);


String getSdpMid(MediaDescription* md);
String formatMediaDescription(MediaDescription* md);

const char* getAddressType(const char* ip);

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

#endif // MediaDescription_h
