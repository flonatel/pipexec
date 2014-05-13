#ifndef PIPEXEC_COMMON_PIPE_INFO_H
#define PIPEXEC_COMMON_PIPE_INFO_H

#include "src/pipe_info.h"
#include "src/parent_pipe_info.h"

/*
 * Functions with use pipe_info and parent_pipe_info
 */

void common_pipe_info_block_used_fds(pipe_info_t const *const ipipe,
                                     unsigned long const cnt,
                                     parent_pipe_info_t const *const iparent,
                                     size_t const parent_cnt);

#endif
