#ifndef TC2_TEST_ANDROID_LOG_H
#define TC2_TEST_ANDROID_LOG_H

#define ANDROID_LOG_ERROR 6
#define ANDROID_LOG_INFO 4
int __android_log_print(int priority,const char *tag,const char *format,...);

#endif
