#include "ExportMacros.h"
#include "Assertions.h"

// For sysLogF
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <stdarg.h>
#include "DataLog.h"

namespace WTF {

// Defining it here with WTF_EXPORT_PRIVATE will make it available in the qtbrowser.
// The version in SysLog.h is without macros so qtbrowser use it without knowing about these macros.
WTF_EXPORT_PRIVATE void openSysLog(const char* logTag);
WTF_EXPORT_PRIVATE void closeSysLog();
WTF_EXPORT_PRIVATE void sysLogF(const char* format, ...) WTF_ATTRIBUTE_PRINTF(1, 2);

static bool isSyslogOpen_ = false;

void openSysLog(const char* logTag)
{
  openlog(logTag, LOG_ODELAY, LOG_USER);
  isSyslogOpen_ = true;
}

void closeSysLog()
{
  if(isSyslogOpen_ == true)
  {
    closelog();
    isSyslogOpen_ = false;
  }
}

void sysLogF(const char* format, ...)
{
  va_list argList;
  va_start(argList, format);

  if (isSyslogOpen_ == true)
  {
    const int    priority = LOG_INFO;
    const size_t SysLogBufferSize_ = 256;
    char buffer[SysLogBufferSize_ + 1];

    vsnprintf(buffer, SysLogBufferSize_, format, argList);
    syslog(priority, "%s", buffer);
  }
  else
  {
      dataLogFV(format, argList);
  }

  va_end(argList);
}

}

