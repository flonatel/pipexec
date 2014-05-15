#include "src/logging.h"

#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>

/**
 * Logging is done by means of an additional file descriptor
 * which can be passed in by command line parameter.
 */
int g_log_fd = -1;
int g_log_use_syslog = 0;

void logging_set_global_log_fd(int fd) {
   g_log_fd = fd;
}

void logging_set_global_use_syslog() {
  g_log_use_syslog = 1;
}

static unsigned long log_fd_write_time(char * buf, unsigned long free_bytes) {
   time_t t = time(NULL);
   // No need to use the thread-safe version: we are in the one-threaded
   // universe here.
   struct tm *tmp = localtime(&t);
   return strftime(buf, free_bytes, "%F %T", tmp);
}

static unsigned long log_fd_write_pname_and_pid(
   char * buf, unsigned long free_bytes) {
   return snprintf(buf, free_bytes, ";pipexec;%d;", getpid());
}

static unsigned long log_fd_write_args(
   char * buf, unsigned long free_bytes,
   char const * fmt, va_list ap) {
   return vsnprintf(buf, free_bytes, fmt, ap);
}

static unsigned long log_fd_write_newline(
   char * buf, unsigned long free_bytes) {
   return snprintf(buf, free_bytes, "\n");
}

/**
 * Log the state, events and actions.
 * The format of a logging line contains the date and time,
 * the pid of this process and the passed in parameters.
 */
void logging(char const * fmt, ...) {
   if(g_log_fd==-1 && g_log_use_syslog==0) {
      return;
   }

   va_list ap;
   va_start(ap, fmt);

   unsigned int free_bytes = 1024;
   char pbuf[free_bytes];
   char * cbuf = pbuf;
   unsigned int written_bytes;

   written_bytes = log_fd_write_time(cbuf, free_bytes);
   cbuf += written_bytes; free_bytes -= written_bytes;
   written_bytes = log_fd_write_pname_and_pid(cbuf, free_bytes);
   cbuf += written_bytes; free_bytes -= written_bytes;
   written_bytes = log_fd_write_args(cbuf, free_bytes, fmt, ap);
   cbuf += written_bytes; free_bytes -= written_bytes;
   written_bytes = log_fd_write_newline(cbuf, free_bytes);
   cbuf += written_bytes; free_bytes -= written_bytes;

   if(g_log_fd!=-1) {
     // Ignore the result:
     // What to do when the result shows a failure? Logging?
     ssize_t wr = write(g_log_fd, pbuf, cbuf-pbuf);
     (void)wr;
   }

   if(g_log_use_syslog) {
     syslog(LOG_PID | LOG_DAEMON, "%s", pbuf);
   }
}
