#ifndef PIPEXEC_COMMAND_INFO_H
#define PIPEXEC_COMMAND_INFO_H

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

void command_info_array_constrcutor(
   command_info_t * icmd,
   int const start_argc, int const argc, char * argv[]);

void command_info_array_print(
   command_info_t const * const icmd, unsigned long const cnt);

void command_info_print(
   command_info_t const * const self);


// Helper:

// Counts the command line parameters
unsigned int command_info_clp_count(
   int const start_argc, int const argc, char * const argv[]);


#endif
