/*
 * pipexec
 *
 * Build up a directed graph of processes and pipes.
 *
 * Copyright 2015,2022 by Andreas Florath
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "src/logging.h"
#include "src/version.h"
#include "src/command_info.h"
#include "src/pipe_info.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

/**
 * Globals
 * Used for communication between signal handler and main program.
 */
volatile int g_restart = 0;
volatile int g_terminate = 0;
volatile int g_kill_child_processes = 0;

/**
 * Should the processes restart - pass in a 1.
 * If the process in the termination phase (e.g. it received itself
 * a signal) - this has no effect.
 */
void set_restart(int rs) {
  if (g_terminate) {
    logging(lid_internal, "status", "warning",
	    "Cannot set restart flag - process will terminate", 0);
    return;
  }
  g_restart = rs;
}

/**
 * If an appropriate signal was sent to this process,
 * it will terminate - indeptendent of the results the childs return
 * during their shutdown.
 */
void set_terminate() {
  g_terminate = 1;
  g_restart = 0;
}

/**
 * An array with the pids of all child processes.
 * If a child is not running (e.g. during cleanup or restart phase)
 * the appropriate entry it set to 0.
 *
 * This needs to be global, because it is also accessed from the
 * interrupt handler.
 */
volatile unsigned int g_child_cnt = 0;
volatile pid_t *g_child_pids = NULL;

/**
 * Unset the given pid.
 */
void child_pids_unset(pid_t cpid) {
  for (unsigned int child_idx = 0; child_idx < g_child_cnt; ++child_idx) {
    if (g_child_pids[child_idx] == cpid) {
      g_child_pids[child_idx] = 0;
      return;
    }
  }
  ITOCHAR(spid, 16, cpid);
  logging(lid_internal, "status", "warning",
	  "child_pids_unset: PID not found in list", 1, "pid", spid);
}

void child_pids_print() {
  int const pilen = 4096;
  char pbuf[pilen];
  pbuf[0] = '[';
  int poffset = 1;
  int plen = pilen - poffset;
  bool first = true;
  for (unsigned int child_idx = 0; child_idx < g_child_cnt; ++child_idx) {
    if (g_child_pids[child_idx] == 0) {
      continue;
    }
    if(! first && plen > 0) {
      pbuf[poffset] = ',';
      ++poffset;
    }
    poffset += snprintf(pbuf + poffset, plen, "%d", g_child_pids[child_idx]);
    plen = pilen - poffset;
    first = false;
  }
  if(plen > 2) {
      pbuf[poffset] = ']';
      pbuf[poffset+1] = '\0';
  }

  logging(lid_internal, "status", "info", "Child pids", 1, "pids", pbuf);
}

void child_pids_kill_all() {
  if(! g_kill_child_processes) {
    logging(lid_internal, "tracing", "info", "Do not kill child processes", 0);
    return;
  }

  for (unsigned int child_idx = 0; child_idx < g_child_cnt; ++child_idx) {
    if (g_child_pids[child_idx] != 0) {
      pid_t const to_kill = g_child_pids[child_idx];
      ITOCHAR(skill, 16, to_kill);
      logging(lid_internal, "tracing", "info", "Sending SIGTERM", 1, "pid", skill);
      kill(to_kill, SIGTERM);
    }
  }
}

void child_pids_wait_all() {
  for (unsigned int child_idx = 0; child_idx < g_child_cnt; ++child_idx) {
    if (g_child_pids[child_idx] != 0) {
      pid_t const to_wait = g_child_pids[child_idx];
      ITOCHAR(swait, 16, to_wait);
      logging(lid_internal, "tracing", "info", "Wait for pid to terminate",
	      1, "pid", swait);
      int status;
      pid_t const rw = waitpid(to_wait, &status, 0);
      if (rw == -1) {
	ITOCHAR(swait, 16, to_wait);
	ITOCHAR(serrno, 16, errno);
	logging(lid_internal, "tracing", "error", "Error waiting", 3,
		"pid", swait, "error", strerror(errno),
		"errno", serrno);
      } else {
	ITOCHAR(spid, 16, to_wait);
	ITOCHAR(sstatus, 16, status);
	ITOCHAR(snormal_exit, 16, WIFEXITED(status));
	ITOCHAR(schild_status, 16, WEXITSTATUS(status));
	ITOCHAR(schild_signaled, 16, WIFSIGNALED(status));
	logging(lid_child_exit, "exec", "info", "child exit", 5,
		"command_pid", spid, "status", schild_status,
		"normal_exit", snormal_exit, "child_status", schild_status,
		"child_signaled", schild_signaled);

        if (WIFSIGNALED(status)) {
          logging(lid_internal, "tracing", "info", "Signaled child",
		  2, "pid", spid, "signaled_with", WTERMSIG(status));
          if (WTERMSIG(status) != SIGTERM) {
	    logging(lid_internal, "tracing", "error",
		    "Child terminated because of a different signal - not SIGTERM "
                    "Do not restart",
		    1, "pid", spid);
            set_terminate();
          }
        }
      }
      child_pids_unset(to_wait);
    }
  }
  logging(lid_internal, "tracing", "debug", "Finished waiting for all children", 0);
}

void child_pids_kill_all_and_wait() {
  child_pids_kill_all();
  child_pids_wait_all();
}

/**
 * Signal Related.
 */

void sh_term(int signum, siginfo_t *siginfo, void *ucontext) {
  (void)siginfo;
  (void)ucontext;

  ITOCHAR(ssignum, 16, signum);
  logging(lid_internal, "signal", "info",
	  "signal terminate handler called - signal received",
	  1, "signal", ssignum);

  // Kill all children and stop
  set_terminate();
  child_pids_kill_all_and_wait();
}

void sh_restart(int signum, siginfo_t *siginfo, void *ucontext) {
  (void)siginfo;
  (void)ucontext;

  ITOCHAR(ssignum, 16, signum);
  logging(lid_internal, "signal", "info",
	  "signal restart handler called - signal received",
	  1, "signal", ssignum);

  // Kill all children and restart
  set_restart(1);
  child_pids_kill_all_and_wait();
}

void install_signal_handler() {

  struct sigaction sa_term;
  sa_term.sa_sigaction = sh_term;
  sigemptyset(&sa_term.sa_mask);
  sa_term.sa_flags = SA_SIGINFO | SA_NODEFER;

  struct sigaction sa_restart;
  sa_restart.sa_sigaction = sh_restart;
  sigemptyset(&sa_restart.sa_mask);
  sa_restart.sa_flags = SA_SIGINFO | SA_NODEFER;

  sigaction(SIGHUP, &sa_restart, NULL);
  sigaction(SIGINT, &sa_term, NULL);
  sigaction(SIGQUIT, &sa_term, NULL);
  sigaction(SIGTERM, &sa_term, NULL);
}

void uninstall_signal_handler() {

  struct sigaction sa_default;
  sa_default.sa_handler = SIG_DFL;
  sigemptyset(&sa_default.sa_mask);
  sa_default.sa_flags = SA_SIGINFO | SA_NODEFER;

  sigaction(SIGHUP, &sa_default, NULL);
  sigaction(SIGINT, &sa_default, NULL);
  sigaction(SIGQUIT, &sa_default, NULL);
  sigaction(SIGTERM, &sa_default, NULL);
}

// Functions using the upper data structures
static void pipe_execv_one(command_info_t const *params,
                           pipe_info_t *const ipipe, size_t const pipe_cnt) {
  pipe_info_dup_in_pipes(ipipe, pipe_cnt, params->cmd_name, 1);

  logging(lid_internal, "exec", "info", "Calling execv",
	  2, "command", params->cmd_name, "path", params->path);
  execv(params->path, params->argv);

  ITOCHAR(serrno, 16, errno);
  logging(lid_internal, "exec", "error", "Calling execv",
	  2, "command", params->cmd_name, "path", params->path,
	  "errno", serrno, "error", strerror(errno));
  abort();
}

static pid_t pipe_execv_fork_one(command_info_t const *params,
                                 pipe_info_t *const ipipe,
                                 size_t const pipe_cnt) {
  command_info_print(params);
  pid_t const fpid = fork();

  if (fpid == -1) {
    ITOCHAR(serrno, 16, errno);
    logging(lid_internal, "exec", "error", "Error during fork()", 2,
	    "errno", serrno, "error", strerror(errno));
    exit(10);
  } else if (fpid == 0) {
    uninstall_signal_handler();
    pipe_execv_one(params, ipipe, pipe_cnt);
    // Neverreached
    abort();
  }

  ITOCHAR(spid, 16, fpid);
  logging(lid_command_pid, "exec", "info", "New child forked", 2,
	  "command", params->cmd_name, "command_pid", spid);
  // fpid>0: parent
  return fpid;
}

int pipe_execv(command_info_t *const icmd, size_t const command_cnt,
               pipe_info_t *const ipipe, size_t const pipe_cnt,
               pid_t *child_pids) {

  pipe_info_block_used_fds(ipipe, pipe_cnt);
  pipe_info_create_pipes(ipipe, pipe_cnt);

  // Looks that messing around with the pipes (storing them and propagating
  // them to all children) is not a good idea.
  // ... but in this case there is no other way....
  for (size_t cidx = 0; cidx < command_cnt; ++cidx) {
    child_pids[cidx] = pipe_execv_fork_one(&icmd[cidx], ipipe, pipe_cnt);
  }

  pipe_info_close_all(ipipe, pipe_cnt);

  return 0;
}

unsigned int next_running_child() {
  for (unsigned int child_idx = 0; child_idx < g_child_cnt; ++child_idx) {
    if (g_child_pids[child_idx] != 0) {
      return child_idx;
    }
  }
  return g_child_cnt;
}

static void usage() {
  fprintf(stderr, "pipexec version %s\n", app_version);
  fprintf(stderr, "%s\n", desc_copyight);
  fprintf(stderr, "%s\n", desc_license);
  fprintf(stderr, "\n");
  fprintf(stderr, "Usage: pipexec [options] -- process-pipe-graph\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr, " -h              display this help\n");
  fprintf(stderr, " -j logfd        set fd which is used for json logging\n");
  fprintf(stderr, " -k              kill all child processes when one \n");
  fprintf(stderr, "                 terminates abnormally\n");
  fprintf(stderr, " -l logfd        set fd which is used for text logging\n");
  fprintf(stderr, " -p pidfile      specify a pidfile\n");
  fprintf(stderr, " -s sleep_time   time to wait before a restart\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "process-pipe-graph is a list of process descriptions\n");
  fprintf(stderr, "                   and pipe descriptions.\n");
  fprintf(stderr, "process description: '[ NAME /path/to/proc <optional args> ]'\n");
  fprintf(stderr, "pipe description: '{NAME1:fd1>NAME2:fd2}'\n");
  exit(1);
}

static void write_pid_file(char const *const pid_file) {
  ITOCHAR(spid, 16, getpid());
  logging(lid_internal, "tracing", "info", "Writing pid file", 2,
	  "pid_file", pid_file, "pid", spid);
  char pbuf[20];
  int const plen = snprintf(pbuf, 20, "%d\n", getpid());
  int const fd =
      open(pid_file, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IRGRP | S_IROTH);
  if (fd == -1) {
    ITOCHAR(serrno, 16, errno);
    logging(lid_internal, "tracing", "error", "Cannot open pid file", 2,
	    "error", strerror(errno), "errno", serrno);
    close(fd);
    return;
  }
  ssize_t const written = write(fd, pbuf, plen);
  if (written != plen) {
    ITOCHAR(serrno, 16, errno);
    logging(lid_internal, "tracing", "error", "Write error writing pid", 2,
	    "error", strerror(errno), "errno", serrno);
  }
  close(fd);
}

static void remove_pid_file(char const *const pid_file) {
  ITOCHAR(spid, 16, getpid());
  logging(lid_internal, "tracing", "info", "Removing pid file", 2,
	  "pid_file", pid_file, "pid", spid);
  int const rval = unlink(pid_file);
  if (rval == -1) {
    ITOCHAR(serrno, 16, errno);
    logging(lid_internal, "tracing", "error", "Cannot remove pid file", 2,
	    "error", strerror(errno), "errno", serrno);
  }
}

int main(int argc, char *argv[]) {

  int sleep_timer = 0;
  char *pid_file = NULL;

  int opt;
  while ((opt = getopt(argc, argv, "hj:kl:p:s:-")) != -1) {
    switch (opt) {
    case 'h':
      usage();
      break;
    case 'j': {
      if(*optarg=='s') {
        logging_json_set_global_use_syslog();
      } else {
        int const logfd = atoi(optarg);
        logging_json_set_global_log_fd(logfd);
      }
    } break;
    case 'k':
      g_kill_child_processes = 1;
      break;
    case 'l': {
      if(*optarg=='s') {
        logging_text_set_global_use_syslog();
      } else {
        int const logfd = atoi(optarg);
        logging_text_set_global_log_fd(logfd);
      }
    } break;
    case 'p':
      pid_file = optarg;
      break;
    case 's':
      sleep_timer = atoi(optarg);
      break;
    case '-':
      // The rest are commands.....
      break;
    default: /* '?' */
      usage();
    }
  }

  if (optind == argc) {
    fprintf(stderr, "Error: No command-pipe given\n");
    usage();
  }

  if(sleep_timer==0) {
    // When there is no restart give - terminate all processes when done
    set_restart(0);
    set_terminate();
  }

  logging(lid_internal, "version", "info", "pipexec", 1, "version", app_version);

  if (pid_file != NULL) {
    write_pid_file(pid_file);
  }

  install_signal_handler();

  unsigned int const command_cnt = command_info_clp_count(optind, argc, argv);
  unsigned int const pipe_cnt = pipe_info_clp_count(optind, argc, argv);

  ITOCHAR(scommand_cnt, 16, command_cnt);
  logging(lid_internal, "command_line", "info", "Number of commands", 1,
	  "command_cnt", scommand_cnt);
  ITOCHAR(spipe_cnt, 16, pipe_cnt);
  logging(lid_internal, "command_line", "info", "Number of pipes", 1,
	  "command_cnt", scommand_cnt);

  unsigned int handled_args = 0;

  command_info_t icmd[command_cnt];
  handled_args += command_info_array_constrcutor(icmd, optind, argc, argv);
  command_info_array_print(icmd, command_cnt);

  pipe_info_t ipipe[pipe_cnt];
  pipe_info_parse(ipipe, optind, argc, argv, '>');
  pipe_info_print(ipipe, pipe_cnt);
  pipe_info_check_for_duplicates(ipipe, pipe_cnt);

  ITOCHAR(shandled_args, 16, handled_args);
  logging(lid_internal, "command_line", "info", "Number of handled args", 1,
	  "handled_args", shandled_args);
  unsigned int const not_processed_args = argc - optind - pipe_cnt - handled_args;
  ITOCHAR(snot_processed_args, 16, not_processed_args);
  logging(lid_internal, "command_line", "info", "Not processed args", 1,
	  "not_processed_args", snot_processed_args);

  if(argc - optind - pipe_cnt - handled_args > 0) {
    logging(lid_internal, "command_line", "error",
	    "Error: rubbish / unparsable parameters given", 0);
    usage();
  }

  // Provide memory for child_pids and initialize.
  pid_t child_pids[command_cnt];
  for (unsigned int i = 0; i < command_cnt; ++i) {
    child_pids[i] = 0;
  }
  g_child_pids = child_pids;
  g_child_cnt = command_cnt;

  bool child_failed = false;

  do {
    if (next_running_child() == command_cnt) {
      set_restart(0);
      ITOCHAR(schild_count, 16, command_cnt);
      logging(lid_internal, "exec", "info", "Start all children", 1,
	      "child_count", schild_count);
      pipe_execv(icmd, command_cnt, ipipe, pipe_cnt, child_pids);
    }

    logging(lid_internal, "exec", "info", "Wait for termination of children", 0);

    while (next_running_child() != command_cnt) {
      // Still running children
      logging(lid_internal, "exec", "info", "Wait for next child to terminate", 0);
      child_pids_print();
      int status;
      logging(lid_internal, "exec", "info", "Calling wait", 0);

      pid_t const cpid = wait(&status);

      if (cpid == -1) {
	ITOCHAR(swait, 16, cpid);
	ITOCHAR(serrno, 16, errno);
	logging(lid_internal, "tracing", "error", "Error waiting", 3,
		"pid", swait, "error", strerror(errno),
		"errno", serrno);
      } else {
	ITOCHAR(spid, 16, cpid);
	ITOCHAR(sstatus, 16, status);
	ITOCHAR(snormal_exit, 16, WIFEXITED(status));
	ITOCHAR(schild_status, 16, WEXITSTATUS(status));
	ITOCHAR(schild_signaled, 16, WIFSIGNALED(status));
	logging(lid_child_exit, "exec", "info", "child exit", 5,
		"command_pid", spid, "status", schild_status,
		"normal_exit", snormal_exit, "child_status", schild_status,
		"child_signaled", schild_signaled);
        child_pids_unset(cpid);

	if( status!=0 ) {
	  child_failed = true;
	}

        if (!WIFEXITED(status) || WIFSIGNALED(status)) {
          logging(lid_internal, "tracing", "warning",
		  "Unnormal termination/signaling of child - restarting", 1,
		  "pid", spid);
          set_restart(1);
          child_pids_kill_all_and_wait();
        }
      }

      logging(lid_internal, "tracing", "debug", "Remaining children", 0);
      child_pids_print();

      if (g_restart && sleep_timer != 0) {
	ITOCHAR(ssleep_timer, 16, sleep_timer);
        logging(lid_internal, "tracing", "info", "Waiting for before restart", 1,
		"sleep_timer", ssleep_timer);
        sleep(sleep_timer);
        logging(lid_internal, "tracing", "info", "Continue restarting", 0);
      }
    }
  } while (g_restart);

  if (pid_file != NULL) {
    remove_pid_file(pid_file);
  }

  logging(lid_internal, "tracing", "info", "exiting", 0);

  return child_failed ? 1 : 0;
}
