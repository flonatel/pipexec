#include "src/pipe_info.h"
#include "src/logging.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

char *pipes_end_info_parse(pipes_end_info_t *const pend, char *const str) {

  char *const colon = strchr(str, ':');
  if (colon == NULL) {
    logging("Invalid syntax: no colon in pipe desc found");
    exit(1);
  }
  *colon = '\0';
  pend->name = str;
  char *end_fd;
  pend->fd = strtol(colon + 1, &end_fd, 10);
  return end_fd;
}

void pipe_info_print(pipe_info_t const *const ipipe, unsigned long const cnt) {
  for (unsigned int pidx = 0; pidx < cnt; ++pidx) {
    logging("{%d} Pipe [%s] [%d] > [%s] [%d]", pidx, ipipe[pidx].from.name,
            ipipe[pidx].from.fd, ipipe[pidx].to.name, ipipe[pidx].to.fd);
  }
}

unsigned int pipe_info_clp_count(int const start_argc, int const argc,
                                 char *const argv[]) {
  unsigned int cnt = 0;
  for (int i = start_argc; i < argc; ++i) {
    if (argv[i][0] == '{' && strchr(argv[i], '>') != NULL) {
      ++cnt;
    }
  }
  return cnt;
}

void pipe_info_parse(pipe_info_t *const ipipe, int const start_argc,
                     int const argc, char *const argv[], char const sep) {
  unsigned int pipe_no = 0;
  for (int i = start_argc; i < argc; ++i) {
    if (argv[i] == NULL) {
      continue;
    }

    if (argv[i][0] == '{' && strchr(argv[i], sep) != NULL) {
      char *const end_from =
          pipes_end_info_parse(&ipipe[pipe_no].from, &argv[i][1]);
      if (*end_from != sep) {
        logging("Invalid syntax: no '%c' in pipe desc found", sep);
        exit(1);
      }

      char *const end_to =
          pipes_end_info_parse(&ipipe[pipe_no].to, end_from + 1);
      if (*end_to != '}') {
        logging("Invalid syntax: no '}' closing pipe desc found");
        exit(1);
      }
      ++pipe_no;
    }
  }
}

void pipe_info_create_pipes(pipe_info_t *const ipipe,
                            unsigned long const pipe_cnt) {
  // Open up all the pipes.
  for (size_t pidx = 0; pidx < pipe_cnt; ++pidx) {
    int const pres = pipe(ipipe[pidx].pipefds);
    if (pres == -1) {
      perror("pipe");
      exit(10);
    }
    logging("{%d} Pipe created [%d] -> [%d]", pidx, ipipe[pidx].pipefds[1],
            ipipe[pidx].pipefds[0]);
  }
}

// This is similar to the parent_pipe_info_dup_in_piped_for_pipe_end
// function - but has some differences in the data.
// Might be hard to refactor (unify).
static void
pipe_info_dup_in_piped_for_pipe_end(size_t const pidx, char *cmd_name,
                                    pipes_end_info_t const *const pend,
                                    int pipe_fd, int close_unused) {
  if (strcmp(cmd_name, pend->name) == 0) {
    logging("{%d} [%s] Dup fd [%s] [%d] -> [%d]", pidx, cmd_name, pend->name,
            pipe_fd, pend->fd);
    close(pend->fd);
    int const bfd = dup2(pipe_fd, pend->fd);
    if (bfd != pend->fd) {
      logging("{%d} ERROR: dup2() [%d] -> [%d] failed [%s]", pidx, pipe_fd,
              pend->fd, strerror(errno));
      abort();
    }
  } else {
    if (close_unused) {
      logging("{%d} [%s] Closing [%s] [%d]", pidx, cmd_name, pend->name,
              pipe_fd);
      close(pipe_fd);
    }
  }
}

void pipe_info_dup_in_pipes(pipe_info_t *ipipe, unsigned long pipe_cnt,
                            char *cmd_name, int close_unused) {
  for (size_t pidx = 0; pidx < pipe_cnt; ++pidx) {
    pipe_info_dup_in_piped_for_pipe_end(pidx, cmd_name, &ipipe[pidx].from,
                                        ipipe[pidx].pipefds[1], close_unused);
    pipe_info_dup_in_piped_for_pipe_end(pidx, cmd_name, &ipipe[pidx].to,
                                        ipipe[pidx].pipefds[0], close_unused);
  }
}

void pipe_info_close_all(pipe_info_t const *const ipipe,
                         unsigned long const pipe_cnt) {
  for (size_t pidx = 0; pidx < pipe_cnt; ++pidx) {
    logging("{%d} Closing all fd from [%d]", pidx, ipipe[pidx].pipefds[1]);
    close(ipipe[pidx].pipefds[1]);
    logging("{%d} Closing all fd to [%d]", pidx, ipipe[pidx].pipefds[0]);
    close(ipipe[pidx].pipefds[0]);
  }
}

static void block_fd(pipes_end_info_t const *const pend, int blocking_fd) {
  if (pend->fd > 2 && pend->fd != blocking_fd) {
    logging("Blocking fd [%d] with copy of [%d]", pend->fd, blocking_fd);
    int const bfd = dup2(blocking_fd, pend->fd);
    if (bfd != pend->fd) {
      abort();
    }
  }
}

// Block all FDs for really used pipes:
// o open an additional unused pipe
// o dup2() one fd of this unused pipe for all later on used fds.
void pipe_info_block_used_fds(pipe_info_t const *const ipipe,
                              unsigned long const cnt) {
  logging("Blocking used fds");

  logging("Creating extra pipe for blocking fds");
  int block_pipefds[2];
  int const pres = pipe(block_pipefds);
  if (pres == -1) {
    perror("pipe");
    exit(10);
  }
  // One is enough:
  close(block_pipefds[1]);
  logging("Fd for blocking [%d]", block_pipefds[0]);

  for (unsigned int pidx = 0; pidx < cnt; ++pidx) {
    block_fd(&ipipe[pidx].from, block_pipefds[0]);
    block_fd(&ipipe[pidx].to, block_pipefds[0]);
  }
}
