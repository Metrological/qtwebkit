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

#include "MediaDescription.h"

#include <wtf/text/CString.h>
#include <wtf/text/StringBuilder.h>

//#define RTC_LOG(fmt, args...) printf(fmt "\n", ##args)
#define RTC_LOG(fmt, args...) (void) 0
#define RTC_LOG_FENTER() (void) 0
#define RTC_LOG_FLEAVE() (void) 0

namespace WebCore {

const char* MediaDescription::RtcpProfileSpecificParameterString[3] = {"ack", "nack", "ccm fir"};


void MediaDescription::addRtcpProfileSpecificParameter(RtcpProfileSpecificParameter parameter) {
    RTC_LOG_FENTER();
    Vector<RtcpProfileSpecificParameter>::iterator paramIt = m_rtcpProfileSpecificParameters.begin();
    while (paramIt != m_rtcpProfileSpecificParameters.end()) {
        if (*paramIt == parameter)
            return;
        paramIt++;
    }
    m_rtcpProfileSpecificParameters.append(parameter);

    RTC_LOG_FLEAVE();
}


MediaDescription* createMediaDescription(const String& mLine) {
    RTC_LOG_FENTER();
    if (mLine[0] != 'm' || mLine[1] != '=') {
        RTC_LOG("Not a valid m line: %s\n", mLine.ascii().data());
        return 0;
    }

    String mSuffix = mLine.substring(2);
    Vector<String> tokens;
    mSuffix.split(" ", tokens);

    if (tokens.size() < 3) {
        RTC_LOG("Invalid number of tokens in line: %s\n", mLine.ascii().data());
        return 0;
    }

    Vector<String>::iterator tokenIt = tokens.begin();

    MediaDescription* md = new MediaDescription();

    String mediaType = *tokenIt++;
    if (mediaType == "text")
        md->setMediaType(MediaDescription::Text);
    else if (mediaType == "audio")
        md->setMediaType(MediaDescription::Audio);
    else if (mediaType == "video")
        md->setMediaType(MediaDescription::Video);
    else {
        RTC_LOG("Invalid media type (%s) on line: %s\n", mediaType.ascii().data(), mLine.ascii().data());
        delete md;
        return 0;
    }

    bool succeeded = false;
    String portStr = *tokenIt++;
    unsigned int port = portStr.toUInt(&succeeded);
    if (!succeeded || port > 0xFFFF) {
        RTC_LOG("Found invalid port (%s) on line: %s\n", portStr.ascii().data(), mLine.ascii().data());
        delete md;
        return 0;
    }
    md->setPort(port);

    String& transportType = *tokenIt++;

    if (transportType == "udp")
        md->setMediaTransportType(MediaDescription::UDP);
    else if (transportType == "RTP/AVP")
        md->setMediaTransportType(MediaDescription::RTP_AVP);
    else if (transportType == "RTP/AVPF")
        md->setMediaTransportType(MediaDescription::RTP_AVPF);
    else {
        RTC_LOG("Invalid transport type (%s) on line: %s\n", transportType.ascii().data(), mLine.ascii().data());
        delete md;
        return 0;
    }


    if (port != 0 && (md->mediaTransportType() == MediaDescription::RTP_AVP || md->mediaTransportType() == MediaDescription::RTP_AVPF)) {
        if (tokens.size() < 4) {
            delete md;
            return 0;
        }
        String& pltn = *tokenIt++;
        bool isPayloadType = false;
        pltn.toUInt(&isPayloadType);
        if (port != 0 && !isPayloadType) {
            RTC_LOG("Expected payload type number, found %s on line %s\n", pltn.ascii().data(), mLine.ascii().data());
            delete md;
            return 0;
        }
    }

    RTC_LOG_FLEAVE();
    return md;
}


bool addAttribute(MediaDescription* mediaDescription, const String& aLine) {
    RTC_LOG_FENTER();
    RTC_LOG("line = %s\n", aLine.ascii().data());
    if (aLine[0] != 'a' || aLine[1] != '=') {
        RTC_LOG("Not a valid a line: %s\n", aLine.ascii().data());
        return false;
    }

    String aSuffix = aLine.substring(2);
    Vector<String> tokens;
    aSuffix.split(":", tokens);
    String& attribute = tokens[0];

    if (attribute == "label") {
        RTC_LOG("Label found on line: %s\n", aLine.ascii().data());
        if (tokens.size() != 2) {
            RTC_LOG("Failed to parse label attribute\n");
            return false;
        }
        String& label = tokens[1];
        mediaDescription->setLabel(label);
    } else if (attribute == "rtpmap") {
        Payload* payload = createPayload(aLine);
        if (payload)
            mediaDescription->payloads().append(payload);
        else {
            RTC_LOG("Failed parsing payload line: %s\n", aLine.ascii().data());
            return false;
        }
    } else if (attribute == "fmtp") {
        String fmtpSuffix = aLine.substring(7);
        int firstSpaceIndex = fmtpSuffix.find(" ");
        String format = fmtpSuffix.left(firstSpaceIndex);
        String formatSpecificParameters = fmtpSuffix.substring(firstSpaceIndex+1);
        bool wasInt = false;
        unsigned int payloadTypeNumber = format.toUInt(&wasInt);
        if (!wasInt) {
            RTC_LOG("Failed parsing fmtp line: %s\n", aLine.ascii().data());
            return false;
        }

        Vector<Payload*>& payloads = mediaDescription->payloads();
        Vector<Payload*>::iterator plIt = payloads.begin();
        while (plIt != payloads.end()) {
            Payload* payload = *plIt;
            plIt++;
            if (payload->payloadTypeNumber() == payloadTypeNumber) {
                payload->setFormat(formatSpecificParameters);
                break;
            }
        }


    } else if (attribute == "candidate") {
        RTC_LOG("Found candidate attribute");
        Candidate* candidate = createCandidate(aLine);
        if (candidate)
            mediaDescription->candidates().append(candidate);
        else {
            RTC_LOG("Failed parsing candidate line: %s\n", aLine.ascii().data());
            return false;
        }
    } else if (attribute == "ice-ufrag") {
        if (tokens.size() > 1) {
            String username = tokens[1];
            mediaDescription->setUsername(username);
        }
    } else if (attribute == "ice-pwd") {
        if (tokens.size() > 1) {
            String password = tokens[1];
            mediaDescription->setPassword(password);
        }
    } else if (attribute == "sendonly") {
        mediaDescription->setDirection(MediaDescription::SendOnly);
    } else if (attribute == "recvonly") {
        mediaDescription->setDirection(MediaDescription::RecvOnly);
    } else if (attribute == "sendrecv") {
        mediaDescription->setDirection(MediaDescription::SendRecv);
    } else if (attribute == "rtcp-fb") {
        RTC_LOG("rtcp-fb NOT IMPLEMENTED.. Has not been needed.");
        // FIXME: Should store in the vector
    } else if (attribute == "framesize") {
        RTC_LOG("Found framesize attribute");
        String framesizeSuffix = aLine.substring(12);
        int firstSpaceIndex = framesizeSuffix.find(" ");
        if (firstSpaceIndex == -1)
            return false;
        String pltn = framesizeSuffix.left(firstSpaceIndex);
        String sizes = framesizeSuffix.substring(firstSpaceIndex+1);
        bool wasInt = false;
        unsigned int payloadTypeNumber = pltn.toUInt(&wasInt);
        if (!wasInt)
            return false;
        int firstDashIndex = sizes.find("-");
        if (firstDashIndex == -1)
            return false;
        String widthStr = sizes.left(firstDashIndex);
        String heightStr = sizes.substring(firstDashIndex+1);
        unsigned int width = widthStr.toUInt(&wasInt);
        if (!wasInt)
            return false;
        unsigned int height = heightStr.toUInt(&wasInt);
        if (!wasInt)
            return false;

        Vector<Payload*>& payloads = mediaDescription->payloads();
        Vector<Payload*>::iterator plIt = payloads.begin();
        while (plIt != payloads.end()) {
            Payload* payload = *plIt;
            plIt++;
            if (payload->payloadTypeNumber() == payloadTypeNumber) {
                payload->setFramesizeWidth(width);
                payload->setFramesizeHeight(height);
                RTC_LOG("Set width to %u and height to %u for payload %u", width, height, payloadTypeNumber);
                break;
            }
        }
    } else {
        RTC_LOG("Unrecognized attribute (%s) on row: %s\n", attribute.ascii().data(), aLine.ascii().data());
        return true;
    }



    RTC_LOG_FLEAVE();
    return true;
}


bool setMediaBandwidth(MediaDescription* mediaDescription, const String& aLine)
{
    RTC_LOG_FENTER();
    RTC_LOG("line = %s\n", aLine.ascii().data());
    if (aLine[0] != 'b' || aLine[1] != '=') {
        RTC_LOG("Not a valid a line: %s\n", aLine.ascii().data());
        return false;
    }

    String aSuffix = aLine.substring(2);
    Vector<String> tokens;
    aSuffix.split(":", tokens);
    String& attribute = tokens[0];
    if (attribute == "TIAS") {
        bool isInt = false;
        unsigned int maxBw = (tokens[1].toUInt(&isInt));
        if (!isInt)
            return false;
        mediaDescription->setMaximumBandwidth(maxBw);
    }

    RTC_LOG_FLEAVE();
    return true;
}


bool setConnection(MediaDescription* mediaDescription, const String& aLine)
{
    RTC_LOG_FENTER();
    if (aLine[0] != 'c' || aLine[1] != '=') {
        RTC_LOG("Not a valid a line: %s\n", aLine.ascii().data());
        return false;
    }
    String aSuffix = aLine.substring(2);
    Vector<String> tokens;
    aSuffix.split(" ", tokens);
    if (tokens.size() != 3) {
        return false;
    }
    mediaDescription->setConnectionAddress(tokens[2]);
    RTC_LOG_FLEAVE();
    return true;
}

const char* getAddressType(const char* ip)
{
    if (!ip)
        return 0;
    return (strchr(ip, ':') == 0) ?  "IP4" : "IP6";
}


String getSdpMid(MediaDescription* md)
{
    static const char* MEDIA_TYPE_TEXT[4] = {"audio", "video", "text", "invalid"};

    return String(MEDIA_TYPE_TEXT[md->mediaType()]);
}

String formatMediaDescription(MediaDescription* md)
{
    static const char* MEDIA_TYPE_TEXT[4] = {"audio", "video", "text", "invalid"};
    static const char* MEDIA_TRANSPORT_TYPE[3] = {"udp", "RTP/AVP", "RTP/AVPF"};
    static const char* DIRECTION[4] = {"unknown", "sendonly", "recvonly", "sendrecv"};

    // TODO: What do we do if mediaType is unknown?

    Vector<Payload*>& payloads = md->payloads();
    Vector<Candidate*>& candidates = md->candidates();

    StringBuilder builder;
    builder.append("m=");
    builder.append(MEDIA_TYPE_TEXT[md->mediaType()]);
    builder.append(" ");
    builder.append(String::number(md->port()));
    builder.append(" ");
    builder.append(MEDIA_TRANSPORT_TYPE[md->mediaTransportType()]);
    if (md->mediaType() == MediaDescription::Text)
        builder.append(" nt\r\n");
    else {
        Vector<Payload*>::iterator plIt = payloads.begin();
        while (plIt != payloads.end()) {
            Payload* payload = *plIt;
            plIt++;
            builder.append(" ");
            builder.append(String::number(payload->payloadTypeNumber()));
        }
        builder.append("\r\n");
    }

    if (md->maximumBandwidth() != 0) {
        builder.append("b=TIAS:");
        builder.append(String::number(md->maximumBandwidth()));
        builder.append("\r\n");
    }

    if (md->port() != 0) {
        if (md->connectionAddress() != "") {
            builder.append("c=IN ");
            builder.append(getAddressType(md->connectionAddress().ascii().data()));
            builder.append(" ");
            builder.append(md->connectionAddress());
            builder.append("\r\n");
        }

        Vector<Payload*>::iterator plIt = payloads.begin();
        while (plIt != payloads.end()) {
            Payload* payload = *plIt;
            plIt++;
            builder.append(formatPayload(payload));
        }

        Vector<Candidate*>::iterator cIt = candidates.begin();
        while (cIt != candidates.end()) {
            Candidate* candidate = *cIt;
            cIt++;
            builder.append(formatCandidate(candidate));
            builder.append("\r\n");
        }

        builder.append("a=");
        builder.append(DIRECTION[md->direction()]);
        builder.append("\r\n");

        builder.append("a=mid:");
        builder.append(MEDIA_TYPE_TEXT[md->mediaType()]);
        builder.append("\r\n");

        if (!md->username().isEmpty()) {
            builder.append("a=ice-ufrag:");
            builder.append(md->username());
            builder.append("\r\n");
        }

        if (!md->password().isEmpty()) {
            builder.append("a=ice-pwd:");
            builder.append(md->password());
            builder.append("\r\n");
        }
    }
    if (!md->label().isEmpty()) {
        builder.append("a=label:");
        builder.append(md->label());
        builder.append("\r\n");
    }
    if (!md->rtcpProfileSpecificParameters().isEmpty()) {
        builder.append("a=rtcp-fb:*");
        const Vector<MediaDescription::RtcpProfileSpecificParameter> rtcpProfileSpecificParameters = md->rtcpProfileSpecificParameters();
        Vector<MediaDescription::RtcpProfileSpecificParameter>::const_iterator paramIt = rtcpProfileSpecificParameters.begin();
        while (paramIt != rtcpProfileSpecificParameters.end()) {
            builder.append(" ");
            builder.append(MediaDescription::RtcpProfileSpecificParameterString[*paramIt]);
            paramIt++;
        }
        builder.append("\r\n");
    }

    return builder.toString();
}



}

#endif
