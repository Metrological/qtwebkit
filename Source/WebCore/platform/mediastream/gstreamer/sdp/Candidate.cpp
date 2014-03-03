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

#include "Candidate.h"

#include <wtf/text/CString.h>
#include <wtf/text/StringBuilder.h>

//#define RTC_LOG(fmt, args...) printf(fmt "\n", ##args)
#define RTC_LOG(fmt, args...) (void) 0
#define RTC_LOG_FENTER() (void) 0
#define RTC_LOG_FLEAVE() (void) 0

namespace WebCore {

static bool addCandidateAttribute(Candidate* candidate, const String& candidateSuffix);

Candidate::Candidate() :
    m_componentId(0),
    m_candidateType(Host),
    m_port(0),
    m_prio(0),
    m_relAddress(false),
    m_relPort(false),
    m_protocol(Candidate::UDP)
{
}

Candidate* createCandidate(const String& candidateAttribute) {
    RTC_LOG_FENTER();

    String candidateSuffix = candidateAttribute.substring(12);
    RTC_LOG("a=candidate: suffix is %s. whole line is %s\n", candidateSuffix.ascii().data(), candidateAttribute.ascii().data());

    Candidate* candidate = new Candidate();
    bool succeeded = addCandidateAttribute(candidate, candidateSuffix);
    if (!succeeded) {
        delete candidate;
        candidate = 0;
    }

    RTC_LOG_FLEAVE();
    return candidate;
}


static bool addCandidateAttribute(Candidate* candidate, const String& candidateSuffix)
{
    RTC_LOG("candidateSuffix is %s\n", candidateSuffix.ascii().data());
    Vector<String> tokens;
    candidateSuffix.split(" ", tokens);

    Vector<String>::iterator tokenIt = tokens.begin();
    Vector<String>::iterator tokenItEnd = tokens.end();

    if (tokenIt == tokenItEnd)
        return false;

    bool wasInt = false;

    String& foundationStr = *tokenIt++;
    candidate->setFoundation(foundationStr);

    if (tokenIt == tokenItEnd)
        return false;
    String& componentIdStr = *tokenIt++;
    unsigned int componentId = componentIdStr.toUInt(&wasInt);
    if (!wasInt)
        return false;
    candidate->setComponentId(componentId);

    if (tokenIt == tokenItEnd)
        return false;
    String& transport = *tokenIt++;
    if (transport == "UDP")
        candidate->setProtocol(Candidate::UDP);
    else if (transport == "TCP")
        candidate->setProtocol(Candidate::TCP);
    else
        return false;
    // FIXME: Save this?

    if (tokenIt == tokenItEnd)
        return false;
    String& priorityStr = *tokenIt++;
    unsigned int priority = priorityStr.toUInt(&wasInt);
    if (!wasInt)
        return false;
    candidate->setPriority(priority);

    if (tokenIt == tokenItEnd)
        return false;
    String& connectionAddress = *tokenIt++;
    candidate->setAddress(connectionAddress);
    if (tokenIt == tokenItEnd)
        return false;

    if (tokenIt == tokenItEnd)
        return false;
    String& portStr = *tokenIt++;
    unsigned int port = portStr.toUInt(&wasInt);
    if (!wasInt || port > 65535)
        return false;
    candidate->setPort(static_cast<unsigned short>(port));

    if (tokenIt == tokenItEnd)
            return false;
    String& typStr = *tokenIt++;
    if (typStr != "typ")
        return false;

    if (tokenIt == tokenItEnd)
            return false;
    String& candType = *tokenIt++;
    if (candType == "host")
        candidate->setCandidateType(Candidate::Host);
    else if (candType == "srflx")
        candidate->setCandidateType(Candidate::ServerReflexive);
    else if (candType == "prflx")
        candidate->setCandidateType(Candidate::PeerReflexive);
    else if (candType == "relay")
        candidate->setCandidateType(Candidate::Relayed);
    else
        return false;

    if (tokenIt != tokenItEnd) {
        String& rel = *tokenIt++;
        if (rel == "raddr")
            candidate->setRelatedAddress(true);
            // TODO missing parsing the raddr IP
        else if (rel == "rport")
            candidate->setRelatedPort(true);
            // TODO missing parsing the rport port number
    }

    if (tokenIt != tokenItEnd) {
        String& rel = *tokenIt++;
        if (rel == "raddr")
            candidate->setRelatedAddress(true);
            // TODO missing parsing the raddr IP
        else if (rel == "rport")
            candidate->setRelatedPort(true);
            // TODO missing parsing the rport port number
    }

     return true;
}


String formatCandidate(Candidate* candidate)
{
    static const char* CANDIDATE_TYPE[5] = {"srflx", "prflx", "relay", "host", "multicast"};

    StringBuilder builder;
    builder.append("a=candidate:");
    builder.append(candidate->foundation());
    builder.append(" ");
    builder.append(String::number(candidate->componentId()));
    builder.append(candidate->protocol() == Candidate::UDP ? " UDP " : " TCP ");
    builder.append(String::number(candidate->priority()));
    builder.append(" ");
    builder.append(candidate->address());
    builder.append(" ");
    builder.append(String::number(candidate->port()));
    builder.append(" typ ");
    builder.append(CANDIDATE_TYPE[candidate->candidateType()]);
    return builder.toString();
}

}

#endif
