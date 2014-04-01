/*
 * ptee
 *
 * Tee for pipes / fds.
 * This is like the 'normal' tee - except that it does not output to
 * different files but to differend fds.
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

int main(int argc, char * argv[]) {
   // All parameters are fds.
   size_t const fd_cnt = argc - 1;
   int fds[fd_cnt];

   for(int aidx=1; aidx<argc; ++aidx) {
      fds[aidx-1] = atoi(argv[aidx]);
   }

   char buffer[4096];
   while(1) {
      ssize_t const bytes_read = read(0, buffer, sizeof(buffer));
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
               abort();
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
