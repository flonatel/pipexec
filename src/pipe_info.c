// Copyright 2015,2022 by Andreas Florath
// SPDX-License-Identifier: GPL-2.0-or-later

#include "src/pipe_info.h"
#include "src/logging.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

char *pipes_end_info_parse(pipes_end_info_t *const pend, char *const str) {
  /*
    GCC 12 introduces a new dangling pointer check.
    > dangling pointer ‘colon’ to ‘end_fd’ may be used
    This is at this point a false positive as there is no way
    to access 'end_fd' using 'colum' after returning from
    this function.

    Limit the diagnostics ignorance to GCC >= 12 as dangling-pointer
    is not known to gcc-11 or clang 13.
   */
#if __GNUC__ >= 12
#ifndef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-pointer"
#endif
#endif

  char *const colon = strchr(str, ':');
  if (colon == NULL) {
    logging(lid_internal, "command_line", "error",
	    "Invalid syntax: no colon in pipe desc found", 0);
    exit(1);
  }
  *colon = '\0';
  pend->name = str;
  char *end_fd;
  pend->fd = strtol(colon + 1, &end_fd, 10);
  return end_fd;

#if __GNUC__ >= 12
#ifndef __clang__
#pragma GCC diagnostic pop
#endif
#endif
}

void pipe_info_print(pipe_info_t const *const ipipe, unsigned long const cnt) {
  for (unsigned int pidx = 0; pidx < cnt; ++pidx) {
    ITOCHAR(spidx, 16, pidx);
    ITOCHAR(sin_pipe_fd, 16, ipipe[pidx].from.fd);
    ITOCHAR(sout_pipe_fd, 16, ipipe[pidx].to.fd);
    logging(lid_internal, "pipe", "info", "pipe_info", 5,
	    "pipe_index", spidx, "from_pipe_name", ipipe[pidx].from.name,
	    "from_pipe_fd", sin_pipe_fd, "to_pipe_name", ipipe[pidx].to.name,
	    "to_pipe_fd", sout_pipe_fd);
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
        logging(lid_internal, "command_line", "error", "Invalid syntax: no ':' in pipe desc found", 0);
        exit(1);
      }

      char *const end_to =
          pipes_end_info_parse(&ipipe[pipe_no].to, end_from + 1);
      if (*end_to != '}') {
        logging(lid_internal, "command_line", "error", "Invalid syntax: no '}' closing pipe desc found", 0);
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
    SIZETTOCHAR(spidx, 20, pidx);
    ITOCHAR(sfrom_fd, 16, ipipe[pidx].pipefds[1]);
    ITOCHAR(sto_fd, 16, ipipe[pidx].pipefds[0]);

    logging(lid_internal, "pipe", "info", "pipe_created", 3,
	    "pipe_index", spidx, "from_fd", sfrom_fd, "to_fd", sto_fd);
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
    SIZETTOCHAR(spidx, 20, pidx);
    ITOCHAR(from_pipe_fd, 16, pipe_fd);
    ITOCHAR(to_pipe_fd, 16, pend->fd);
    logging(lid_internal, "pipe", "info", "dup", 5,
	    "pipe_index", spidx, "command", cmd_name,
	    "pipe_name", pend->name, "from_pipe_fd", from_pipe_fd,
	    "to_pipe_fd", to_pipe_fd);
    close(pend->fd);
    int const bfd = dup2(pipe_fd, pend->fd);
    if (bfd != pend->fd) {
      ITOCHAR(serrno, 16, errno);
      logging(lid_internal, "pipe", "error", "dup2", 7,
	      "pipe_index", spidx, "command", cmd_name,
	      "pipe_name", pend->name, "from_pipe_fd", from_pipe_fd,
	      "to_pipe_fd", to_pipe_fd, "errno", serrno,
	      "error", strerror(errno));
      abort();
    }
  } else {
    if (close_unused) {
      SIZETTOCHAR(spidx, 20, pidx);
      ITOCHAR(from_pipe_fd, 16, pipe_fd);
      ITOCHAR(to_pipe_fd, 16, pend->fd);
      logging(lid_internal, "pipe", "info", "closing", 5,
	      "pipe_index", spidx, "command", cmd_name,
	      "pipe_name", pend->name, "from_pipe_fd", from_pipe_fd,
	      "to_pipe_fd", to_pipe_fd);
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
    SIZETTOCHAR(spidx, 20, pidx);
    ITOCHAR(from_fd, 16, ipipe[pidx].pipefds[1]);
    logging(lid_internal, "pipe", "info", "closing fd from", 2,
	    "pipe_index", spidx, "fd", from_fd);
    close(ipipe[pidx].pipefds[1]);
    ITOCHAR(to_fd, 16, ipipe[pidx].pipefds[0]);
    logging(lid_internal, "pipe", "info", "closing fd to", 2,
	    "pipe_index", spidx, "fd", to_fd);
    close(ipipe[pidx].pipefds[0]);
  }
}

static void block_fd(pipes_end_info_t const *const pend, int blocking_fd) {
  if (pend->fd > 2 && pend->fd != blocking_fd) {
    ITOCHAR(pipe_fd, 16, pend->fd);
    ITOCHAR(sbfd, 16, blocking_fd);
    logging(lid_internal, "pipe", "info", "blocking_fd", 2,
	    "pipe_fd", pipe_fd, "blocking_fd", sbfd);
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
  logging(lid_internal, "pipe", "info", "Blocking used fds", 0);

  logging(lid_internal, "pipe", "info", "Creating extra pipe for blocking fds", 0);
  int block_pipefds[2];
  int const pres = pipe(block_pipefds);
  if (pres == -1) {
    perror("pipe");
    exit(10);
  }
  // One is enough:
  close(block_pipefds[1]);
  ITOCHAR(sbfd, 16, block_pipefds[0]);
  logging(lid_internal, "pipe", "info", "fd for blocking", 1, "fd", sbfd);

  for (unsigned int pidx = 0; pidx < cnt; ++pidx) {
    block_fd(&ipipe[pidx].from, block_pipefds[0]);
    block_fd(&ipipe[pidx].to, block_pipefds[0]);
  }
}

#define PI_CHECK_FOR_DUPS(iPiPe, cNt, fOrT)				\
  do {									\
    for(unsigned long ocmp = 0; ocmp < cNt - 1; ++ocmp) {		\
      for(unsigned long tcmp = ocmp + 1; tcmp < cNt; ++tcmp) {		\
        if(strcmp(iPiPe[ocmp].fOrT.name, iPiPe[tcmp].fOrT.name) == 0 && \
	   (iPiPe[ocmp].fOrT.fd == iPiPe[tcmp].fOrT.fd)) {		\
           fprintf(stderr, "ERROR: Duplicate pipe in command line: [%s] [%s] [%d]\n", \
		   #fOrT, iPiPe[ocmp].fOrT.name, iPiPe[ocmp].fOrT.fd);  \
	   exit(1);                                                     \
        }								\
      }									\
    }									\
  } while(0)

// Check for any duplicates in the from or the to pipes
void pipe_info_check_for_duplicates(pipe_info_t const *const ipipe,
				    unsigned long const cnt) {
  // Handle simple case where there is no or only one pipe info
  if(cnt <= 1) {
    return;
  }

  PI_CHECK_FOR_DUPS(ipipe, cnt, from);
  PI_CHECK_FOR_DUPS(ipipe, cnt, to);
}
