/*
 * Copyright (C) 2012 Apple Inc. All rights reserved.
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
#include "HeapStatistics.h"

#include <interpreter/CallFrame.h>
#include "Heap.h"
#include "JSObject.h"
#include "Operations.h"
#include "Options.h"
#include <stdlib.h>
#include "API/APICast.h"
#include "API/OpaqueJSString.h"
#include "API/JSContextRefPrivate.h"
#if OS(UNIX)
#include <sys/resource.h>
#endif
#include <wtf/CurrentTime.h>
#include <wtf/SysLog.h>
#include <wtf/Deque.h>


namespace JSC {

double HeapStatistics::s_startTime = 0.0;
double HeapStatistics::s_endTime = 0.0;
Vector<double>* HeapStatistics::s_pauseTimeStarts = 0;
Vector<double>* HeapStatistics::s_pauseTimeEnds = 0;

#if OS(UNIX) 

void HeapStatistics::initialize()
{
    ASSERT(Options::recordGCPauseTimes());
    s_startTime = WTF::monotonicallyIncreasingTime();
    s_pauseTimeStarts = new Vector<double>();
    s_pauseTimeEnds = new Vector<double>();
}

void HeapStatistics::recordGCPauseTime(double start, double end)
{
    ASSERT(Options::recordGCPauseTimes());
    ASSERT(s_pauseTimeStarts);
    ASSERT(s_pauseTimeEnds);
    s_pauseTimeStarts->append(start);
    s_pauseTimeEnds->append(end);
}

void HeapStatistics::logStatistics()
{
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
#if USE(CF) || OS(UNIX)
    char* vmName = getenv("JSVMName");
    char* suiteName = getenv("JSSuiteName");
    char* benchmarkName = getenv("JSBenchmarkName");
#else
#error "The HeapStatistics module is not supported on this platform."
#endif
    if (!vmName || !suiteName || !benchmarkName)
        WTF::sysLogF("HeapStatistics: {\"max_rss\": %ld", usage.ru_maxrss);
    else
        WTF::sysLogF("HeapStatistics: {\"max_rss\": %ld, \"vm_name\": \"%s\", \"suite_name\": \"%s\", \"benchmark_name\": \"%s\"", 
            usage.ru_maxrss, vmName, suiteName, benchmarkName); 

    if (Options::recordGCPauseTimes()) {
        WTF::sysLogF(", \"pause_times\": [");
        Vector<double>::iterator startIt = s_pauseTimeStarts->begin();
        Vector<double>::iterator endIt = s_pauseTimeEnds->begin();
        if (startIt != s_pauseTimeStarts->end() && endIt != s_pauseTimeEnds->end()) {
            WTF::sysLogF("[%f, %f]", *startIt, *endIt);
            ++startIt;
            ++endIt;
        }
        while (startIt != s_pauseTimeStarts->end() && endIt != s_pauseTimeEnds->end()) {
            WTF::sysLogF(", [%f, %f]", *startIt, *endIt);
            ++startIt;
            ++endIt;
        }
        WTF::sysLogF("], \"start_time\": %f, \"end_time\": %f", s_startTime, s_endTime);
    }
    WTF::sysLogF("}\n");
}

void HeapStatistics::exitWithFailure()
{
    ASSERT(Options::logHeapStatisticsAtExit());
    s_endTime = WTF::monotonicallyIncreasingTime();
    logStatistics();
    exit(-1);
}

void HeapStatistics::reportSuccess()
{
    ASSERT(Options::logHeapStatisticsAtExit());
    s_endTime = WTF::monotonicallyIncreasingTime();
    logStatistics();
}

#else

void HeapStatistics::initialize()
{
}

void HeapStatistics::recordGCPauseTime(double, double)
{
}

void HeapStatistics::logStatistics()
{
}

void HeapStatistics::exitWithFailure()
{
}

void HeapStatistics::reportSuccess()
{
}

#endif // OS(UNIX)

size_t HeapStatistics::parseMemoryAmount(char* s)
{
    size_t multiplier = 1;
    char* afterS;
    size_t value = strtol(s, &afterS, 10);
    char next = afterS[0];
    switch (next) {
    case 'K':
        multiplier = KB;
        break;
    case 'M':
        multiplier = MB;
        break;
    case 'G':
        multiplier = GB;
        break;
    default:
        break;
    }
    return value * multiplier;
}

class StorageStatistics : public MarkedBlock::VoidFunctor {
public:
    StorageStatistics();

    void operator()(JSCell*);

    size_t objectWithOutOfLineStorageCount();
    size_t objectCount();

    size_t storageSize();
    size_t storageCapacity();

private:
    size_t m_objectWithOutOfLineStorageCount;
    size_t m_objectCount;
    size_t m_storageSize;
    size_t m_storageCapacity;
};

inline StorageStatistics::StorageStatistics()
    : m_objectWithOutOfLineStorageCount(0)
    , m_objectCount(0)
    , m_storageSize(0)
    , m_storageCapacity(0)
{
}

inline void StorageStatistics::operator()(JSCell* cell)
{
    if (!cell->isObject())
        return;

    JSObject* object = jsCast<JSObject*>(cell);
    if (hasIndexedProperties(object->structure()->indexingType()))
        return;

    if (object->structure()->isUncacheableDictionary())
        return;

    ++m_objectCount;
    if (!object->hasInlineStorage())
        ++m_objectWithOutOfLineStorageCount;
    m_storageSize += object->structure()->totalStorageSize() * sizeof(WriteBarrierBase<Unknown>);
    m_storageCapacity += object->structure()->totalStorageCapacity() * sizeof(WriteBarrierBase<Unknown>); 
}

inline size_t StorageStatistics::objectWithOutOfLineStorageCount()
{
    return m_objectWithOutOfLineStorageCount;
}

inline size_t StorageStatistics::objectCount()
{
    return m_objectCount;
}

inline size_t StorageStatistics::storageSize()
{
    return m_storageSize;
}

inline size_t StorageStatistics::storageCapacity()
{
    return m_storageCapacity;
}

void HeapStatistics::showObjectStatistics(Heap* heap)
{
    WTF::sysLogF("\n=== Heap Statistics: ===\n");
    WTF::sysLogF("size: %ldkB\n", static_cast<long>(heap->m_sizeAfterLastCollect / KB));
    WTF::sysLogF("capacity: %ldkB\n", static_cast<long>(heap->capacity() / KB));
    WTF::sysLogF("pause time: %lfs\n\n", heap->m_lastGCLength);

    StorageStatistics storageStatistics;
    heap->m_objectSpace.forEachLiveCell(storageStatistics);
    long wastedPropertyStorageBytes = 0;
    long wastedPropertyStoragePercent = 0;
    long objectWithOutOfLineStorageCount = 0;
    long objectsWithOutOfLineStoragePercent = 0;
    if ((storageStatistics.storageCapacity() > 0) && (storageStatistics.objectCount() > 0)) {
        wastedPropertyStorageBytes = static_cast<long>((storageStatistics.storageCapacity() - storageStatistics.storageSize()) / KB);
        wastedPropertyStoragePercent = static_cast<long>(
            (storageStatistics.storageCapacity() - storageStatistics.storageSize()) * 100 / storageStatistics.storageCapacity());
        objectWithOutOfLineStorageCount = static_cast<long>(storageStatistics.objectWithOutOfLineStorageCount());
        objectsWithOutOfLineStoragePercent = objectWithOutOfLineStorageCount * 100 / storageStatistics.objectCount();
    }
    WTF::sysLogF("wasted .property storage: %ldkB (%ld percent)\n", wastedPropertyStorageBytes, wastedPropertyStoragePercent);
    WTF::sysLogF("objects with out-of-line .property storage: %ld (%ld percent)\n", objectWithOutOfLineStorageCount, objectsWithOutOfLineStoragePercent);
}

#if ENABLE(JS_MEMORY_TRACKING)
void HeapStatistics::showAllocBacktrace(Heap *heap, size_t size, void *address)
{
    if (heap->m_computingBacktrace)
        // we got called by an allocation triggered by JSContextCreateBacktrace()
        return;

    CallFrame *topCallFrame = heap->vm()->topCallFrame->removeHostCallFrameFlag();
    //JSContextRef context = topCallFrame?toRef(topCallFrame):0;
    WTF::sysLogF("\n%zu bytes at %p\n", size, address);

    if (topCallFrame) {
        if (*reinterpret_cast<int32_t*>(topCallFrame) == 0xabadcafe) {
            /* dirty hackish workaround */
            WTF::sysLogF("No backtrace: uninitialised top frame\n");
        } else {
            while (topCallFrame && topCallFrame != CallFrame::noCaller()
                    && !topCallFrame->codeBlock()) {
                // We are likely in the process of JITing this function, and
                // getStackTrace() does not support this well, so we'll ignore
                // the top frame(s) and start from the first one to have a code
                // block)
                WTF::sysLogF("No codeblock in frame at %p: ignoring it.\n", topCallFrame);
                topCallFrame = topCallFrame->trueCallerFrame();
            }
            if (topCallFrame && topCallFrame != CallFrame::noCaller()) {
                JSContextRef context = toRef(topCallFrame);
                heap->m_computingBacktrace = true;
                RefPtr<OpaqueJSString> backtrace = adoptRef(JSContextCreateBacktrace(context, 50));
                heap->m_computingBacktrace = false;
                WTF::sysLogF("Backtrace:\n%s\nBacktrace end.\n",
                        backtrace->string().utf8().data());
            }
        }
    }
}
#endif

} // namespace JSC
