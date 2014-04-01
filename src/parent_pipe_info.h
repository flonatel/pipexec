#ifndef PIPEXEC_PARENT_PIPE_INFO_H
#define PIPEXEC_PARENT_PIPE_INFO_H

struct parent_pipe_info {
   int parent_fd;

   char * child_name;
   int child_fd;
};

typedef struct parent_pipe_info parent_pipe_info_t;


unsigned int parent_pipe_info_clp_count(
   int const start_argc, int const argc, char * const argv[]);

#endif
