/*
 * Copyright (C) 2014 Igalia S.L.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "GMainLoopSource.h"

#if USE(GLIB)

#include <gio/gio.h>

namespace WTF {

GMainLoopSource::Simple::Simple(const char* name)
    : m_source(g_source_new(&m_sourceFunctions, sizeof(GSource)))
    , m_function(nullptr)
    , m_status(Ready)
{
    g_source_set_name(m_source.get(), name);
    g_source_set_callback(m_source.get(), reinterpret_cast<GSourceFunc>(simpleSourceCallback), this, nullptr);

    g_source_attach(m_source.get(), g_main_context_get_thread_default());
}

void GMainLoopSource::Simple::cancel()
{
    ASSERT(m_source);
    g_source_set_ready_time(m_source.get(), -1);
    m_status = Ready;
}

void GMainLoopSource::Simple::schedule(std::chrono::microseconds delay, std::function<void ()> function)
{
    ASSERT(function);
    m_function = std::move(function);

    g_source_set_ready_time(m_source.get(), g_get_monotonic_time() + delay.count());
    m_status = Scheduled;
}

GSourceFuncs GMainLoopSource::Simple::m_sourceFunctions = {
    nullptr, // prepare
    nullptr, // check
    // dispatch
    [](GSource*, GSourceFunc callback, gpointer userData) -> gboolean
    {
        return callback(userData);
    },
    nullptr, // finalize
    nullptr, // closure_callback
    nullptr // closure_marshall
};

gboolean GMainLoopSource::Simple::simpleSourceCallback(GMainLoopSource::Simple* source)
{
    ASSERT(source->m_function);

    g_source_set_ready_time(source->m_source.get(), -1);
    source->m_status = Dispatching;
    source->m_function();
    source->m_status = Ready;
    return G_SOURCE_CONTINUE;
}

} // namespace WTF

#endif // USE(GLIB)
