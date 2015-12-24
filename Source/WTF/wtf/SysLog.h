#ifndef SYSLOG_H__
#define SYSLOG_H__

namespace WTF {

  void openSysLog(const char* logTag);
  void closeSysLog();
  void sysLogF(const char* format, ...);

}

#endif // SYSLOG_H__

