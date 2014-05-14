#ifndef PIPEXEC_PARENT_PIPE_INFO_H
#define PIPEXEC_PARENT_PIPE_INFO_H

#include "src/pipe_info.h"

#include <stddef.h>

struct parent_pipe_info {
  int parent_fd;
  pipes_end_info_t to;
  int pipefds[2];
};

typedef struct parent_pipe_info parent_pipe_info_t;

void parent_pipe_info_parse(parent_pipe_info_t *const ipipe,
                            size_t const parent_pipe_cnt, int const start_argc,
                            int const argc, char *const argv[]);
void parent_pipe_info_print(parent_pipe_info_t const *const ipipe,
                            unsigned long const cnt);
unsigned int parent_pipe_info_clp_count(int const start_argc, int const argc,
                                        char *const argv[]);
#endif
