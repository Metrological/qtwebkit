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

#include "Payload.h"

#include <wtf/text/CString.h>
#include <wtf/text/StringBuilder.h>

//#define RTC_LOG(fmt, args...) printf(fmt "\n", ##args)
#define RTC_LOG(fmt, args...) (void) 0
#define RTC_LOG_FENTER() (void) 0
#define RTC_LOG_FLEAVE() (void) 0

namespace WebCore {

    static bool addPayloadAttribute(Payload* payload, const String& rtpmapSuffix);

    Payload* createPayload(const String& rtpmapLine) {
        String rtpmapSuffix = rtpmapLine.substring(9);
        RTC_LOG("a=rtpmap: suffix is %s. whole line is %s\n", rtpmapSuffix.ascii().data(), rtpmapLine.ascii().data());

        Payload* payload = new Payload();
        bool succeed = addPayloadAttribute(payload, rtpmapSuffix);
        if (!succeed) {
            delete payload;
            payload = 0;
        }

        return payload;
    }

    static bool addPayloadAttribute(Payload* payload, const String& rtpmapSuffix) {
        RTC_LOG("Rtpmap suffix: %s\n", rtpmapSuffix.ascii().data());

        Vector<String> tokens;
        rtpmapSuffix.split(" ", tokens);

        Vector<String>::iterator tokenIt = tokens.begin();
        Vector<String>::iterator tokenItEnd = tokens.end();

        if (tokenIt == tokenItEnd)
            return false;

        bool wasInt = false;

        String& pltnStr = *tokenIt++;
        RTC_LOG("Payloadtype number string is %s\n", pltnStr.ascii().data());
        unsigned int pltn = pltnStr.toUInt(&wasInt);
        if (!wasInt /*|| pltn > 128*/)
            return false;
        payload->setPayloadTypeNumber(pltn);

        if (tokenIt == tokenItEnd)
            return false;
        String& nameAndRate = *tokenIt++;
        Vector<String> nameRateVec;
        nameAndRate.split("/", nameRateVec);
        Vector<String>::iterator mapIt = nameRateVec.begin();
        if (mapIt == nameRateVec.end())
            return false;
        String& codecName = *mapIt;
        payload->setCodecName(codecName);

        mapIt++;
        if (mapIt == nameRateVec.end())
            return false;
        String& rateStr = *mapIt;
        unsigned int rate = rateStr.toUInt(&wasInt);
        if (!wasInt)
            return false;
        payload->setRate(rate);

        mapIt++;
        if (mapIt != nameRateVec.end()) {
            String& channelsStr = *mapIt;
            unsigned int channels = channelsStr.toUInt(&wasInt);
            if (!wasInt)
               return false;
            payload->setChannels(channels);
            RTC_LOG("Channels set to %u", channels);
        }
        return true;
    }

String formatPayload(Payload* payload)
{
    StringBuilder builder;
    builder.append("a=rtpmap:");
    builder.append(String::number(payload->payloadTypeNumber()));
    builder.append(" ");
    builder.append(payload->codecName());
    builder.append("/");
    builder.append(String::number(payload->rate()));
    if (payload->channels() != 1) {
        builder.append("/");
        builder.append(String::number(payload->channels()));
    }
    builder.append("\r\n");
    if (! (payload->format().isNull() || payload->format().isEmpty() || payload->format() == "")) {
        builder.append("a=fmtp:");
        builder.append(String::number(payload->payloadTypeNumber()));
        builder.append(" ");
        builder.append(payload->format());
        builder.append("\r\n");
    }
    unsigned int framesizeWidth = payload->framesizeWidth();
    unsigned int framesizeHeight = payload->framesizeHeight();
    if (framesizeWidth && framesizeHeight) {
        builder.append("a=framesize:");
        builder.append(String::number(payload->payloadTypeNumber()));
        builder.append(" ");
        builder.append(String::number(framesizeWidth));
        builder.append("-");
        builder.append(String::number(framesizeHeight));
        builder.append("\r\n");
    }

    return builder.toString();
}

}

#endif
