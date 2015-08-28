/*
 * pipexec
 *
 * Build up a directed graph of processes and pipes.
 *
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
    logging("Cannot set restart flag - process will terminate");
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
  logging("child_pids_unset: PID not found in list [%d]", cpid);
}

void child_pids_print() {
  int const pilen = 1024;
  char pbuf[pilen];
  int plen = pilen;
  int poffset = snprintf(pbuf, pilen, "Child pids: ");
  plen = pilen - poffset;
  for (unsigned int child_idx = 0; child_idx < g_child_cnt; ++child_idx) {
    if (g_child_pids[child_idx] == 0) {
      continue;
    }
    poffset += snprintf(pbuf + poffset, plen, "[%d] ", g_child_pids[child_idx]);
    plen = pilen - poffset;
  }
  logging(pbuf);
}

void child_pids_kill_all() {
  if(! g_kill_child_processes) {
    logging("Do not kill child processes");
    return;
  }

  for (unsigned int child_idx = 0; child_idx < g_child_cnt; ++child_idx) {
    if (g_child_pids[child_idx] != 0) {
      pid_t const to_kill = g_child_pids[child_idx];
      logging("Sending SIGTERM to [%d]", to_kill);
      kill(to_kill, SIGTERM);
    }
  }
}

void child_pids_wait_all() {
  for (unsigned int child_idx = 0; child_idx < g_child_cnt; ++child_idx) {
    if (g_child_pids[child_idx] != 0) {
      pid_t const to_wait = g_child_pids[child_idx];
      logging("Wait for pid [%d] to terminate", to_wait);
      int status;
      pid_t const rw = waitpid(to_wait, &status, 0);
      if (rw == -1) {
        logging("Error waiting for [%d] [%s]", to_wait, strerror(errno));
      } else {
        logging("Child [%d] exit with status [%d] "
                "normal exit [%d] child status [%d] "
                "child signaled [%d]",
                to_wait, status, WIFEXITED(status), WEXITSTATUS(status),
                WIFSIGNALED(status));

        if (WIFSIGNALED(status)) {
          logging("Signaled with [%d]", WTERMSIG(status));
          if (WTERMSIG(status) != SIGTERM) {
            logging("Child terminated because of a different signal. "
                    "Do not restart");
            set_terminate();
          }
        }
      }
      child_pids_unset(to_wait);
    }
  }
  logging("Finished waiting for all children");
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

  logging("signal terminate handler called - signal received [%d]", signum);

  // Kill all children and stop
  set_terminate();
  child_pids_kill_all_and_wait();
}

void sh_restart(int signum, siginfo_t *siginfo, void *ucontext) {
  (void)siginfo;
  (void)ucontext;

  logging("signal restart handler called - signal received [%d]", signum);

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

  logging("[%s] Calling execv [%s]", params->cmd_name, params->path);
  execv(params->path, params->argv);
  perror("execv");
  abort();
}

static pid_t pipe_execv_fork_one(command_info_t const *params,
                                 pipe_info_t *const ipipe,
                                 size_t const pipe_cnt) {

  command_info_print(params);
  pid_t const fpid = fork();

  if (fpid == -1) {
    logging("Error during fork() [%s]", strerror(errno));
    exit(10);
  } else if (fpid == 0) {
    uninstall_signal_handler();
    pipe_execv_one(params, ipipe, pipe_cnt);
    // Neverreached
    abort();
  }

  logging("[%s] New child pid [%d]", params->cmd_name, fpid);
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
  fprintf(stderr, " -k              kill all child processes when one \n");
  fprintf(stderr, "                 terminates abnormally\n");
  fprintf(stderr, " -l logfd        set fd which is used for logging\n");
  fprintf(stderr, " -p pidfile      specify a pidfile\n");
  fprintf(stderr, " -s sleep_time   time to wait before a restart\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "process-pipe-graph is a list of process descriptions\n");
  fprintf(stderr, "                   and pipe descriptions.\n");
  fprintf(stderr, "process description: '[ NAME /path/to/proc ]'\n");
  fprintf(stderr, "pipe description: '{NAME1:fd1>NAME2:fd2}'\n");
  exit(1);
}

static void write_pid_file(char const *const pid_file) {
  logging("Writing pid file [%s]: [%d]", pid_file, getpid());
  char pbuf[20];
  int const plen = snprintf(pbuf, 20, "%d\n", getpid());
  int const fd =
      open(pid_file, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IRGRP | S_IROTH);
  if (fd == -1) {
    logging("Cannot open pid file, reason [%s]", strerror(errno));
    close(fd);
    return;
  }
  ssize_t const written = write(fd, pbuf, plen);
  if (written != plen) {
    logging("Write error [%s]", strerror(errno));
  }
  close(fd);
}

static void remove_pid_file(char const *const pid_file) {
  logging("Removing pid file [%s]: [%d]", pid_file, getpid());
  int const rval = unlink(pid_file);
  if (rval == -1) {
    logging("Cannot remove pid file, reason [%s]", strerror(errno));
  }
}

int main(int argc, char *argv[]) {

  int sleep_timer = 0;
  char *pid_file = NULL;

  int opt;
  while ((opt = getopt(argc, argv, "hkl:p:s:-")) != -1) {
    switch (opt) {
    case 'h':
      usage();
      break;
    case 'k':
      g_kill_child_processes = 1;
      break;
    case 'l': {
      if(*optarg=='s') {
        logging_set_global_use_syslog();
      } else {
        int const logfd = atoi(optarg);
        logging_set_global_log_fd(logfd);
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

  logging("pipexec version %s", app_version);

  if (pid_file != NULL) {
    write_pid_file(pid_file);
  }

  install_signal_handler();

  unsigned int const command_cnt = command_info_clp_count(optind, argc, argv);
  unsigned int const pipe_cnt = pipe_info_clp_count(optind, argc, argv);

  logging("Number of commands in command line [%d]", command_cnt);
  logging("Number of pipes in command line [%d]", pipe_cnt);

  command_info_t icmd[command_cnt];
  command_info_array_constrcutor(icmd, optind, argc, argv);
  command_info_array_print(icmd, command_cnt);

  pipe_info_t ipipe[pipe_cnt];
  pipe_info_parse(ipipe, optind, argc, argv, '>');
  pipe_info_print(ipipe, pipe_cnt);

  // Provide memory for child_pids and initialize.
  pid_t child_pids[command_cnt];
  for (unsigned int i = 0; i < command_cnt; ++i) {
    child_pids[i] = 0;
  }
  g_child_pids = child_pids;
  g_child_cnt = command_cnt;

  do {
    if (next_running_child() == command_cnt) {
      set_restart(0);
      logging("Start all [%d] children", command_cnt);
      pipe_execv(icmd, command_cnt, ipipe, pipe_cnt, child_pids);
    }

    logging("Wait for termination of children");

    while (next_running_child() != command_cnt) {
      // Still running children
      logging("Wait for next child to terminate - ");
      child_pids_print();
      int status;
      logging("Calling wait");

      pid_t const cpid = wait(&status);

      if (cpid == -1) {
        logging("wait() error [%s]", strerror(errno));
      } else {
        logging("Child [%d] exit with status [%d] "
                "normal exit [%d] child status [%d] "
                "child signaled [%d]",
                cpid, status, WIFEXITED(status), WEXITSTATUS(status),
                WIFSIGNALED(status));
        child_pids_unset(cpid);

        if (!WIFEXITED(status) || WIFSIGNALED(status)) {
          logging("Unnormal termination/signaling of child - restarting");
          set_restart(1);
          child_pids_kill_all_and_wait();
        }
      }

      logging("Remaining children:");
      child_pids_print();

      if (g_restart && sleep_timer != 0) {
        logging("Waiting for [%d] seconds before restart", sleep_timer);
        sleep(sleep_timer);
        logging("Continue restarting");
      }
    }
  } while (g_restart);

  if (pid_file != NULL) {
    remove_pid_file(pid_file);
  }

  logging("exiting");

  return 0;
}
