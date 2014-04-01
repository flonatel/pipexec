#include "src/pipe_info.h"
#include "src/logging.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

void pipe_info_print(
   pipe_info_t const * const ipipe, unsigned long const cnt) {
   for(unsigned int pidx = 0; pidx < cnt; ++pidx) {
      logging("{%d} Pipe [%s] [%d] > [%s] [%d]", pidx,
              ipipe[pidx].from_name, ipipe[pidx].from_fd,
              ipipe[pidx].to_name, ipipe[pidx].to_fd);
   }
}

unsigned int pipe_info_clp_count(
   int const start_argc, int const argc, char * const argv[]) {
   unsigned int cnt = 0;
   for(int i = start_argc; i<argc; ++i) {
      if(argv[i][0]=='{' && strchr(argv[i], '>')!=NULL) {
         ++cnt;
      }
   }
   return cnt;
}

void pipe_info_parse(
   pipe_info_t * const ipipe,
   int const start_argc, int const argc, char * const argv[]) {
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
void pipe_info_block_used_fds(
   pipe_info_t const * const ipipe, unsigned long const cnt) {
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

void pipe_info_create_pipes(
   pipe_info_t * const ipipe, unsigned long const pipe_cnt) {
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
void pipe_info_dup_in_pipes(
   pipe_info_t * ipipe, unsigned long pipe_cnt, char * cmd_name) {
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

void pipe_info_close_all(
   pipe_info_t const * const ipipe, unsigned long const pipe_cnt) {
   for(size_t pidx=0; pidx<pipe_cnt; ++pidx) {
      logging("{%d} Closing all fd from [%d]",
              pidx, ipipe[pidx].pipefds[1]);
      close(ipipe[pidx].pipefds[1]);
      logging("{%d} Closing all fd to [%d]",
              pidx, ipipe[pidx].pipefds[0]);
      close(ipipe[pidx].pipefds[0]);
   }
}
