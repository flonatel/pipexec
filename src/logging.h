#ifndef PIPEXEC_LOGGING_H
#define PIPEXEC_LOGGING_H

/*
 * Logging
 *
 * The logging system writes its output to a given fd.
 *
 * Copyright 2015,2022 by Andreas Florath
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define ITOCHAR(vNaMe, sIzE, vAr) char vNaMe[sIzE]; snprintf(vNaMe, sIzE, "%d", vAr)
#define SIZETTOCHAR(vNaMe, sIzE, vAr) char vNaMe[sIzE]; snprintf(vNaMe, sIzE, "%zu", vAr)

void logging_text_set_global_log_fd(int fd);
void logging_text_set_global_use_syslog();
void logging_json_set_global_log_fd(int fd);
void logging_json_set_global_use_syslog();

enum logid {
  lid_internal = 0,
  lid_command_pid = 1,
  lid_child_exit = 2
};

void logging(enum logid lid,
	     char const * const type,
	     char const * const serverity,
	     char const * const msg,
	     unsigned int const count, ...);

#endif
