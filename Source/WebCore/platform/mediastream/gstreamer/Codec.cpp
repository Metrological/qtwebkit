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

#include "Codec.h"

#include "CentralPipelineUnit.h"

#include <wtf/text/CString.h>
#include <wtf/text/StringBuilder.h>

//#define RTC_LOG(fmt, args...) printf(fmt "\n", ##args)
#define RTC_LOG(fmt, args...) (void) 0
#define RTC_LOG_FENTER() (void) 0
#define RTC_LOG_FLEAVE() (void) 0

namespace WebCore
{

String createCodecHash(const String& codecName, unsigned int clockrate)
{
    StringBuilder builder;
    builder.append(codecName + ":");
    builder.append(String::number(clockrate));
    return builder.toString();
}


CodecRegistry::~CodecRegistry()
{
    // delete on all codecs in map
}

bool CodecRegistry::installCodec(Codec* codec, unsigned int priority)
{
    bool success = false;
    GstElement* encoder = codec->createEncoderElement();
    GstElement* decoder = codec->createDecoderElement();
    GstElement* pay = codec->createRtpPacketizerElement();
    GstElement* depay = codec->createRtpDepacketizerElement();
    if (encoder && decoder && pay && depay) {
        std::pair<Codec*, unsigned int> c(codec, priority);
        String hash = createCodecHash(codec->name(), codec->rate());
        m_codecMap.add(hash, c);
        success = true;
        RTC_LOG("Codec %s successfully installed", codec->name().ascii().data());
    } else {
        if (encoder)
            gst_object_unref(encoder);
        if (decoder)
            gst_object_unref(decoder);
        if (pay)
            gst_object_unref(pay);
        if (depay)
            gst_object_unref(depay);
        delete codec;
    }
    return success;
}

Codec* CodecRegistry::getCodec(const String& codecName, unsigned int clockrate)
{
    RTC_LOG("GetCodec called with codecName %s and clockrate %u", codecName.ascii().data(), clockrate);
    String hash = createCodecHash(codecName, clockrate);
    CodecMap::iterator cIt = m_codecMap.find(hash);
    if (cIt == m_codecMap.end()) {
        RTC_LOG("Did not find codec");
        return NULL;
    }
    return cIt->value.first->clone();
}


bool CodecRegistry::isCodecAvailable(const String& codecName)
{
    return m_codecMap.find(codecName) != m_codecMap.end();
}

void CodecRegistry::removeCodec(const String& codecName)
{
    CodecMap::iterator cIt = m_codecMap.find(codecName);
    if (cIt == m_codecMap.end())
        return;
    delete cIt->value.first;
    m_codecMap.remove(cIt);
    //m_codecMap.remove(codecName);
}


Vector<Codec*> CodecRegistry::getAudioCodecList()
{
    CodecPrioritySorter sorter;
    Vector<Codec*> audioCodecVec;
    CodecMap::iterator cIt = m_codecMap.begin();
    while (cIt != m_codecMap.end()) {
        Codec* codec = cIt->value.first;
        unsigned int prio = cIt->value.second;
        ++cIt;

        if (codec->type() == Codec::Audio)
            sorter.insertCodec(codec->clone(), prio);
    }
    return sorter.getSortedVector();
}

Vector<Codec*> CodecRegistry::getVideoCodecList()
{
    CodecPrioritySorter sorter;
    Vector<Codec*> videoCodecVec;
    CodecMap::iterator cIt = m_codecMap.begin();
    while (cIt != m_codecMap.end()) {
        Codec* codec = cIt->value.first;
        unsigned int prio = cIt->value.second;
        ++cIt;
        if (codec->type() == Codec::Video)
            sorter.insertCodec(codec->clone(), prio);
    }
    return sorter.getSortedVector();
}


void CodecRegistry::CodecPrioritySorter::insertCodec(Codec* codec, unsigned int prio)
{
    bool inserted = false;
    for (unsigned int i = 0; i < m_prioVector.size(); i++) {
        if (prio > m_prioVector[i]) {
            m_prioVector.insert(i, prio);
            m_codecVector.insert(i, codec);
            inserted = true;
            break;
        }
    }
    if (!inserted) {
        m_prioVector.append(prio);
        m_codecVector.append(codec);
    }


}

Vector<Codec*> CodecRegistry::CodecPrioritySorter::getSortedVector()
{
    return m_codecVector;
}

GstCaps* Codec::createRtpCaps() const
{
    //FIXME: Some codec might have to overload this
    return gst_caps_new_simple("application/x-rtp",
                               "clock-rate", G_TYPE_INT, m_rate, // FIXME: or clock-rate?
                               "media", G_TYPE_STRING, m_type == Codec::Audio ? "audio" : "video",
                               "payload", G_TYPE_INT, m_payloadTypeNumber,
                               "encoding-name", G_TYPE_STRING, m_name.ascii().data(),
                               "encoding-params", G_TYPE_STRING, "1",
                               "octet-align", G_TYPE_STRING, "1",
                               "crc", G_TYPE_STRING, "0",
                               "robust-sorting", G_TYPE_STRING, "0",
                               "interleaving", G_TYPE_STRING, "0",
                               "channels", G_TYPE_INT, m_channels,
                               NULL);
}


/**
 * H264Codec
 */

GstElement* H264Codec::createEncoderElement() const
{
    GstElement* encoder = gst_element_factory_make("x264enc", 0);
    g_object_set(encoder,"speed-preset", 3,  "rc-lookahead", 1, "sync-lookahead", 0, "threads", 1, "sliced-threads", false, "bitrate", 512, "aud", false, "key-int-max", 30000, NULL);
    Vector<String>::const_iterator paramIt = m_fmtpParam.begin();
    while (paramIt != m_fmtpParam.end()) {
        if ((*paramIt).startsWith("packetization-mode=")) {
            bool isInt = false;
            String pmodeStr = (*paramIt).substring(19);
            RTC_LOG("packetization-mode=%s", pmodeStr.ascii().data());
            int pmode = pmodeStr.toInt(&isInt);
            if (!isInt)
                continue;
            switch (pmode) {
            case 1:
                // FIXME:
                RTC_LOG("configuration for packitization-mode=%u NOT IMPLEMENTED", pmode);
                break;
            case 2:
                // FIXME:
                RTC_LOG("configuration for packitization-mode=%u NOT IMPLEMENTED", pmode);
                break;
            case 0:
            default:
                // FIXME:
                RTC_LOG("configuration for packitization-mode=%u NOT IMPLEMENTED", pmode);
                break;
            }
        } else if ((*paramIt).startsWith("profile-level-id=")) {
            String bytesStr = (*paramIt).substring(17);
            RTC_LOG("byte string is %s", bytesStr.ascii().data());
            if (bytesStr.length() == 6) {
                bool isInt1 = false, isInt2 = false, isInt3 = false;
                String b1Str = bytesStr.substring(0,1);
                String b2Str = bytesStr.substring(2,3);
                String b3Str = bytesStr.substring(4,5);
                char profile_idc = (char) b1Str.toIntStrict(&isInt1, 16);
                char profile_iop = (char) b2Str.toIntStrict(&isInt2, 16);
                char level_idc = (char) b3Str.toIntStrict(&isInt3, 16);
                RTC_LOG("profile_idc = %u, profile_iop = %u, level_idc = %u", profile_idc, profile_iop, profile_iop);
                if (isInt1 && isInt2 && isInt3) {
                    // FIXME:
                    switch (profile_idc) {
                    case 42:
                    default:
                        // Baseline
                        g_object_set(encoder, "profile", 1, NULL);
                    }

                    StringBuilder optBuilder;
                    optBuilder.append("level-idc=");
                    //optString += b3Str;
                    optBuilder.append(String::number(level_idc));
                    String optString = optBuilder.toString();
                    RTC_LOG("Setting option-string to %s", optString.ascii().data());
                    g_object_set(encoder, "option-string", optString.ascii().data(), NULL);
                    RTC_LOG("option-String set!");

                    /*
                    switch (level_idc) {
                    case(11): // level 1.1
                        break;
                    case(30): // level 3.0
                        String optString = "level-"
                        g_object_set(encoder, "option-string", "level-idc="
                    default:
                        break;
                    }
                    */

                }
            }
        }

        paramIt++;
    }


    return encoder;
}

GstElement* H264Codec::createDecoderElement() const
{
    GstElement* dec = gst_element_factory_make("avdec_h264", 0);
    return dec;
}

GstElement* H264Codec::createRtpPacketizerElement() const
{
    GstElement* pay = gst_element_factory_make("rtph264pay", 0);
    if (pay)
        g_object_set(pay, "pt", m_payloadTypeNumber, NULL);
    return pay;
}

GstElement* H264Codec::createRtpDepacketizerElement() const
{
    GstElement *depay = gst_element_factory_make("rtph264depay", 0);
    return depay;
}

void H264Codec::setFmtp(const String& fmtp)
{
    m_fmtp = fmtp;  // FIXME: Might need to remove white spaces
    fmtp.split(";", m_fmtpParam);
}


/**
 * JPEGCodec
 */

GstElement* JPEGCodec::createEncoderElement() const
{
    GstElement* encoder = gst_element_factory_make("jpegenc", 0);
    if (encoder)
        g_object_set(encoder, "quality", 60, NULL);
    return encoder;
}

GstElement* JPEGCodec::createDecoderElement() const
{
    GstElement* dec = gst_element_factory_make("jpegdec", 0);
    return dec;
}

GstElement* JPEGCodec::createRtpPacketizerElement() const
{
    GstElement* pay = gst_element_factory_make("rtpjpegpay", 0);
    if (pay)
        g_object_set(pay, "pt", m_payloadTypeNumber, NULL);
    return pay;
}

GstElement* JPEGCodec::createRtpDepacketizerElement() const
{
    GstElement* depay = gst_element_factory_make("rtpjpegdepay", 0);
    return depay;
}


/**
 * THEORACodec
 */

GstElement* THEORACodec::createEncoderElement() const
{
    GstElement* encoder = gst_element_factory_make("theoraenc", 0);
    return encoder;
}

GstElement* THEORACodec::createDecoderElement() const
{
    GstElement* dec = gst_element_factory_make("theoradec", 0);
    return dec;
}

GstElement* THEORACodec::createRtpPacketizerElement() const
{
    GstElement* pay = gst_element_factory_make("rtptheorapay", 0);
    if (pay)
        g_object_set(pay, "pt", m_payloadTypeNumber, "config-interval", 1, NULL);
    return pay;
}

GstElement* THEORACodec::createRtpDepacketizerElement() const
{
    GstElement* depay = gst_element_factory_make("rtptheoradepay", 0);
    return depay;
}


/**
 * Mpeg4 Codec
 */


GstElement* Mpeg4Codec::createEncoderElement() const
{
    GstElement* encoder = gst_element_factory_make("avenc_mpeg4", 0);
    if (encoder)
        g_object_set(encoder, "gop-size", 1, NULL);
    return encoder;
}

GstElement* Mpeg4Codec::createDecoderElement() const
{
    GstElement* dec = gst_element_factory_make("avdec_mpeg4", 0);
    return dec;
}

GstElement* Mpeg4Codec::createRtpPacketizerElement() const
{
    GstElement* pay = gst_element_factory_make("rtpmp4vpay", 0);
    if (pay)
        g_object_set(pay, "pt", m_payloadTypeNumber, "config-interval", 1, NULL);
    return pay;
}

GstElement* Mpeg4Codec::createRtpDepacketizerElement() const
{
    GstElement* depay = gst_element_factory_make("rtpmp4vdepay", 0);
    return depay;
}

/**
 * VP8 Codec
 */


GstElement* VP8Codec::createEncoderElement() const
{
    GstElement* encoder = gst_element_factory_make("vp8enc", 0);
    // if (encoder)
    //     g_object_set(encoder, "threads", 1, "speed", 2, "max-latency", 2, NULL);
    return encoder;
}

GstElement* VP8Codec::createDecoderElement() const
{
    GstElement* dec = gst_element_factory_make("vp8dec", 0);
    return dec;
}

GstElement* VP8Codec::createRtpPacketizerElement() const
{
    GstElement* pay = gst_element_factory_make("rtpvp8pay", 0);
    if (pay)
        g_object_set(pay, "pt", m_payloadTypeNumber, NULL);
    return pay;
}

GstElement* VP8Codec::createRtpDepacketizerElement() const
{
    GstElement* depay = gst_element_factory_make("rtpvp8depay", 0);
    return depay;
}

GstCaps* VP8Codec::createRtpCaps() const
{
    //FIXME: Some codec might have to overload this
    return gst_caps_new_simple("application/x-rtp",
                               "clock-rate", G_TYPE_INT, m_rate, // FIXME: or clock-rate?
                               "media", G_TYPE_STRING, "video",
                               "payload", G_TYPE_INT, m_payloadTypeNumber,
                               "encoding-name", G_TYPE_STRING, "VP8-DRAFT-0-3-2",
                               NULL);
}


/**
 * SPEEXCodec
 */

GstElement* SPEEXCodec::createEncoderElement() const
{
    GstElement* encoder = gst_element_factory_make("speexenc", 0);
    return encoder;
}

GstElement* SPEEXCodec::createDecoderElement() const
{
    GstElement* dec = gst_element_factory_make("speexdec", 0);
    return dec;
}

GstElement* SPEEXCodec::createRtpPacketizerElement() const
{
    GstElement* pay = gst_element_factory_make("rtpspeexpay", 0);
    if (pay)
        g_object_set(pay, "pt", m_payloadTypeNumber, NULL);
    return pay;
}

GstElement* SPEEXCodec::createRtpDepacketizerElement() const
{
    GstElement* depay = gst_element_factory_make("rtpspeexdepay", 0);
    return depay;
}


/**
 * G719Codec
 */

GstElement* G719Codec::createEncoderElement() const
{
    GstElement* encoder = gst_element_factory_make("g719encoder", 0);
    return encoder;
}

GstElement* G719Codec::createDecoderElement() const
{
    GstElement* dec = gst_element_factory_make("g719decoder", 0);
    return dec;
}

GstElement* G719Codec::createRtpPacketizerElement() const
{
    GstElement* pay = gst_element_factory_make("rtpg719pay", 0);
    if (pay)
        g_object_set(pay, "pt", m_payloadTypeNumber, NULL);
    return pay;
}

GstElement* G719Codec::createRtpDepacketizerElement() const
{
    GstElement* depay = gst_element_factory_make("rtpg719depay", 0);
    return depay;
}

/**
 * PCMUCodec
 */

GstElement* PCMUCodec::createEncoderElement() const
{
    GstElement* encoder = gst_element_factory_make("mulawenc", 0);
    return encoder;
}

GstElement* PCMUCodec::createDecoderElement() const
{
    GstElement* dec = gst_element_factory_make("mulawdec", 0);
    return dec;
}

GstElement* PCMUCodec::createRtpPacketizerElement() const
{
    GstElement* pay = gst_element_factory_make("rtppcmupay", 0);
    if (pay)
        g_object_set(pay, "pt", m_payloadTypeNumber, NULL);
    return pay;
}

GstElement* PCMUCodec::createRtpDepacketizerElement() const
{
    GstElement* depay = gst_element_factory_make("rtppcmudepay", 0);
    return depay;
}



/**
 * PCMACodec
 */

GstElement* PCMACodec::createEncoderElement() const
{
    GstElement* encoder = gst_element_factory_make("alawenc", 0);
    return encoder;
}

GstElement* PCMACodec::createDecoderElement() const
{
    GstElement* dec = gst_element_factory_make("alawdec", 0);
    return dec;
}

GstElement* PCMACodec::createRtpPacketizerElement() const
{
    GstElement* pay = gst_element_factory_make("rtppcmapay", 0);
    if (pay)
        g_object_set(pay, "pt", m_payloadTypeNumber, "max-ptime", (gint64) 20000000, "min-ptime", (gint64) 20000000, NULL);
    return pay;
}

GstElement* PCMACodec::createRtpDepacketizerElement() const
{
    GstElement* depay = gst_element_factory_make("rtppcmadepay", 0);
    return depay;
}



/**
 * AMRCodec
 */

GstElement* AMRCodec::createEncoderElement() const
{
    GstElement* encoder = gst_element_factory_make("amrnbenc", 0);
    if (encoder)
        g_object_set(encoder, "band-mode", 7, NULL); // 12.2 kbps
    return encoder;
}

GstElement* AMRCodec::createDecoderElement() const
{
    GstElement* dec = gst_element_factory_make("amrnbdec", 0);
    return dec;
}

GstElement* AMRCodec::createRtpPacketizerElement() const
{
    GstElement* pay = gst_element_factory_make("rtpamrpay", 0);
    if (pay)
        g_object_set(pay, "pt", m_payloadTypeNumber, NULL);
    return pay;
}

GstElement* AMRCodec::createRtpDepacketizerElement() const
{
    GstElement* depay = gst_element_factory_make("rtpamrdepay", 0);
    return depay;
}




/**
 * AMRWBCodec
 */

GstElement* AMRWBCodec::createEncoderElement() const
{
    GstElement* encoder = gst_element_factory_make("amrwbenc", 0);
    if (encoder)
        g_object_set(encoder, "band-mode", 7, NULL); // 23.85 kbps
    return encoder;
}

GstElement* AMRWBCodec::createDecoderElement() const
{
    GstElement* dec = gst_element_factory_make("amrwbdec", 0);
    return dec;
}

GstElement* AMRWBCodec::createRtpPacketizerElement() const
{
    GstElement* pay = gst_element_factory_make("rtpamrpay", 0);
    if (pay)
        g_object_set(pay, "pt", m_payloadTypeNumber, NULL);
    return pay;
}

GstElement* AMRWBCodec::createRtpDepacketizerElement() const
{
    GstElement* depay = gst_element_factory_make("rtpamrdepay", 0);
    return depay;
}

GstCaps* AMRWBCodec::createRtpCaps() const
{
    //FIXME: Some codec might have to overload this
    return gst_caps_new_simple("application/x-rtp",
                               "clock-rate", G_TYPE_INT, m_rate, // FIXME: or clock-rate?
                               "media", G_TYPE_STRING, "audio",
                               "payload", G_TYPE_INT, m_payloadTypeNumber,
                               "encoding-name", G_TYPE_STRING, "AMR-WB",
                               "encoding-params", G_TYPE_STRING, "1",
                               "octet-align", G_TYPE_STRING, "1",
                               "crc", G_TYPE_STRING, "0",
                               "robust-sorting", G_TYPE_STRING, "0",
                               "interleaving", G_TYPE_STRING, "0",
                               NULL);
}


/**
 * OpusCodec
 */

GstElement* OpusCodec::createEncoderElement() const
{
    GstElement* encoder = gst_element_factory_make("opusenc", 0);
    return encoder;
}

GstElement* OpusCodec::createDecoderElement() const
{
    GstElement* dec = gst_element_factory_make("opusdec", 0);
    return dec;
}

GstElement* OpusCodec::createRtpPacketizerElement() const
{
    GstElement* pay = gst_element_factory_make("rtpopuspay", 0);
    if (pay)
        g_object_set(pay, "pt", m_payloadTypeNumber, NULL);
    return pay;
}

GstElement* OpusCodec::createRtpDepacketizerElement() const
{
    GstElement* depay = gst_element_factory_make("rtpopusdepay", 0);
    return depay;
}




bool CodecOptions::parse(const String& options, const String& mediaType)
{
    Vector<String> optVec;
    options.split(";", optVec);

    Vector<String>::iterator optIt = optVec.begin();
    while (optIt != optVec.end()) {
        String& optionForLabel = *optIt;
        optIt++;

        int colonIndex = optionForLabel.find(":");
        String type = optionForLabel.left(colonIndex);
        String optionsStr = optionForLabel.substring(colonIndex + 1);

        if (type != mediaType) continue;

        Vector<String> singleOptVec;
        optionsStr.split(" ", singleOptVec);
        Vector<String>::iterator soIt = singleOptVec.begin();

        while (soIt != singleOptVec.end()) {
            String& singleOpt = *soIt;
            soIt++;
            int eqIndex = singleOpt.find("=");
            String opt = singleOpt.left(eqIndex);
            String value = singleOpt.substring(eqIndex + 1);
            m_options.add(opt, value);
            RTC_LOG("%s option added: %s = %s", type.ascii().data(), opt.ascii().data(), value.ascii().data());
        }
        // FIXME: error check for out of bounds etc.
    }
    return true;
}

String CodecOptions::attribute(const String& name) {
    String value = "";
    OptionMap::iterator optIt = m_options.find(name);
    if (optIt != m_options.end())
        value = optIt->value;
    return value;
}

}

#endif
