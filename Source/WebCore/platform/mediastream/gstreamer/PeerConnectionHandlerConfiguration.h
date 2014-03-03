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

#ifndef PeerConnectionHandlerConfiguration_h
#define PeerConnectionHandlerConfiguration_h

#include "config.h"

#if ENABLE(MEDIA_STREAM)

#include <wtf/PassOwnPtr.h>
#include <wtf/RefCounted.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

struct PeerConnectionHandlerConfiguration : public RefCounted<PeerConnectionHandlerConfiguration> {
public:
    enum Type {
        TypeNONE,
        TypeSTUN,
        TypeTURN
    };

    static PassRefPtr<PeerConnectionHandlerConfiguration> create()
    {
        return adoptRef(new PeerConnectionHandlerConfiguration());
    }

    static PassRefPtr<PeerConnectionHandlerConfiguration> create(const String& serverConfiguration, const String& username);

    Type type;
    bool secure;
    String host;
    int port;
    String username;
    String password;

private:
    PeerConnectionHandlerConfiguration()
        : type(TypeNONE)
        , secure(false)
        , host("")
        , port(-1)
        , username("")
        , password("")
    { }
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

#endif // PeerConnectionHandlerConfiguration_h
