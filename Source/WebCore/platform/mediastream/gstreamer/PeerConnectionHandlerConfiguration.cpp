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

#include "PeerConnectionHandlerConfiguration.h"

#include <wtf/text/CString.h>
#include <gio/gio.h>

namespace WebCore {

static bool isCarriageReturnOrLineFeed(UChar c)
{
    return c == '\r' || c == '\n';
}

PassRefPtr<PeerConnectionHandlerConfiguration> PeerConnectionHandlerConfiguration::create(const String& serverConfiguration, const String& username)
{
    size_t endOfLine = serverConfiguration.find(WebCore::isCarriageReturnOrLineFeed);
    String serverConfig = endOfLine == notFound ? serverConfiguration : serverConfiguration.left(endOfLine);

    Vector<String> configurationComponents;
    serverConfig.split(' ', configurationComponents);

    RefPtr<PeerConnectionHandlerConfiguration> configuration = PeerConnectionHandlerConfiguration::create();

    if (configurationComponents.size() < 2)
        return configuration;

    String& type = configurationComponents[0];
    if (type == "STUN" || type == "STUNS")
        configuration->type = PeerConnectionHandlerConfiguration::TypeSTUN;
    else if (type == "TURN" || type == "TURNS")
        configuration->type = PeerConnectionHandlerConfiguration::TypeTURN;
    else
        return configuration;

    configuration->secure = type == "STUNS" || type == "TURNS";

    String& hostAndPort = configurationComponents[1];
    size_t firstColon = hostAndPort.find(':');
    if (firstColon != notFound) {
        bool portOk;
        int port = hostAndPort.substring(firstColon + 1).toInt(&portOk);
        portOk &= 0 <= port && port < 65536;
        if (!portOk)
            return PeerConnectionHandlerConfiguration::create();

        configuration->host = hostAndPort.left(firstColon);
        configuration->port = port;
    } else
        configuration->host = hostAndPort;

    GResolver* resolver = g_resolver_get_default();
    GError *error = NULL;
    GList *addresses = g_resolver_lookup_by_name(resolver, configuration->host.utf8().data(), NULL, &error);
    g_object_unref(resolver);

    if (error) {
        g_warning ("Error resolving server '%s': %s", configuration->host.utf8().data(), error->message);
        g_error_free(error);
    } else {
        if (g_list_length(addresses) > 0) {
            GInetAddress* addr = reinterpret_cast<GInetAddress*>(addresses->data);
            gchar* cAddr = g_inet_address_to_string(addr);
            configuration->host = String(cAddr);
            g_debug("Resolved host '%s' to '%s'", configuration->host.utf8().data(), cAddr);
            g_free(cAddr);
        } else {
            g_warning("Host name '%s' didn't resolve to any valid addresses", configuration->host.utf8().data());
        }
        g_resolver_free_addresses(addresses);
    }

    configuration->username = username;

    return configuration;
}

}

#endif
