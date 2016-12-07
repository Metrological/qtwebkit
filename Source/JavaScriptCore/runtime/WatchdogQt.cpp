/*
 * Copyright (C) 2013 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "Watchdog.h"

#include <QObject>
#include <QThread>
#include <wtf/Functional.h>

namespace JSC {

// Qt version. Inspired by what WorkQueueQt does and also WatchdogMac.cpp


class Watchdog::WorkItemQt : public QObject {
    Q_OBJECT
// Based on cut and pasted code from WebKit2/Platform/qt/WorkQueueQt.cpp
public:
    WorkItemQt(const Function<void()>& function)
        : m_source(0)
        , m_signal(0)
        , m_function(function)
    {
    }

    WorkItemQt(QObject* source, const char* signal, const Function<void()>& function)
        : m_source(source)
        , m_signal(signal)
        , m_function(function)
    {
        connect(m_source, m_signal, SLOT(execute()), Qt::QueuedConnection);
    }

    ~WorkItemQt()
    {
    }

    Q_SLOT void execute()
    {
        m_function();
    }

    Q_SLOT void executeAndDelete()
    {
        execute();
        delete this;
    }

    virtual void timerEvent(QTimerEvent*)
    {
        executeAndDelete();
    }

    Watchdog* m_watchDog;
    QObject* m_source;
    const char* m_signal;
    Function<void()> m_function;
};

void Watchdog::initTimer()
{
    m_workThread = new QThread();
    m_workThread->start();
    m_timer = 0;
    m_itemQt = 0;
}

void Watchdog::destroyTimer()
{
    ASSERT(!m_timer);
    if (m_itemQt) {
        delete m_itemQt;
        m_itemQt = 0;
    }
    m_workThread->exit();
    m_workThread->wait();
    delete m_workThread;
}

void Watchdog::fireTimer()
{
    m_timerDidFire = true;
}

void Watchdog::startTimer(double delayInSecond)
{
    m_itemQt = new Watchdog::WorkItemQt(bind(&Watchdog::fireTimer, this));
    m_timer = m_itemQt->startTimer(static_cast<int>(delayInSecond * 1000));
    m_itemQt->moveToThread(m_workThread);
}

void Watchdog::stopTimer()
{
    m_itemQt->disconnect();
    m_itemQt->killTimer(m_timer);
    m_timer = 0;
    delete m_itemQt;
    m_itemQt = 0;
}

} // namespace JSC
#include "WatchdogQt.moc"
