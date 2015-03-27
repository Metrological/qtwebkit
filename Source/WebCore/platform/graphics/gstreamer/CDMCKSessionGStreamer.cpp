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

#include "config.h"

#if ENABLE(ENCRYPTED_MEDIA_V2) && USE(GSTREAMER)

#include "CDM.h"
#include "MediaKeyError.h"
#include "UUID.h"

#include <wtf/text/Base64.h>
#include <wtf/text/StringBuilder.h>

#include "CDMCKSessionGStreamer.h"
#include "MediaPlayerPrivateGStreamer.h"

GST_DEBUG_CATEGORY_EXTERN(webkit_media_player_debug);
#define GST_CAT_DEFAULT webkit_media_player_debug

namespace WebCore {

static GstPadProbeReturn clearkeyCDMBufferProbe (GstPad* pad, GstPadProbeInfo* info, CDMCKSessionGStreamer* cdm)
{
    GstMapInfo map;
    guint16 *ptr;
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (info);

    buffer = gst_buffer_make_writable (buffer);
    
    gst_buffer_map (buffer, &map, GST_MAP_WRITE);

    ptr = (guint16 *) map.data;

    GST_LOG_OBJECT (pad, "received data on DRM pad, descrambling %" G_GSIZE_FORMAT " bytes", map.size);

    GST_MEMDUMP ("before descrambling :", map.data, map.size);

    if (!cdm->decryptData (map.data, (gint) map.size, map.data, (gint*) &map.size))
        GST_WARNING ("descrambling failed");

    GST_MEMDUMP ("after descrambling :", map.data, map.size);

    gst_buffer_unmap (buffer, &map);

    GST_PAD_PROBE_INFO_DATA (info) = buffer;

    return GST_PAD_PROBE_OK;
}

static void clearkeyCDMPadAdded (GstElement* elem, GstPad* pad, CDMCKSessionGStreamer* cdm)
{
    // Install a probe on this pad as well.
    GST_DEBUG_OBJECT (pad, "installing probe on DRM pad");
    cdm->addProbe (pad);
}

CDMCKSessionGStreamer::CDMCKSessionGStreamer(MediaPlayerPrivateGStreamer* parent)
    : m_parent(parent)
    , m_client(NULL)
    , m_sessionId(createCanonicalUUIDString())
    , m_decryptctx(NULL)
{
  // Install pad probes on the DRM element to decrypt data in place.
  installProbes (parent->m_drmElement.get ());
  // Also register to pad-added signal for upcoming pads
  g_signal_connect (G_OBJECT(parent->m_drmElement.get ()), "pad-added", G_CALLBACK (clearkeyCDMPadAdded), this);
}

CDMCKSessionGStreamer::~CDMCKSessionGStreamer ()
{
    if (m_decryptctx != NULL) {
      EVP_CIPHER_CTX_cleanup (m_decryptctx);
      EVP_CIPHER_CTX_free (m_decryptctx);
    }
}

bool CDMCKSessionGStreamer::decryptData (const guint8* in, gint in_length, guint8* out, gint *out_length)
{
    bool ret = false;
    
    if (m_decryptctx) {
        gint written = 0;
        EVP_DecryptInit (m_decryptctx, NULL, NULL, NULL);
        ret = EVP_DecryptUpdate (m_decryptctx, out, &written, in, in_length) == 1;
        *out_length = written;
        ret = EVP_DecryptFinal (m_decryptctx, out + written, &written) == 1;
        *out_length += written;
    }
    
    return ret;
}

void CDMCKSessionGStreamer::addProbe (GstPad *pad)
{
  m_vprobes.push_back (gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback) clearkeyCDMBufferProbe, this, NULL));
}

void CDMCKSessionGStreamer::installProbes (GstElement *elem)
{
    GstIterator* it;
    GValue item = G_VALUE_INIT;

    it = gst_element_iterate_src_pads (elem);

    if (!it || (gst_iterator_next(it, &item)) != GST_ITERATOR_OK) {
        // This takes a reference.
        GstPad* srcpad = GST_PAD (g_value_dup_object (&item));
        
        if (srcpad) {
            addProbe (srcpad);
        
            gst_object_unref (srcpad);
        }
    }
    g_value_unset(&item);
    if (it)
        gst_iterator_free(it);
}

PassRefPtr<Uint8Array> CDMCKSessionGStreamer::generateKeyRequest(const String& mimeType, Uint8Array* initData, String& destinationURL, unsigned short& errorCode, unsigned long& systemCode)
{
    UNUSED_PARAM(mimeType);
    UNUSED_PARAM(errorCode);
    UNUSED_PARAM(systemCode);
    
    GST_DEBUG ("generating license request");
    GST_MEMDUMP ("initdata for license request :", initData->data (), initData->byteLength ());

#if 0 // Google test system does not seem to use the JSON syntax for request and returns the KID directly.
    // Base64 encode
    String kid_base64 = base64Encode (reinterpret_cast<const char*>(initData->data ()), initData->byteLength ());
    // Remove padding chars
    kid_base64 = kid_base64.substring (0, kid_base64.find ('='));
    // Replace some chars
    kid_base64 = kid_base64.replace ('+', '-');
    kid_base64 = kid_base64.replace ('/', '_');
    CString request_ascii = String ("{ \"kids\":[\"" + kid_base64 + "\"], \"type\":\"temporary\" }").ascii ();
    GST_MEMDUMP ("license request :", reinterpret_cast<const unsigned char*>(request_ascii.data ()), request_ascii.length ());
    PassRefPtr<Uint8Array> result = Uint8Array::create(reinterpret_cast<const unsigned char*>(request_ascii.data ()), request_ascii.length ());
#endif
    PassRefPtr<Uint8Array> result = Uint8Array::create(initData->data (), initData->byteLength ());
    
    return result;
}

void CDMCKSessionGStreamer::releaseKeys()
{
    
}

bool CDMCKSessionGStreamer::initializeCipher(const unsigned char* key_data, int key_data_len)
{
    unsigned char iv[16];
    
    memset (iv, '\0', 16);
    
    if (m_decryptctx != NULL) {
      EVP_CIPHER_CTX_cleanup (m_decryptctx);
      EVP_CIPHER_CTX_free (m_decryptctx);
    }
    m_decryptctx = EVP_CIPHER_CTX_new ();

    EVP_CIPHER_CTX_init(m_decryptctx);

    EVP_DecryptInit(m_decryptctx, EVP_aes_128_ctr(), key_data, iv);

    return true;
}

bool CDMCKSessionGStreamer::update(Uint8Array* key, RefPtr<Uint8Array>& nextMessage, unsigned short& errorCode, unsigned long& systemCode)
{
    bool ret = false;
    
    UNUSED_PARAM(systemCode);
    
    GST_DEBUG ("update license status");
    GST_MEMDUMP ("key received :", key->data (), key->byteLength ());
    
    // Initialize cipher with key
    if (initializeCipher (key->data (), key->byteLength ())) {
      ret = true;
    } else {
      GST_WARNING ("failed initializing cipher with this key");
      errorCode = MediaKeyError::MEDIA_KEYERR_CLIENT;
    }
    
    // Notify the player instance that a key was added
    m_parent->signalDRM ();
    
    return ret;
}

}

#endif
