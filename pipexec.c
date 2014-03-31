/*
 * pipexec
 *
 * Build up a directed graph of processes and pipes.
 *
 */
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

int const app_version = 2;
int const app_subversion = 0;

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

void child_pids_wait_all() {
   for(unsigned int child_idx=0; child_idx<g_child_cnt; ++child_idx) {
      if(g_child_pids[child_idx]!=0) {
         pid_t const to_wait = g_child_pids[child_idx];
         logging("Wait for pid [%d] to terminate", to_wait);
         int status;
         pid_t const rw = waitpid(to_wait, &status, 0);
         if(rw==-1) {
            logging("Error waiting for [%d] [%s]",
                    to_wait, strerror(errno));
         } else {
            logging("Child [%d] exit with status [%d] "
                    "normal exit [%d] child status [%d] "
                    "child signaled [%d]",
                    to_wait, status, WIFEXITED(status),
                    WEXITSTATUS(status), WIFSIGNALED(status));
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

void sh_term(int signum, siginfo_t * siginfo,
             void * ucontext) {
   (void)siginfo; (void)ucontext;

   logging("signal terminate handler called - signal received [%d]", signum);

   // Kill all children and stop
   set_terminate();
   child_pids_kill_all_and_wait();
}

void sh_restart(int signum, siginfo_t * siginfo, void * ucontext) {
   (void)siginfo; (void)ucontext;

   logging("signal restart handler called - signal received [%d]", signum);

   // Kill all children and restart
   set_restart(1);
   child_pids_kill_all_and_wait();
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
struct command_info {
   char * cmd_name;
   char * path;
   char ** argv;
};

typedef struct command_info command_info_t;

static unsigned int command_info_clp_count(
   int start_argc, int argc, char * argv[]) {
   unsigned int cnt = 0;
   for(int i = start_argc; i<argc; ++i) {
      if(argv[i][0]=='[') {
         ++cnt;
      }
   }
   return cnt;
}

/**
 * Placement constructor:
 * pass in a unititialized memory region.
 */
static void command_info_array_constrcutor(
   command_info_t * icmd,
   int start_argc, int argc, char * argv[]) {
   unsigned int cmd_no = 0;
   for(int i = start_argc; i<argc; ++i) {
      if(argv[i][0]=='[') {
         icmd[cmd_no].cmd_name = &argv[i][1];
         icmd[cmd_no].path = argv[i+1];
         icmd[cmd_no].argv = &argv[i+1];
         ++cmd_no;
      } else if(argv[i][0]==']') {
         argv[i] = NULL;
      }
   }
}

void command_info_array_print(
   command_info_t * icmd,
   unsigned long cnt) {
   for(unsigned int cidx = 0; cidx < cnt; ++cidx) {
      logging("Command [%2d] [%s] [%s]",
              cidx, icmd[cidx].cmd_name, icmd[cidx].path);
   }
}

static void command_info_constrcutor(
   command_info_t * self,
   char ** argv) {
   self->path = *argv;
   self->argv = argv;
}

static void command_info_print(
   command_info_t const * const self) {
   logging("command_info path [%s]", self->argv[0]);
}

/*
 * Pipe information
 *
 * This contains the source (name and fd) and the destination
 * (also name and fd).
 */
struct pipe_info {
   char *      from_name;
   int         from_fd;

   char *      to_name;
   int         to_fd;

   int         pipefds[2];
};

typedef struct pipe_info pipe_info_t;

static void pipe_info_print(pipe_info_t * ipipe, unsigned long cnt) {
   for(unsigned int pidx = 0; pidx < cnt; ++pidx) {
      logging("{%d} Pipe [%s] [%d] > [%s] [%d]", pidx,
              ipipe[pidx].from_name, ipipe[pidx].from_fd,
              ipipe[pidx].to_name, ipipe[pidx].to_fd);
   }
}

static unsigned int pipe_info_clp_count(
   int start_argc, int argc, char * argv[]) {
   unsigned int cnt = 0;
   for(int i = start_argc; i<argc; ++i) {
      if(argv[i][0]=='{' && strchr(argv[i], '>')!=NULL) {
         ++cnt;
      }
   }
   return cnt;
}


static void pipe_info_parse(pipe_info_t * ipipe,
                     int start_argc, int argc, char * argv[]) {
   unsigned int pipe_no = 0;
   for(int i = start_argc; i<argc; ++i) {
      if(argv[i]==NULL) {
         continue;
      }

      if(argv[i][0]=='{' && strchr(argv[i], '>')!=NULL) {
         char * colon = strchr(&argv[i][1], ':');
         if(colon==NULL) {
            logging("Invalid syntax: no colon in pipe desc found");
            exit(1);
         }
         *colon = '\0';
         ipipe[pipe_no].from_name = &argv[i][1];
         char * end_fd;
         ipipe[pipe_no].from_fd = strtol(colon+1, &end_fd, 10);

         // connect symbol
         if(*end_fd!='>') {
            logging("Invalid syntax: no '>' in pipe desc found");
            exit(1);
         }

         // ToDo: Copy from above
         char * colon2 = strchr(end_fd, ':');
         if(colon2==NULL) {
            logging("Invalid syntax: no colon in pipe desc 2 found");
            exit(1);
         }
         *colon2 = '\0';
         ipipe[pipe_no].to_name = end_fd+1;
         ipipe[pipe_no].to_fd = strtol(colon2+1, &end_fd, 10);

         ++pipe_no;
      }
   }
}

// Dup2 a fd
static void block_fd(int to_block, int blocking_fd) {
   logging("Blocking fd [%d] with copy of [%d]", to_block, blocking_fd);
   int const bfd=dup2(blocking_fd, to_block);
   if(bfd!=to_block) {
      abort();
   }
}

// Block all FDs for really used pipes:
// o open an additional unused pipe
// o dup2() one fd of this unused pipe for all later on used fds.
static void pipe_info_block_used_fds(pipe_info_t * ipipe, unsigned long cnt) {
   logging("Blocking used fds");

   logging("Creating extra pipe for blocking fds");
   int block_pipefds[2];
   int const pres = pipe(block_pipefds);
   if(pres==-1) {
      perror("pipe");
      exit(10);
   }
   // One is enough:
   close(block_pipefds[1]);
   logging("Fd for blocking [%d]", block_pipefds[0]);

   for(unsigned int pidx = 0; pidx < cnt; ++pidx) {
      if(ipipe[pidx].from_fd>2
         && ipipe[pidx].from_fd!=block_pipefds[0]) {
         block_fd(ipipe[pidx].from_fd, block_pipefds[0]);
      }
      if(ipipe[pidx].to_fd>2
         && ipipe[pidx].to_fd!=block_pipefds[0]) {
         block_fd(ipipe[pidx].to_fd, block_pipefds[0]);
      }
   }
}

static void pipe_info_create_pipes(
   pipe_info_t * ipipe, unsigned long pipe_cnt) {
   // Open up all the pipes.
   for(size_t pidx=0; pidx<pipe_cnt; ++pidx) {
      int const pres = pipe(ipipe[pidx].pipefds);
      if(pres==-1) {
         perror("pipe");
         exit(10);
      }
      logging("{%d} Pipe created [%d] -> [%d]", pidx, ipipe[pidx].pipefds[1],
              ipipe[pidx].pipefds[0]);
   }
}

// ToDo: check result of close.
static void pipe_info_dup_in_pipes(char * cmd_name,
   pipe_info_t * ipipe, unsigned long pipe_cnt) {
   for(size_t pidx=0; pidx<pipe_cnt; ++pidx) {
      if(strcmp(cmd_name, ipipe[pidx].from_name)==0) {
         logging("{%d} [%s] Dup fd [%s] [%d] -> [%d]", pidx, cmd_name,
                 ipipe[pidx].from_name, ipipe[pidx].pipefds[1],
                 ipipe[pidx].from_fd);
         close(ipipe[pidx].from_fd);
         int const bfd=dup2(ipipe[pidx].pipefds[1], ipipe[pidx].from_fd);
         if(bfd!=ipipe[pidx].from_fd) {
            logging("{%d} ERROR: dup2() [%d] -> [%d] failed [%s]", pidx,
                    ipipe[pidx].pipefds[1], ipipe[pidx].from_fd,
                    strerror(errno));
            abort();
         }
      } else {
         logging("{%d} [%s] Closing [%s] [%d]", pidx, cmd_name,
                 ipipe[pidx].from_name, ipipe[pidx].pipefds[1]);
         close(ipipe[pidx].pipefds[1]);
      }

      // ToDo: copy of the above
      if(strcmp(cmd_name, ipipe[pidx].to_name)==0) {
         logging("{%d} [%s] Dup fd [%s] [%d] -> [%d]", pidx, cmd_name,
                 ipipe[pidx].to_name, ipipe[pidx].pipefds[0],
                 ipipe[pidx].to_fd);
         close(ipipe[pidx].to_fd);
         int const bfd=dup2(ipipe[pidx].pipefds[0], ipipe[pidx].to_fd);
         if(bfd!=ipipe[pidx].to_fd) {
            logging("{%d} ERROR: dup2() [%d] -> [%d] failed [%s]", pidx,
                    ipipe[pidx].pipefds[0], ipipe[pidx].to_fd,
                    strerror(errno));
            abort();
         }
      } else {
         logging("{%d} [%s] Closing [%s] [%d]", pidx, cmd_name,
                 ipipe[pidx].to_name, ipipe[pidx].pipefds[0]);
         close(ipipe[pidx].pipefds[0]);
      }

   }
}

static void pipe_info_close_all(pipe_info_t * ipipe, unsigned long pipe_cnt) {
   for(size_t pidx=0; pidx<pipe_cnt; ++pidx) {
      logging("{%d} Closing all fd from [%d]",
              pidx, ipipe[pidx].pipefds[1]);
      close(ipipe[pidx].pipefds[1]);
      logging("{%d} Closing all fd to [%d]",
              pidx, ipipe[pidx].pipefds[0]);
      close(ipipe[pidx].pipefds[0]);
   }
}

struct parent_pipe_info {
   int parent_fd;

   char * child_name;
   int child_fd;
};

typedef struct parent_pipe_info parent_pipe_info_t;

static unsigned int parent_pipe_info_clp_count(
   int start_argc, int argc, char * argv[]) {
   unsigned int cnt = 0;
   for(int i = start_argc; i<argc; ++i) {
      if(argv[i][0]=='{' && strchr(argv[i], '=')!=NULL) {
         ++cnt;
      }
   }
   return cnt;
}


// Functions using the upper data structures
static void pipe_execv_one(
   command_info_t const * params,
   pipe_info_t * const ipipe, size_t const pipe_cnt) {

   pipe_info_dup_in_pipes(params->cmd_name, ipipe, pipe_cnt);

   logging("[%s] Calling execv [%s]", params->cmd_name, params->path);
   execv(params->path, params->argv);
   perror("execv");
   abort();
}

static pid_t pipe_execv_fork_one(
   command_info_t const * params,
   pipe_info_t * const ipipe, size_t const pipe_cnt) {

   command_info_print(params);
   pid_t const fpid = fork();

   if(fpid==-1) {
      logging("Error during fork() [%s]", strerror(errno));
      exit(10);
   } else if(fpid==0) {
      uninstall_signal_handler();
      pipe_execv_one(params, ipipe, pipe_cnt);
      // Neverreached
      abort();
   }

   logging("[%s] New child pid [%d]", params->cmd_name, fpid);
   // fpid>0: parent
   return fpid;
}

int pipe_execv(command_info_t * const icmd, size_t const command_cnt,
               pipe_info_t * const ipipe, size_t const pipe_cnt,
               pid_t * child_pids) {

   pipe_info_block_used_fds(ipipe, pipe_cnt);
   pipe_info_create_pipes(ipipe, pipe_cnt);

   // TODO: dup2 of '=' IN and OUTs

   // Looks that messing around with the pipes (storing them and propagating
   // them to all children) is not a good idea.
   // ... but in this case there is no other way....
   for(size_t cidx=0; cidx<command_cnt; ++cidx) {
      child_pids[cidx]
         = pipe_execv_fork_one(
            &icmd[cidx], ipipe, pipe_cnt);
   }

   pipe_info_close_all(ipipe, pipe_cnt);

   return 0;
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

static unsigned int clp_count_enteties(
   char entc, int start_argc, int argc, char * argv[]) {
   unsigned int cnt = 0;
   for(int i = start_argc; i<argc; ++i) {
      if(argv[i][0]==entc) {
         ++cnt;
      }
   }
   return cnt;
}

static unsigned int clp_count_commands(
   int start_argc, int argc, char * argv[]) {
   return clp_count_enteties('[', start_argc, argc, argv);
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

   unsigned int const command_cnt
      = command_info_clp_count(optind, argc, argv);
   unsigned int const pipe_cnt
      = pipe_info_clp_count(optind, argc, argv);
   unsigned int const parent_pipe_cnt
      = parent_pipe_info_clp_count(optind, argc, argv);

   logging("Number of commands in command line [%d]", command_cnt);
   logging("Number of pipes in command line [%d]", pipe_cnt);
   logging("Number of parent pipes in command line [%d]", parent_pipe_cnt);

   command_info_t icmd[command_cnt];
   command_info_array_constrcutor(icmd, optind, argc, argv);
   command_info_array_print(icmd, command_cnt);

   struct pipe_info    ipipe[pipe_cnt];
   pipe_info_parse(ipipe, optind, argc, argv);
   pipe_info_print(ipipe, pipe_cnt);

   // Provide memory for child_pids and initialize.
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
         pipe_execv(icmd, command_cnt,
                    ipipe, pipe_cnt, child_pids);
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

            if(! WIFEXITED(status) || WIFSIGNALED(status)) {
               logging("Unnormal termination/signaling of child - restarting");
               set_restart(1);
               child_pids_kill_all_and_wait();
            }
         }

         logging("Remaining children:");
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
