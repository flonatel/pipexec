#ifndef PIPEXEC_LOGGING_H
#define PIPEXEC_LOGGING_H

/*
 * Logging
 *
 * The logging system writes its output to a given fd.
 */

void logging_set_global_log_fd(int fd);
void logging_set_global_use_syslog();
void logging(char const * fmt, ...);

#endif
