#ifndef PIPEXEC_PIPE_INFO_H
#define PIPEXEC_PIPE_INFO_H

/*
 * Information about one pipe's end:
 * The name of the process and the fd it should get.
 */
struct pipes_end_info {
   char *      name;
   int         fd;
};

typedef struct pipes_end_info pipes_end_info_t;

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

void pipe_info_parse(
   pipe_info_t * const ipipe,
   int const start_argc, int const argc, char * const argv[]);
void pipe_info_create_pipes(
   pipe_info_t * const ipipe, unsigned long const pipe_cnt);
void pipe_info_close_all(
   pipe_info_t const * const ipipe, unsigned long const pipe_cnt);
void pipe_info_dup_in_pipes(
   pipe_info_t * ipipe, unsigned long pipe_cnt, char * cmd_name);
void pipe_info_block_used_fds(
   pipe_info_t const * const ipipe, unsigned long const cnt);
void pipe_info_print(
   pipe_info_t const * const ipipe, unsigned long const cnt);

unsigned int pipe_info_clp_count(
   int const start_argc, int const argc, char * const argv[]);

#endif
