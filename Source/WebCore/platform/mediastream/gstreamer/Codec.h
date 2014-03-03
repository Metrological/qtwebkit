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


#ifndef Codec_h
#define Codec_h

#if ENABLE(MEDIA_STREAM)

#include <wtf/HashMap.h>
#include <wtf/Vector.h>
#include <gst/gst.h>
//#include <wtf/text/CString.h>
#include <wtf/text/StringHash.h>
#include <wtf/text/WTFString.h>


namespace WebCore
{

class Codec;

class CodecRegistry
{
public:
    CodecRegistry() {};
    ~CodecRegistry();
    bool installCodec(Codec*, unsigned int priority);
    Codec* getCodec(const String& codecName, unsigned int clockrate);
    bool isCodecAvailable(const String& codecName);
    void removeCodec(const String& codecName);
    Vector<Codec*> getAudioCodecList();
    Vector<Codec*> getVideoCodecList();

private:
    typedef HashMap<String, std::pair<Codec*, unsigned int>> CodecMap;
    class CodecPrioritySorter {
    public:
        void insertCodec(Codec*, unsigned int prio);
        Vector<Codec*> getSortedVector();

        Vector<unsigned int> m_prioVector;
        Vector<Codec*> m_codecVector;


    };
    CodecMap m_codecMap;

};



class Codec
{
public:
    typedef enum {
        Audio,
        Video
    } MediaType;

    Codec(const String& name, unsigned int rate, MediaType type, unsigned char payloadTypeNumber, const String& fmtp, unsigned int channels)
    :   m_name(name)
    ,   m_rate(rate)
    ,   m_type(type)
    ,   m_payloadTypeNumber(payloadTypeNumber)
    ,   m_fmtp(fmtp)
    ,   m_channels(channels){}

    Codec(const Codec& codec)
    :   m_name(codec.m_name)
    ,   m_rate(codec.m_rate)
    ,   m_type(codec.m_type)
    ,   m_payloadTypeNumber(codec.m_payloadTypeNumber)
    ,   m_fmtp(codec.m_fmtp)
    ,   m_channels(codec.m_channels){}

    virtual ~Codec() { }

    virtual Codec *clone() = 0;

    String name() const { return m_name; }
    unsigned int rate() const { return m_rate; }
    MediaType type() const { return m_type; }
    unsigned char payloadTypeNumber() const { return m_payloadTypeNumber; }
    String fmtp() const { return m_fmtp; }
    unsigned int channels() const { return m_channels; }

    virtual void setPayloadTypeNumber(unsigned char payloadTypeNumber) { m_payloadTypeNumber = payloadTypeNumber; }
    virtual void setFmtp(const String& fmtp) { m_fmtp = fmtp; }

    virtual GstElement* createEncoderElement() const = 0;
    virtual GstElement* createDecoderElement() const = 0;
    virtual GstElement* createRtpPacketizerElement() const = 0;
    virtual GstElement* createRtpDepacketizerElement() const = 0;

    virtual GstCaps* createRtpCaps() const;

protected:
    String m_name;
    unsigned int m_rate;
    MediaType m_type;
    unsigned char m_payloadTypeNumber;
    String m_fmtp;
    unsigned int m_channels;
};


// Video codecs

class H264Codec : public Codec
{
public:
    H264Codec()
    :   Codec("H264", 90000, Video, 96,
                //"profile-level-id=42c01e;packetization-mode=1"
                ""
                , 1)
    {
        setFmtp(m_fmtp);
    }

    Codec *clone() { return new H264Codec(*this); }

    GstElement* createEncoderElement() const;
    GstElement* createDecoderElement() const;
    GstElement* createRtpPacketizerElement() const;
    GstElement* createRtpDepacketizerElement() const;
    void setFmtp(const String& fmtp);
private:
    Vector<String> m_fmtpParam;
};


class JPEGCodec : public Codec
{
public:
    JPEGCodec()
    :   Codec("JPEG", 90000, Video, 26, "", 1)
    {}

    Codec *clone() { return new JPEGCodec(*this); }

    void setPayloadTypeNumber(unsigned int) {} // 26 is static
    GstElement* createEncoderElement() const;
    GstElement* createDecoderElement() const;
    GstElement* createRtpPacketizerElement() const;
    GstElement* createRtpDepacketizerElement() const;
};


class THEORACodec : public Codec
{
public:
    THEORACodec()
    :   Codec("THEORA", 90000, Video, 97, "", 1)
    {}

    Codec *clone() { return new THEORACodec(*this); }

    GstElement* createEncoderElement() const;
    GstElement* createDecoderElement() const;
    GstElement* createRtpPacketizerElement() const;
    GstElement* createRtpDepacketizerElement() const;
};

class Mpeg4Codec : public Codec
{
public:
    Mpeg4Codec()
    : Codec ("MP4V-ES", 90000, Video, 105, "", 1)
    {}

    Codec *clone() { return new Mpeg4Codec(*this); }

    GstElement* createEncoderElement() const;
    GstElement* createDecoderElement() const;
    GstElement* createRtpPacketizerElement() const;
    GstElement* createRtpDepacketizerElement() const;
};

class VP8Codec : public Codec
{
public:
    VP8Codec()
    : Codec ("VP8", 90000, Video, 106, "", 1)
    {}

    Codec *clone() { return new VP8Codec(*this); }

    GstElement* createEncoderElement() const;
    GstElement* createDecoderElement() const;
    GstElement* createRtpPacketizerElement() const;
    GstElement* createRtpDepacketizerElement() const;
    GstCaps* createRtpCaps() const;
};


// Audio codecs

class SPEEXCodec : public Codec
{
public:
    SPEEXCodec()
    :   Codec("SPEEX", 48000, Audio, 98, "", 1)
    {}

    Codec *clone() { return new SPEEXCodec(*this); }

    GstElement* createEncoderElement() const;
    GstElement* createDecoderElement() const;
    GstElement* createRtpPacketizerElement() const;
    GstElement* createRtpDepacketizerElement() const;
};

class G719Codec : public Codec
{
public:
    G719Codec()
    :   Codec("G719", 48000, Audio, 99, "", 2)
    {}

    Codec *clone() { return new G719Codec(*this); }

    GstElement* createEncoderElement() const;
    GstElement* createDecoderElement() const;
    GstElement* createRtpPacketizerElement() const;
    GstElement* createRtpDepacketizerElement() const;
};


class PCMUCodec : public Codec
{
public:
    PCMUCodec()
    :   Codec("PCMU", 8000, Audio, 0, "", 1)
    {}

    Codec *clone() { return new PCMUCodec(*this); }

    GstElement* createEncoderElement() const;
    GstElement* createDecoderElement() const;
    GstElement* createRtpPacketizerElement() const;
    GstElement* createRtpDepacketizerElement() const;
};


class PCMACodec : public Codec
{
public:
    PCMACodec()
    :   Codec("PCMA", 8000, Audio, 8, "", 1)
    {}

    Codec *clone() { return new PCMACodec(*this); }

    GstElement* createEncoderElement() const;
    GstElement* createDecoderElement() const;
    GstElement* createRtpPacketizerElement() const;
    GstElement* createRtpDepacketizerElement() const;
};


class AMRCodec : public Codec
{
public:
    AMRCodec()
    :   Codec("AMR", 8000, Audio, 103, "", 1)
    {}

    Codec *clone() { return new AMRCodec(*this); }

    GstElement* createEncoderElement() const;
    GstElement* createDecoderElement() const;
    GstElement* createRtpPacketizerElement() const;
    GstElement* createRtpDepacketizerElement() const;
};

class AMRWBCodec : public Codec
{
public:
    AMRWBCodec()
    :   Codec("AMR", 16000, Audio, 104, "", 1)
    {}

    Codec *clone() { return new AMRWBCodec(*this); }

    GstElement* createEncoderElement() const;
    GstElement* createDecoderElement() const;
    GstElement* createRtpPacketizerElement() const;
    GstElement* createRtpDepacketizerElement() const;
    GstCaps* createRtpCaps() const;
};

class OpusCodec : public Codec
{
public:
    OpusCodec()
    :   Codec("OPUS", 48000, Audio, 105, "", 1)
    {}

    Codec *clone() { return new OpusCodec(*this); }

    GstElement* createEncoderElement() const;
    GstElement* createDecoderElement() const;
    GstElement* createRtpPacketizerElement() const;
    GstElement* createRtpDepacketizerElement() const;
};

class CodecOptions
{
public:
    CodecOptions() {};
    CodecOptions(const CodecOptions& options) {m_options = options.m_options;}
    bool parse(const String& options, const String& mediaType);
    String attribute(const String& name);
private:
    typedef HashMap<String, String> OptionMap;
    OptionMap m_options;
};


}


#endif // ENABLE(MEDIA_STREAM)

#endif // Codec_h
