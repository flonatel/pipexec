#include "src/common_pipe_info.h"
#include "src/logging.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

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
void common_pipe_info_block_used_fds(pipe_info_t const *const ipipe,
                                     unsigned long const cnt,
                                     parent_pipe_info_t const *const iparent,
                                     size_t const parent_cnt) {
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

  for (unsigned int pidx = 0; pidx < parent_cnt; ++pidx) {
    block_fd(&iparent[pidx].to, block_pipefds[0]);
  }
}
