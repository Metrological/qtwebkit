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

#ifndef Candidate_h
#define Candidate_h

#if ENABLE(MEDIA_STREAM)

#include <wtf/text/WTFString.h>

namespace WebCore {

class Candidate {
public:
    enum CandidateType {
        ServerReflexive,
        PeerReflexive,
        Relayed,
        Host,
        Multicast
    };

    enum Protocol {
        UDP,
        TCP
    };

    Candidate();
    ~Candidate() {};

    String foundation() const { return m_foundation; }
    void setFoundation(const String& foundation) { m_foundation = foundation; }

    unsigned int componentId() const { return m_componentId; }
    void setComponentId(unsigned int componentId) { m_componentId = componentId; }

    CandidateType candidateType() const { return m_candidateType; }
    void setCandidateType(CandidateType candidateType) { m_candidateType = candidateType; }

    String address() const { return m_address; }
    void setAddress(const String& address) { m_address = address; }

    unsigned short port() const { return m_port; }
    void setPort(unsigned short port) { m_port = port; }

    unsigned int priority() const { return m_prio; }
    void setPriority(unsigned int prio) { m_prio = prio; }

    bool relatedAddress() const { return m_relAddress; }
    void setRelatedAddress(bool relatedAddress) { m_relAddress = relatedAddress; }

    bool relatedPort() const { return m_relPort; }
    void setRelatedPort(bool relatedPort) { m_relPort = relatedPort; }

    Protocol protocol() const { return m_protocol; }
    void setProtocol(Protocol protocol) { m_protocol = protocol; }

private:
    String m_foundation;
    unsigned int m_componentId;
    CandidateType m_candidateType;
    String m_address;
    unsigned short m_port;
    unsigned int m_prio;
    bool m_relAddress;
    bool m_relPort;
    Protocol m_protocol;
};

Candidate* createCandidate(const String& candidateAttribute);

String formatCandidate(Candidate* candidate);

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

#endif // Candidate_h
