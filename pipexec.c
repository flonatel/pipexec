#define _POSIX_C_SOURCE 200809L

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>

int const app_version = 1;
int const app_subversion = 5;

char const desc_copyight[] = { "(c) 2014 by flonatel GmbH & Co, KG" };
char const desc_license[] = {"License GPLv2+: GNU GPL version 2 or later "
                             "<http://gnu.org/licenses/gpl.html>." };

/**
 * Logging is done by means of an additional file descriptor
 * which can be passed in by command line parameter.
 */
int g_log_fd = -1;

void log_fd_set(int fd) {
   g_log_fd = fd;
}

unsigned long log_fd_write_time(char * buf, unsigned long free_bytes) {
   time_t t = time(NULL);
   // No need to use the thread-safe version: we are in the one-threaded
   // universe here.
   struct tm *tmp = localtime(&t);
   return strftime(buf, free_bytes, "%F %T", tmp);
}

unsigned long log_fd_write_pname_and_pid(char * buf, unsigned long free_bytes) {
   return snprintf(buf, free_bytes, ";pipexec;%d;", getpid());
}

unsigned long log_fd_write_args(char * buf, unsigned long free_bytes,
                                char const * fmt, va_list ap) {
   return vsnprintf(buf, free_bytes, fmt, ap);
}

unsigned long log_fd_write_newline(char * buf, unsigned long free_bytes) {
   return snprintf(buf, free_bytes, "\n");
}

/**
 * Log the state, events and actions.
 * The format of a logging line contains the date and time,
 * the pid of this process and the passed in parameters.
 */
void logging(char const * fmt, ...) {
   if(g_log_fd==-1) {
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

   write(g_log_fd, pbuf, cbuf-pbuf);
}


/**
 * Globals
 * Used for communication between signal handler and main program.
 */
volatile int g_restart = 0;
volatile int g_terminate = 0;

/**
 * Should the processes restart - pass in a 1.
 * If the process in the termination phase (e.g. it received itself
 * a signal) - this has no effect.
 */
void set_restart(int rs) {
   if(g_terminate) {
      logging("Cannot set restart - process will terminate");
      return;
   }
   g_restart = rs;
}

/**
 * If an appropriate signal was sent to this process,
 * it will terminate - indeptendent of the results the clients return
 * during their shutdown.
 */
void set_terminate() {
   g_terminate = 1;
   g_restart = 0;
}


/**
 * An array with the pids of all client processes.
 * If a child is not running (e.g. during cleanup or restart phase)
 * the appropriate entry it set to 0.
 */
volatile unsigned int g_child_cnt = 0;
volatile pid_t * g_child_pids = NULL;

/**
 * Unset the given pid.
 */
void child_pids_unset(pid_t cpid) {
   for(unsigned int child_idx=0; child_idx<g_child_cnt; ++child_idx) {
      if(g_child_pids[child_idx]==cpid) {
         g_child_pids[child_idx]=0;
         return;
      }
   }
   logging("child_pids_unset: PID not found in list [%d]", cpid);
}

void child_pids_print() {
   int const pilen=1024;
   char pbuf[pilen];
   int plen = pilen;
   int poffset = snprintf(pbuf, pilen, "Child pids: ");
   plen = pilen - poffset;
   for(unsigned int child_idx=0; child_idx<g_child_cnt; ++child_idx) {
      if(g_child_pids[child_idx]==0) {
         continue;
      }
      poffset += snprintf(pbuf + poffset, plen, "[%d] ",
                          g_child_pids[child_idx]);
      plen = pilen - poffset;
   }
   logging(pbuf);
}

void child_pids_kill_all() {
   for(unsigned int child_idx=0; child_idx<g_child_cnt; ++child_idx) {
      if(g_child_pids[child_idx]!=0) {
         pid_t const to_kill = g_child_pids[child_idx];
         logging("Sending SIGTERM to [%d]", to_kill);
         kill(to_kill, SIGTERM);
      }
   }
}

/**
 * Signal Related.
 */

void sh_term(int signum, siginfo_t * siginfo,
             void * ucontext) {
   (void)siginfo; (void)ucontext;

   logging("signal terminate handler called - signal received [%d]", signum);

   // Kill all children and stop
   set_terminate();
   child_pids_kill_all();
}

void sh_restart(int signum, siginfo_t * siginfo, void * ucontext) {
   (void)siginfo; (void)ucontext;

   logging("signal restart handler called - signal received [%d]", signum);

   // Kill all children and restart
   set_restart(1);
   child_pids_kill_all();
}

void install_signal_handler() {

   struct sigaction sa_term;
   sa_term.sa_sigaction = sh_term;
   sigemptyset( &sa_term.sa_mask );
   sa_term.sa_flags = SA_SIGINFO | SA_NODEFER;

   struct sigaction sa_restart;
   sa_restart.sa_sigaction = sh_restart;
   sigemptyset( &sa_restart.sa_mask );
   sa_restart.sa_flags = SA_SIGINFO | SA_NODEFER;

   sigaction(SIGHUP, &sa_restart, NULL);
   sigaction(SIGINT, &sa_term, NULL);
   sigaction(SIGQUIT, &sa_term, NULL);
   sigaction(SIGTERM, &sa_term, NULL);
}

void uninstall_signal_handler() {

   struct sigaction sa_default;
   sa_default.sa_handler = SIG_DFL;
   sigemptyset( &sa_default.sa_mask );
   sa_default.sa_flags = SA_SIGINFO | SA_NODEFER;

   sigaction(SIGHUP, &sa_default, NULL);
   sigaction(SIGINT, &sa_default, NULL);
   sigaction(SIGQUIT, &sa_default, NULL);
   sigaction(SIGTERM, &sa_default, NULL);
}

/**
 * Path and parameters for exec one program.
 * Please note that here are only stored pointers -
 * typically to a memory of argv.
 */
struct pipe_execv_params {
   char * path;
   char ** argv;
};

typedef struct pipe_execv_params pipe_execv_params_t;

/**
 * Placement constructor:
 * pass in a unititialized memory region.
 */
void pipe_execv_params_constrcutor(
   pipe_execv_params_t * self,
   char ** argv) {
   self->path = *argv;
   self->argv = argv;
}

void pipe_execv_params_print(
   pipe_execv_params_t const * const self) {
   logging("pipe_execv_params path [%s]", self->argv[0]);
}

static void pipe_execv_one(
   pipe_execv_params_t const * params,
   int child_stdin_fd, int child_stdout_fd, int next_child_stdin_fd) {

   logging("pipe_execv_one child_fds [%d] [%d] [%d]",
           child_stdin_fd, child_stdout_fd, next_child_stdin_fd);

   if(child_stdin_fd!=-1) {
      close(STDIN_FILENO);
      int const nstdin=dup2(child_stdin_fd, STDIN_FILENO);
      if(nstdin!=STDIN_FILENO) {
         abort();
      }
   }

   if(child_stdout_fd!=-1) {
      close(STDOUT_FILENO);
      int const nstdout=dup2(child_stdout_fd, STDOUT_FILENO);
      if(nstdout!=STDOUT_FILENO) {
         abort();
      }
   }

   if(next_child_stdin_fd!=-1) close(next_child_stdin_fd);

   execv(params->path, params->argv);

   perror("execv");
   abort();
}

static pid_t pipe_execv_fork_one(
   pipe_execv_params_t const * params,
   int child_stdin_fd, int child_stdout_fd, int next_child_stdin_fd) {

   pipe_execv_params_print(params);
   pid_t const fpid = fork();

   if(fpid==-1) {
      logging("Error during fork() [%s]", strerror(errno));
      exit(10);
   } else if(fpid==0) {
      uninstall_signal_handler();
      pipe_execv_one(params, child_stdin_fd, child_stdout_fd,
                     next_child_stdin_fd);
      // Neverreached
      abort();
   }

   logging("New child pid [%d]", fpid);
   // fpid>0: parent
   return fpid;
}

int pipe_execv(size_t const param_cnt, pipe_execv_params_t * const params,
               pid_t * child_pids) {

   // Looks that messing around with the pipes (storing them and propagating
   // them to all children) is not a good idea.
   // There is a need for three pipe fds, because pipe fds can only be
   // created in pairs.
   // o stdin of the child
   // o the new pipe-pair for stdout and the next processes stdin.
   int child_stdin_fd = -1;
   int child_stdout_fd = -1;
   int next_child_stdin_fd = -1;

   for(size_t param_idx=0; param_idx<param_cnt; ++param_idx) {

      int const is_cmd_following  = (param_idx<param_cnt-1);

      if(is_cmd_following) {
         int npipefds[2];
         int const pres = pipe(npipefds);
         if(pres==-1) {
            perror("pipe");
            exit(10);
         }

         child_stdout_fd = npipefds[1];
         next_child_stdin_fd = npipefds[0];
      }

      child_pids[param_idx] =
         pipe_execv_fork_one(
            &params[param_idx],
            child_stdin_fd, child_stdout_fd, next_child_stdin_fd);

      // Advance the pipe fds
      if(child_stdin_fd!=-1) { close(child_stdin_fd); child_stdin_fd = -1; }
      if(child_stdout_fd!=-1) { close(child_stdout_fd); child_stdout_fd = -1; }
      child_stdin_fd = next_child_stdin_fd;
      next_child_stdin_fd = -1;
   }

   return 0;
}

/**
 * Replaces the pipe symbol with NULL.
 * returns the number of replaces pipe symbols.
 */
static unsigned int replace_pipe_symbol_with_null(
   int start_argc, int argc, char * argv[]) {
   logging("Parsing command");

   unsigned int pipe_symbol_cnt = 0;
   for(int i = start_argc; i<argc; ++i) {
      logging("Arg pos [%3d]: [%s]", i, argv[i]);

      if(strlen(argv[i])==1 && argv[i][0]=='|') {
         argv[i]=NULL;
         ++pipe_symbol_cnt;
         logging("Pipe symbol found at pos [%d]", i);
      }
   }
   logging("Found [%d] pipe symbols", pipe_symbol_cnt);
   return pipe_symbol_cnt;
}

/**
 * Find next command.
 * Precondition: pipe symbol in argv must be already replaced by NULL.
 */
static int find_next_cmd(int argc, char ** argv, int arg_idx) {
   for(; arg_idx<argc; ++arg_idx) {
      if(argv[arg_idx]==NULL) {
         return arg_idx+1;
      }
   }
   return argc;
}

unsigned int next_running_child() {
   for(unsigned int child_idx=0; child_idx<g_child_cnt; ++child_idx) {
      if( g_child_pids[child_idx]!=0 ) {
         return child_idx;
      }
   }
   return g_child_cnt;
}


static void usage() {
   fprintf(stderr, "pipexec version %d.%d\n", app_version, app_subversion);
   fprintf(stderr, "%s\n", desc_copyight);
   fprintf(stderr, "%s\n", desc_license);
   fprintf(stderr, "\n");
   fprintf(stderr, "Usage: pipexec [options] -- command-pipe\n");
   fprintf(stderr, "Options:\n");
   fprintf(stderr, " -h              display this help\n");
   fprintf(stderr, " -l logfd        set fd which is used for logging\n");
   fprintf(stderr, " -n name         set the name of the process\n");
   fprintf(stderr, " -p pidfile      specify a pidfile\n");
   fprintf(stderr, " -s sleep_time   time to wait before a restart\n");
   exit(1);
}

static void write_pid_file(char const * const pid_file) {
   logging("Writing pid file [%s]: [%d]", pid_file, getpid());
   char pbuf[20];
   int const plen = snprintf(pbuf, 20, "%d\n", getpid());
   int const fd = open(pid_file, O_WRONLY | O_CREAT | O_TRUNC,
                       S_IRUSR | S_IRGRP | S_IROTH);
   if(fd==-1) {
      logging("Cannot open pid file, reason [%s]", strerror(errno));
      close(fd);
      return;
   }
   ssize_t const written = write(fd, pbuf, plen);
   if(written!=plen) {
      logging("Write error [%s]", strerror(errno));
   }
   close(fd);
}

static void remove_pid_file(char const * const pid_file) {
   logging("Removing pid file [%s]: [%d]", pid_file, getpid());
   int const rval = unlink(pid_file);
   if(rval==-1) {
      logging("Cannot remove pid file, reason [%s]", strerror(errno));
   }
}

int main(int argc, char * argv[]) {

   int logfd = -1;
   int sleep_timer = 0;
   char * pid_file = NULL;
   char * proc_name = NULL;

   int opt;
   while ((opt = getopt(argc, argv, "hl:n:p:s:-")) != -1) {
      switch (opt) {
      case 'h':
         usage();
      case 'l':
         logfd = atoi(optarg);
         log_fd_set(logfd);
         break;
      case 'n':
         proc_name = optarg;
         break;
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

   if(optind==argc) {
      fprintf(stderr, "Error: No command-pipe given\n");
      usage();
   }

   logging("pipexec version %d.%d", app_version, app_subversion);
   logging(desc_copyight);
   logging(desc_license);

   if(pid_file!=NULL) {
      write_pid_file(pid_file);
   }

   if(proc_name!=NULL) {
      logging("Setting process name to [%s]", proc_name);
      int const rp = prctl(PR_SET_NAME, (unsigned long) proc_name, 0, 0, 0);
      if(rp==-1) {
         logging("Setting of process name failed [%s]", strerror(errno));
      }
   }

   install_signal_handler();

   unsigned int const pipe_symbol_cnt
      = replace_pipe_symbol_with_null(optind, argc, argv);
   unsigned int const command_cnt = pipe_symbol_cnt + 1;

   pipe_execv_params_t params[command_cnt];
   unsigned int params_idx = 0;
   int arg_idx = optind;

   do {
      pipe_execv_params_constrcutor(
         &params[params_idx++], &argv[arg_idx]);
   } while ( (arg_idx = find_next_cmd(argc, argv, arg_idx)) != argc );

   pid_t child_pids[command_cnt];
   for(unsigned int i=0; i<command_cnt; ++i) {
      child_pids[i]=0;
   }
   g_child_pids = child_pids;
   g_child_cnt = command_cnt;

   do {
      if( next_running_child()==command_cnt ) {
         set_restart(0);
         logging("Start all [%d] children", command_cnt);
         pipe_execv(command_cnt, params, child_pids);
      }

      logging("Wait for termination of children");

      while( next_running_child()!=command_cnt ) {
         // Still running children
         logging("Wait for next child to terminate - ");
         child_pids_print();
         int status;
         logging("Calling wait");

         pid_t const cpid = wait(&status);

         if(cpid==-1) {
            logging("wait() error [%s]", strerror(errno));
         } else {
            logging("Child [%d] exit with status [%d] "
                    "normal exit [%d] child status [%d] "
                    "child signaled [%d]",
                    cpid, status, WIFEXITED(status),
                    WEXITSTATUS(status), WIFSIGNALED(status));
            child_pids_unset(cpid);

            if(! WIFEXITED(status)) {
               logging("Unnormal termination of child - restarting");
               set_restart(1);
               child_pids_kill_all();
            }
         }

         logging("Finished waiting for clients");
         child_pids_print();

         if( g_restart && sleep_timer!=0 ) {
            logging("Waiting for [%d] seconds before restart", sleep_timer);
            sleep(sleep_timer);
            logging("Continue restarting");
         }
      }
   } while( g_restart );

   if(pid_file!=NULL) {
      remove_pid_file(pid_file);
   }

   logging("exiting");

   return 0;
}
