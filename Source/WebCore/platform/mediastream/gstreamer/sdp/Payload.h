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

#ifndef Payload_h
#define Payload_h

#if ENABLE(MEDIA_STREAM)

#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

class Payload {
public:

    Payload()
    :   m_rate(0)
    ,   m_payloadTypeNumber(0)
    ,   m_codecName("")
    ,   m_format("")
    ,   m_framesizeWidth(0)
    ,   m_framesizeHeight(0)
    ,   m_channels(1)
    {};
    ~Payload() {};

    unsigned int rate() const { return m_rate; }
    void setRate(unsigned int rate) { m_rate = rate; }

    unsigned char payloadTypeNumber() const { return m_payloadTypeNumber; }
    void setPayloadTypeNumber(unsigned char pltn) {m_payloadTypeNumber = pltn; }

    String codecName() const { return m_codecName; }
    void setCodecName(const String& codecName) { m_codecName = codecName; }

    String format() const { return m_format; }
    void setFormat(const String& format) { m_format = format; }


    void setFramesizeWidth(unsigned int width) { m_framesizeWidth = width; }
    void setFramesizeHeight(unsigned int height) { m_framesizeHeight = height; }
    unsigned int framesizeWidth() const { return m_framesizeWidth; }
    unsigned int framesizeHeight() const { return m_framesizeHeight; }

    void setChannels(unsigned int channels) { m_channels = channels; }
    unsigned int channels() const { return m_channels; }

private:
    unsigned int m_rate;
    unsigned char m_payloadTypeNumber;
    String m_codecName;
    String m_format;
    unsigned int m_framesizeWidth;
    unsigned int m_framesizeHeight;
    unsigned int m_channels;
};

Payload* createPayload(const String& rtpmapLine);

String formatPayload(Payload* payload);

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

#endif // Payload_h
