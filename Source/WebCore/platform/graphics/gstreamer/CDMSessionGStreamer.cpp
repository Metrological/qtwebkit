/*
 * Copyright (C) 2014 FLUENDO S.A. All rights reserved.
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
#include "CDMSessionGStreamer.h"

#if ENABLE(ENCRYPTED_MEDIA_V2) && USE(GSTREAMER)

#include "CDM.h"
#include "CDMSession.h"
#include "MediaKeyError.h"
#include "UUID.h"
#include "MediaPlayerPrivateGStreamer.h"
#include <wtf/text/StringBuilder.h>

#include "dxdrm/DxDrmDebugApi.h"

#include <gst/base/gstbytereader.h>

#include <iostream>
using namespace std;

GST_DEBUG_CATEGORY_EXTERN(webkit_media_player_debug);
#define GST_CAT_DEFAULT webkit_media_player_debug

#define MAX_CHALLENGE_LEN 100000

namespace WebCore {

CDMSessionGStreamer::CDMSessionGStreamer(MediaPlayerPrivateGStreamer* parent)
    : m_parent(parent)
    , m_client(NULL)
    , m_sessionId(createCanonicalUUIDString())
    , m_DxDrmStream(NULL)
{
    DxStatus loaded = DxLoadConfigFile("/etc/dxdrm/dxdrm.config");
    if (loaded != DX_SUCCESS)
        GST_WARNING("DX: ERROR - Discretix configuration file not found");

    EDxDrmStatus status = DxDrmClient_Init();
    if (status != DX_SUCCESS) {
        GST_WARNING("failed to initialize the DxDrmClient (error: %d)", status);
        //printError(status);
//        errorCode = MediaKeyError::MEDIA_KEYERR_CLIENT;
//        return nullptr;
    }
}

CDMSessionGStreamer::~CDMSessionGStreamer ()
{
  if (m_DxDrmStream) {
    DxDrmStream_Close (&m_DxDrmStream);
    m_DxDrmStream = NULL;
  }

  DxDrmClient_Terminate();
}

static const guint8* extractWrmHeader(Uint8Array* initData, guint16* recordLength)
{
    cerr << "(CDMSessionGStreamer::)extractWrmHeader entered" << endl;

    GstByteReader reader;
    guint32 length;
    guint16 recordCount;
    const guint8* data;

    gst_byte_reader_init(&reader, initData->data(), initData->byteLength());

    gst_byte_reader_get_uint32_le(&reader, &length);
    gst_byte_reader_get_uint16_le(&reader, &recordCount);

    for (int i = 0; i < recordCount; i++) {
        guint16 type;
        gst_byte_reader_get_uint16_le(&reader, &type);
        gst_byte_reader_get_uint16_le(&reader, recordLength);

        gst_byte_reader_get_data(&reader, *recordLength, &data);
        // 0x1 => rights management header
        if (type == 0x1) {
            cerr << "(CDMSessionGStreamer::)extractWrmHeader returning data" << endl;
            return data;
        }
    }

    cerr << "(CDMSessionGStreamer::)extractWrmHeader returning nullprt" << endl;
    return nullptr;
}

PassRefPtr<Uint8Array> CDMSessionGStreamer::generateKeyRequest(const String& mimeType, Uint8Array* initData, String& destinationURL, unsigned short& errorCode, unsigned long& systemCode)
{
    UNUSED_PARAM(mimeType);
    UNUSED_PARAM(errorCode);
    UNUSED_PARAM(systemCode);

    cerr << "CDMSessionGStreamer::generateKeyRequest entered" << endl;
    
    // Instantiate Discretix DRM client from init data. This could be the WRMHEADER or a complete ASF header..
    guint16 recordLength;
    const guint8* data = extractWrmHeader(initData, &recordLength);
    EDxDrmStatus status = DxDrmClient_OpenDrmStreamFromData (&m_DxDrmStream, data, recordLength);
    if (status != DX_SUCCESS) {
      cerr << "CDMSessionGStreamer::generateKeyRequest OpenDrmStreamFromData isn't a success: " << status << endl;
      GST_WARNING ("failed creating DxDrmClient from initData (%d)", status);
      errorCode = MediaKeyError::MEDIA_KEYERR_CLIENT;
      return NULL;
    }
    
    // Set Secure Clock
/*    status = DxDrmStream_AdjustClock (m_DxDrmStream, DX_AUTO_NO_UI);
    if (status != DX_SUCCESS) {
      GST_WARNING ("failed setting secure clock (%d)", status);
      errorCode = MediaKeyError::MEDIA_KEYERR_CLIENT;
      systemCode = status;
      return NULL;
    }*/
    
    guint32 challenge_length = MAX_CHALLENGE_LEN;
    gpointer challenge = g_malloc0 (challenge_length);
    
    // Get challenge
    status = DxDrmStream_GetLicenseChallenge (m_DxDrmStream, challenge, (unsigned int *) &challenge_length);
    if (status != DX_SUCCESS) {
      cerr << "CDMSessionGStreamer::generateKeyRequest DxDrmStream_GetLicenseChallenge failed" << endl;
      GST_WARNING ("failed to generate challenge request (%d)", status);
      g_free (challenge);
      errorCode = MediaKeyError::MEDIA_KEYERR_CLIENT;
      systemCode = status;
      return NULL;
    }
    
    // Get License URL
    destinationURL = (const char *) DxDrmStream_GetTextAttribute (m_DxDrmStream, DX_ATTR_SILENT_URL, DX_ACTIVE_CONTENT);
    cerr << "CDMSessionGStreamer::generateKeyRequest Set destination url" << endl;
    
    GST_DEBUG ("destination URL : %s", destinationURL.utf8 ().data ());
    GST_MEMDUMP ("generated license request :", (const guint8 *) challenge, challenge_length);
    
    RefPtr<Uint8Array> result = Uint8Array::create(reinterpret_cast<const unsigned char *> (challenge), challenge_length);
    cerr << "CDMSessionGStreamer::generateKeyRequest Created result" << endl;
    
    //g_free (challenge);
    
    // This is the first stage of license aquisition
    m_waitAck = false;
    
    cerr << "CDMSessionGStreamer::generateKeyRequest leaving" << endl;
    return result;
}

void CDMSessionGStreamer::releaseKeys()
{
    m_parent->signalDRM ();
}

bool CDMSessionGStreamer::update(Uint8Array* key, RefPtr<Uint8Array>& nextMessage, unsigned short& errorCode, unsigned long& systemCode)
{
    GST_MEMDUMP ("response received :", key->data (), key->byteLength ());
    
    bool ret = false;
    bool isAckRequired = false;
    HDxResponseResult responseResult = NULL;
    EDxDrmStatus status;

    if (m_waitAck == false) {
      // Server replied to our license request
      status = DxDrmStream_ProcessLicenseResponse (m_DxDrmStream, key->data (), key->byteLength (), &responseResult, &isAckRequired);
    }
    else {
      // Server replied to our license response acknowledge
      status = DxDrmClient_ProcessServerResponse (key->data (), key->byteLength (), DX_RESPONSE_LICENSE_ACK, &responseResult, &isAckRequired);
      if (isAckRequired) {
        GST_WARNING ("ack required when processing ack of ack !");
      }
    }

    if (status != DX_SUCCESS) {
      GST_WARNING ("failed processing license response (%d)", status);
      goto error;
    }

    if (!m_waitAck && isAckRequired) {
      guint32 challenge_length = MAX_CHALLENGE_LEN;
      gpointer challenge = g_malloc0 (challenge_length);
      
      status = DxDrmClient_GetLicenseAcq_GenerateAck (&responseResult, challenge, (unsigned int *) &challenge_length);
      if (status != DX_SUCCESS) {
        GST_WARNING ("failed generating license ack challenge (%d) response result %p", status, responseResult);
        g_free (challenge);
        goto error;
      }
      
      GST_MEMDUMP ("generated license ack request :", (const guint8 *) challenge, challenge_length);
      
      nextMessage = Uint8Array::create(reinterpret_cast<const unsigned char *> (challenge), challenge_length);
      
      g_free (challenge);
      
      m_waitAck = true;
    }
    else {
      // Notify the player instance that a key was added
      m_parent->signalDRM ();
      ret = true;
    }

    return ret;
  
error:
    errorCode = MediaKeyError::MEDIA_KEYERR_CLIENT;
    systemCode = status;
    // Notify Player that license acquisition failed
    m_parent->signalDRM ();
    
    return ret;
}

bool CDMSessionGStreamer::prepareForPlayback()
{
    EDxDrmStatus result;

    result = DxDrmStream_SetIntent(m_DxDrmStream, DX_INTENT_AUTO_PLAY, DX_AUTO_NO_UI);
    if (result != DX_SUCCESS) {
        GST_WARNING("DX: ERROR - opening stream failed because there are no rights (license) to play the content");
        DxDrmStream_Close(&(m_DxDrmStream));
        return false;
    }

    GST_INFO("DX: playback rights found");

    /*starting consumption of the file - notifying the drm that the file is being used*/
    result = DxDrmFile_HandleConsumptionEvent(m_DxDrmStream, DX_EVENT_START);
    if (result != DX_SUCCESS) {
        GST_WARNING("DX: Content consumption failed");
        DxDrmStream_Close(&(m_DxDrmStream));
        return false;
    }

    GST_INFO("DX: Stream was opened and is ready for playback");
    return true;
}

}

#endif
