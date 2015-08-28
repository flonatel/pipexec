/*
 * ptee
 *
 * Tee for pipes / fds.
 * This is like the 'normal' tee - except that it does not output to
 * different files but to differend fds.
 *
 */

#define _POSIX_C_SOURCE 200809L

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include "src/version.h"

static void usage() {
   fprintf(stderr, "ptee from pipexec version %s\n", app_version);
   fprintf(stderr, "%s\n", desc_copyight);
   fprintf(stderr, "%s\n", desc_license);
   fprintf(stderr, "\n");
   fprintf(stderr, "Usage: ptee [options] fd [fd ...]\n");
   fprintf(stderr, "Options:\n");
   fprintf(stderr, " -h              display this help\n");
   fprintf(stderr, " -r fd           fd to read from\n");
   exit(1);
}

int main(int argc, char * argv[]) {

   int read_fd = 0;

   int opt;
   while ((opt = getopt(argc, argv, "hr:")) != -1) {
      switch (opt) {
      case 'h':
         usage();
         break;
      case 'r':
         read_fd = atoi(optarg);
         break;
      default: /* '?' */
         usage();
      }
   }

   if(optind==argc) {
      fprintf(stderr, "Error: No fds given\n");
      usage();
   }

   // All parameters are fds.
   size_t const fd_cnt = argc - optind;
   int fds[fd_cnt];

   size_t fdidx = 0;
   for(int aidx=optind; aidx<argc; ++aidx, ++fdidx) {
      fds[fdidx] = atoi(argv[aidx]);
   }

   char buffer[4096];
   while(1) {
      ssize_t const bytes_read = read(read_fd, buffer, sizeof(buffer));
      if(bytes_read<0 && errno==EINTR)
         continue;
      if(bytes_read<=0)
         // EOF
         break;

      for(size_t fdidx=0; fdidx<fd_cnt; ++fdidx) {
         if(fds[fdidx]!=-1) {
            ssize_t const wr = write(fds[fdidx], buffer, bytes_read);

            if(wr==-1) {
               perror("write - closing fd");
               close(fds[fdidx]);
               fds[fdidx]=-1;
               continue;
            }

            if(wr!=bytes_read) {
               perror("Could not write all data");
               close(fds[fdidx]);
               fds[fdidx]=-1;
               continue;
            }
          }
      }
   }

   for(size_t fdidx=1; fdidx<fd_cnt; ++fdidx) {
      if(fds[fdidx]!=-1) {
         close(fds[fdidx]);
      }
   }

   return 0;
}
