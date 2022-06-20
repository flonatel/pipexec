/*
 * Logging
 *
 * Copyright 2015,2022 by Andreas Florath
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "src/logging.h"

#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <syslog.h>
#include <assert.h>

/**
 * Logging is done by means of an additional file descriptor
 * which can be passed in by command line parameter.
 */
int g_log_text_fd = -1;
int g_log_text_use_syslog = 0;
int g_log_json_fd = -1;
int g_log_json_use_syslog = 0;

void logging_text_set_global_log_fd(int fd) {
   g_log_text_fd = fd;
}

void logging_text_set_global_use_syslog() {
  g_log_text_use_syslog = 1;
}

void logging_json_set_global_log_fd(int fd) {
   g_log_json_fd = fd;
}

void logging_json_set_global_use_syslog() {
  g_log_json_use_syslog = 1;
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

static unsigned long log_fd_write_kv(
   char * buf, unsigned long free_bytes,
   char const * const key, char const * const value) {
  return snprintf(buf, free_bytes, "[%s]=[%s];", key, value);
}

static unsigned long log_fd_write_str(
   char * buf, unsigned long free_bytes,
   char const * const s) {
  return snprintf(buf, free_bytes, "%s;", s);
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
void logging_text(enum logid lid, char const * const type,
		  char const * const serverity,
		  char const * const msg,
		  unsigned int const count, va_list * va) {

  unsigned int free_bytes = 1024;
  char pbuf[free_bytes];
  char * cbuf = pbuf;
  unsigned int written_bytes;

  written_bytes = log_fd_write_time(cbuf, free_bytes);
  cbuf += written_bytes; free_bytes -= written_bytes;
  written_bytes = log_fd_write_pname_and_pid(cbuf, free_bytes);
  cbuf += written_bytes; free_bytes -= written_bytes;

  ITOCHAR(slogid, 20, (int)lid);
  written_bytes = log_fd_write_str(cbuf, free_bytes, slogid);
  cbuf += written_bytes; free_bytes -= written_bytes;

  written_bytes = log_fd_write_str(cbuf, free_bytes, type);
  cbuf += written_bytes; free_bytes -= written_bytes;

  written_bytes = log_fd_write_str(cbuf, free_bytes, serverity);
  cbuf += written_bytes; free_bytes -= written_bytes;

  written_bytes = log_fd_write_str(cbuf, free_bytes, msg);
  cbuf += written_bytes; free_bytes -= written_bytes;

  for(unsigned int idx = 0; idx < count; ++idx) {
    char const * const key = va_arg(*va, char*);
    char const * const value = va_arg(*va, char*);
    written_bytes = log_fd_write_kv(cbuf, free_bytes, key, value);
    cbuf += written_bytes; free_bytes -= written_bytes;
  }
  written_bytes = log_fd_write_newline(cbuf, free_bytes);
  cbuf += written_bytes; free_bytes -= written_bytes;

  if(g_log_text_fd!=-1) {
    // Ignore the result:
    // What to do when the result shows a failure? Logging?
    ssize_t wr = write(g_log_text_fd, pbuf, cbuf-pbuf);
    (void)wr;
  }

  if(g_log_text_use_syslog) {
    syslog(LOG_PID | LOG_DAEMON, "%s", pbuf);
  }
}

static void json_add_kv_str(char * buf, unsigned int * used_bytes,
			    unsigned int * free_bytes,
			    char const * const key, char const * const value) {
  int slen = snprintf(buf + *used_bytes, *free_bytes,
		      "\"%s\":\"%s\"", key, value);
  *used_bytes += slen;
  *free_bytes -= slen;
}

static void json_add_kv_d(char * buf, unsigned int * used_bytes,
			  unsigned int * free_bytes,
			  char const * const key, unsigned int value) {
  int slen = snprintf(buf + *used_bytes, *free_bytes, "\"%s\":%d", key, value);
  *used_bytes += slen;
  *free_bytes -= slen;
}

static void json_add_kv_ld(char * buf, unsigned int * used_bytes,
			   unsigned int * free_bytes,
			   char const * const key, unsigned long value) {
  int slen = snprintf(buf + *used_bytes, *free_bytes, "\"%s\":%ld", key, value);
  *used_bytes += slen;
  *free_bytes -= slen;
}

static void json_add_comma(char * buf, unsigned int * used_bytes,
			   unsigned int * free_bytes) {
  buf[*used_bytes] = ',';
  ++(*used_bytes);
  --(*free_bytes);
}

void logging_json(enum logid lid, char const * const type,
		  char const * const serverity, char const * const msg,
		  unsigned int const count, va_list * va) {

  unsigned int free_bytes = 4096;
  unsigned int used_bytes = 0;
  char pbuf[free_bytes];

  // Initial '{'
  pbuf[0] = '{';
  ++used_bytes;
  --free_bytes;

  json_add_kv_ld(pbuf, &used_bytes, &free_bytes, "timestamp", time(0));
  json_add_comma(pbuf, &used_bytes, &free_bytes);

  json_add_kv_d(pbuf, &used_bytes, &free_bytes, "pipexec_pid", getpid());
  json_add_comma(pbuf, &used_bytes, &free_bytes);

  json_add_kv_d(pbuf, &used_bytes, &free_bytes, "id", (int)lid);
  json_add_comma(pbuf, &used_bytes, &free_bytes);

  json_add_kv_str(pbuf, &used_bytes, &free_bytes, "type", type);
  json_add_comma(pbuf, &used_bytes, &free_bytes);
  json_add_kv_str(pbuf, &used_bytes, &free_bytes, "serverity", serverity);
  json_add_comma(pbuf, &used_bytes, &free_bytes);
  json_add_kv_str(pbuf, &used_bytes, &free_bytes, "message", msg);

  for(unsigned int idx = 0; idx < count; ++idx) {
    char const * const key = va_arg(*va, char*);
    char const * const value = va_arg(*va, char*);
    json_add_comma(pbuf, &used_bytes, &free_bytes);
    json_add_kv_str(pbuf, &used_bytes, &free_bytes, key, value);
  }

  assert( free_bytes > 2 );
  pbuf[used_bytes++] = '}';
  pbuf[used_bytes++] = '\n';

  if(g_log_json_fd!=-1) {
    // Ignore the result:
    // What to do when the result shows a failure? Logging?
    ssize_t wr = write(g_log_json_fd, pbuf, used_bytes);
    (void)wr;
  }
}

void logging(enum logid lid, char const * const type,
	     char const * const serverity,
	     char const * const msg,
	     unsigned int const count, ...) {

  va_list ap;
  va_start(ap, count);

  if(g_log_text_fd!=-1 || g_log_text_use_syslog==1) {
    logging_text(lid, type, serverity, msg, count, &ap);
    return;
  }

  if(g_log_json_fd!=-1 || g_log_json_use_syslog==1) {
    logging_json(lid, type, serverity, msg, count, &ap);
    return;
  }

  va_end(ap);
}
