/*
 * Copyright (C) 2014-2015 FLUENDO S.A. All rights reserved.
 * Copyright (C) 2014-2015 METROLOGICAL All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY FLUENDO S.A. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL FLUENDO S.A. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef CDMCKSessionGStreamer_h
#define CDMCKSessionGStreamer_h

#if ENABLE(ENCRYPTED_MEDIA_V2) && USE(GSTREAMER)

#include "CDMSession.h"
#include <wtf/PassOwnPtr.h>
#include <wtf/RetainPtr.h>

#include <gst/gst.h>

#include <openssl/evp.h>

namespace WebCore {

class MediaPlayerPrivateGStreamer;

class CDMCKSessionGStreamer : public CDMSession {
public:
    CDMCKSessionGStreamer(MediaPlayerPrivateGStreamer* parent);
    virtual ~CDMCKSessionGStreamer() OVERRIDE;

    virtual void setClient(CDMSessionClient* client) OVERRIDE { m_client = client; }
    virtual const String& sessionId() const OVERRIDE { return m_sessionId; }
    virtual PassRefPtr<Uint8Array> generateKeyRequest(const String& mimeType, Uint8Array* initData, String& destinationURL, unsigned short& errorCode, unsigned long& systemCode) OVERRIDE;
    virtual void releaseKeys() OVERRIDE;
    virtual bool update(Uint8Array*, RefPtr<Uint8Array>& nextMessage, unsigned short& errorCode, unsigned long& systemCode) OVERRIDE;

    void addProbe (GstPad *pad);
    bool decryptData (const guint8* in, gint in_length, guint8* out, gint *out_length);

private:
    MediaPlayerPrivateGStreamer* m_parent;
    CDMSessionClient* m_client;
    String m_sessionId;
    std::vector<ulong> m_vprobes; 
    EVP_CIPHER_CTX* m_decryptctx;

    void installProbes (GstElement* elem);
    bool initializeCipher(const unsigned char* key_data, int key_data_len);
};

}

#endif

#endif // CDMCKSessionGStreamer_h
