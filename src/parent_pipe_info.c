#include "src/parent_pipe_info.h"

#include <string.h>

unsigned int parent_pipe_info_clp_count(
   int const start_argc, int const argc, char * const argv[]) {
   unsigned int cnt = 0;
   for(int i = start_argc; i<argc; ++i) {
      if(argv[i][0]=='{' && strchr(argv[i], '=')!=NULL) {
         ++cnt;
      }
   }
   return cnt;
}
