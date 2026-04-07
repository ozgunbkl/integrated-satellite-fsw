#ifndef YAMCS_LOG_H
#define YAMCS_LOG_H

int YAMCS_LogPrintf(const char *fmt, ...);
int __wrap_printf(const char *fmt, ...);

#endif
