/*
 * Copyright (C) 2014-2015 FLUENDO S.A. All rights reserved.
 * Copyright (C) 2014-2015 METROLOGICAL All rights reserved.
 * Copyright (C) 2015 IGALIA S.L All rights reserved.
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
#include "CDMPRSessionGStreamer.h"

#if ENABLE(ENCRYPTED_MEDIA_V2) && USE(GSTREAMER) && USE(DXDRM)

#include "CDM.h"
#include "MediaKeyError.h"
#include "MediaPlayerPrivateGStreamer.h"
#include "UUID.h"

#include <dxdrm/DxDrmClient.h>
#include <dxdrm/DxDrmDebugApi.h>
#include <dxdrm/DxDrmStream.h>

#include <gst/base/gstbytereader.h>

#include <wtf/text/CString.h>
#include <wtf/text/StringBuilder.h>

GST_DEBUG_CATEGORY_EXTERN(webkit_media_playready_decrypt_debug_category);
#define GST_CAT_DEFAULT webkit_media_playready_decrypt_debug_category

#define MAX_CHALLENGE_LEN 64000

namespace WebCore {

class DRMInitialisation
{
    private:
        DRMInitialisation(const DRMInitialisation&);
        DRMInitialisation& operator= (const DRMInitialisation&);

    public:
        DRMInitialisation ()
        {
            DxStatus status = DxLoadConfigFile("/etc/dxdrm/dxdrm.config");
            if (status != DX_SUCCESS)
            {
                GST_WARNING("DX: ERROR - Discretix configuration file not found");
                _status = DX_ERROR_BAD_ARGUMENTS;
            }
            else
            {
                _status = DxDrmClient_Init();
                if (_status != DX_SUCCESS) 
                {
                    GST_WARNING("failed to initialize the DxDrmClient (error: %d)", _status);
                }

                // Set Secure Clock
                /*   _status = DxDrmStream_AdjustClock(m_DxDrmStream, DX_AUTO_NO_UI);
                if (_status != DX_SUCCESS) 
                {
                    GST_WARNING("failed setting secure clock (%d)", _status);
                }
                */
            }
        }
	~DRMInitialisation()
        {
            DxDrmClient_Terminate();
        }

    public:
        inline bool IsInitialised() const
        {
            return (_status == DX_SUCCESS);
        }
        void printError(const EDxDrmStatus status)
        {
            g_printerr("DxDRM Error:\n");

            switch (status) {
            case DX_ERROR_CONTENT_NOT_RECOGNIZED:
                 g_printerr("The specified file is not protected by one of the supported DRM schemes.\n");
                 break;
            case DX_ERROR_NOT_INITIALIZED:
                 g_printerr("The DRM Client has not been initialized.\n");
                 break;
            case DX_ERROR_BAD_ARGUMENTS:
                 g_printerr("Bad arguments.\n");
                 break;
            default:
                 g_printerr("unknown error: %d\n", status);
                 break;
        }
}


    private:
        EDxDrmStatus _status;
};

static DRMInitialisation g_DRMInitialisation;

CDMPRSessionGStreamer::CDMPRSessionGStreamer(MediaPlayerPrivateGStreamer* parent)
    : m_player(parent)
    , m_client(nullptr)
    , m_sessionId()
    , m_DxDrmStream(nullptr)
    , m_key()
    , m_state(PHASE_INITIAL)
{
    if (g_DRMInitialisation.IsInitialised() == true) {
        m_sessionId = createCanonicalUUIDString();
    }
}

CDMPRSessionGStreamer::~CDMPRSessionGStreamer()
{
    if (m_DxDrmStream != nullptr) {
        DxDrmStream_Close(&m_DxDrmStream);
        m_DxDrmStream = nullptr;
    }
}

/* virtual */ CDMSessionType CDMPRSessionGStreamer::type() 
{ 
    return CDMSessionTypeMediaSourcePlayReady; 
}

/* virtual */ void CDMPRSessionGStreamer::setClient(CDMSessionClient* client) 
{ 
    ASSERT ( (m_client == nullptr) ^ (client == nullptr) );

    m_client = client; 
}

/* virtual */ const String& CDMPRSessionGStreamer::sessionId() const 
{ 
    return m_sessionId; 
}

//
// Expected synchronisation from caller. This method is not thread safe!!!!
//
/* virtual */ PassRefPtr<Uint8Array> CDMPRSessionGStreamer::generateKeyRequest(const String& mimeType, Uint8Array* initData, String& destinationURL, unsigned short& errorCode, unsigned long& systemCode) 
{
    UNUSED_PARAM(mimeType);

    RefPtr<Uint8Array> result;

    // Pass initData straight to DxDrm, it will parse the XML WRMHEADER.
    EDxDrmStatus status = DxDrmClient_OpenDrmStreamFromData(&m_DxDrmStream, initData->data(), initData->byteLength());

    if (status != DX_SUCCESS) {
        GST_WARNING("failed creating DxDrmClient from initData (error: %d)", status);
        g_DRMInitialisation.printError(status);
        errorCode = MediaKeyError::MEDIA_KEYERR_CLIENT;
    } else {
        uint32_t challengeLength = MAX_CHALLENGE_LEN;
        unsigned char* challenge = static_cast<unsigned char*> (g_malloc0(challengeLength));

        // Get challenge
        status = DxDrmStream_GetLicenseChallenge(m_DxDrmStream, challenge, &challengeLength);
        if (status != DX_SUCCESS) {
            GST_WARNING("failed to generate challenge request (%d)", status);
            errorCode = MediaKeyError::MEDIA_KEYERR_CLIENT;
        } else {
            // Get License URL
            destinationURL = static_cast<const char *>(DxDrmStream_GetTextAttribute(m_DxDrmStream, DX_ATTR_SILENT_URL, DX_ACTIVE_CONTENT));
            GST_DEBUG("destination URL : %s", destinationURL.utf8().data());

            GST_MEMDUMP("generated license request :", challenge, challengeLength);

            result = Uint8Array::create(challenge, challengeLength);
            errorCode = 0;
        }

        g_free(challenge);
    }

    systemCode = status;

    return result;
}

//
// Expected synchronisation from caller. This method is not thread safe!!!!
//
/* virtual */ bool CDMPRSessionGStreamer::update(Uint8Array* key, RefPtr<Uint8Array>& nextMessage, unsigned short& errorCode, unsigned long& systemCode)
{
    GST_MEMDUMP("response received :", key->data(), key->byteLength());

    bool isAckRequired;
    HDxResponseResult responseResult = nullptr;
    EDxDrmStatus status = DX_ERROR_CONTENT_NOT_RECOGNIZED;

    if (m_state == PHASE_INITIAL)
    {
        // Server replied to our license request
        status = DxDrmStream_ProcessLicenseResponse(m_DxDrmStream, key->data(), key->byteLength(), &responseResult, &isAckRequired);

        if (status  == DX_SUCCESS) {
            // Create a deep copy of the key.
            m_key = key->buffer();
            m_state = (isAckRequired ? PHASE_ACKNOWLEDGE : PHASE_PROVISIONED);
        }

    } else if (m_state == PHASE_ACKNOWLEDGE) {
        // Server replied to our license response acknowledge
        status = DxDrmClient_ProcessServerResponse(key->data(), key->byteLength(), DX_RESPONSE_LICENSE_ACK, &responseResult, &isAckRequired);

        if (status  == DX_SUCCESS) {
            // Create a deep copy of the key.
            m_key   = key->buffer();
            m_state = (isAckRequired ? PHASE_ACKNOWLEDGE : PHASE_PROVISIONED);

            if (m_state == PHASE_ACKNOWLEDGE) {
                GST_WARNING("Acknowledging an Ack. Strange situation.");
            }
        }

    } else {
        GST_WARNING("Unexpected call. We are already provisioned");
    }

    if (status != DX_SUCCESS) {
        GST_WARNING("failed processing license response (%d)", status);
        errorCode = MediaKeyError::MEDIA_KEYERR_CLIENT;
    } else if (m_state == PHASE_PROVISIONED) {
        status = DxDrmStream_SetIntent(m_DxDrmStream, DX_INTENT_AUTO_PLAY, DX_AUTO_NO_UI);
        if (status != DX_SUCCESS) {
            GST_WARNING("DX: ERROR - opening stream failed because there are no rights (license) to play the content");
        } else {
            GST_INFO("DX: playback rights found");

            /* Taken from the "Decryptor, not used there... 
            status =  DxDrmStream_GetFlags(m_DxDrmStream, DX_FLAG_CAN_PLAY | DX_FLAG_HAS_FUTURE_RIGHTS,
                                          &activeFlags, DX_ALL_PERMISSIONS, 0); */

            /* starting consumption of the file - notifying the drm that the file is being used */
            status = DxDrmFile_HandleConsumptionEvent(m_DxDrmStream, DX_EVENT_START);
            if (status != DX_SUCCESS) {
                GST_WARNING("DX: Content consumption failed");
            } else {
                GST_INFO("DX: Stream was opened and is ready for playback");
                m_player->signalDRM();
            }
        }

    } else if (m_state == PHASE_ACKNOWLEDGE) {
        unsigned int challengeLength = MAX_CHALLENGE_LEN;
        unsigned char* challenge = static_cast<unsigned char*>(g_malloc0(challengeLength));

        status = DxDrmClient_GetLicenseAcq_GenerateAck(&responseResult, challenge, &challengeLength);
        if (status != DX_SUCCESS) {
            GST_WARNING("failed generating license ack challenge (%d) response result %p", status, responseResult);
        }

        GST_MEMDUMP("generated license ack request :", challenge, challengeLength);

        nextMessage = Uint8Array::create(challenge, challengeLength);

        g_free(challenge);
    }

    systemCode = status;

    return (status == DX_SUCCESS);
}

/* virtual */ void CDMPRSessionGStreamer::releaseKeys()
{
    m_player->signalDRM();
}

/* virtual */ RefPtr<ArrayBuffer> CDMPRSessionGStreamer::cachedKeyForKeyID(const String& sessionId) const
{
    return (sessionId == m_sessionId ? m_key : nullptr);
}

int CDMPRSessionGStreamer::decrypt (GstMapInfo& map, GstMapInfo& boxMap, const unsigned int sampleIndex, const unsigned int trackId)
{
    EDxDrmStatus status = DxDrmStream_ProcessPiffPacket(
        m_DxDrmStream, 
        static_cast<void*>(map.data),
        static_cast<unsigned int>(map.size),
        static_cast<const void*>(boxMap.data),
        static_cast<unsigned int>(boxMap.size),
        static_cast<unsigned int>(sampleIndex),
        trackId);

    return (status == DX_DRM_SUCCESS ? 0 : status);
}

}

#endif
