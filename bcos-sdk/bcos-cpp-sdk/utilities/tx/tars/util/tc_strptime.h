#ifndef __TC_STRPTIME_H__
#define __TC_STRPTIME_H__


#ifdef TARGET_PLATFORM_WINDOWS

char * strptime(const char *buf, const char *fmt, struct tm *tm);
#endif

#endif

