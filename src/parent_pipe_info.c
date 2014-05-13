#include "src/parent_pipe_info.h"
#include "src/logging.h"

#include <string.h>
#include <stdlib.h>

void parent_pipe_info_parse(
   parent_pipe_info_t * const iparentpipe, size_t const parent_pipe_cnt,
   int const start_argc, int const argc, char * const argv[]) {

   pipe_info_t pinfo[parent_pipe_cnt];
   pipe_info_parse(pinfo, start_argc, argc, argv, '=');

   for(size_t pidx=0; pidx<parent_pipe_cnt; ++pidx) {
      if(strstr(pinfo[pidx].from.name, "PARENT")!=pinfo[pidx].from.name) {
         logging("No PARENT specified");
         exit(10);
      }
      iparentpipe[pidx].parent_fd = pinfo[pidx].from.fd;
      iparentpipe[pidx].to.name = pinfo[pidx].to.name;
      iparentpipe[pidx].to.fd = pinfo[pidx].to.fd;
   }
}

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

void parent_pipe_info_print(
   parent_pipe_info_t const * const ippipe, unsigned long const cnt) {
   for(unsigned int pidx=0; pidx < cnt; ++pidx) {
      logging("{%d} Parent Pipe [%d] = [%s] [%d]", pidx,
              ippipe[pidx].parent_fd,
              ippipe[pidx].to.name, ippipe[pidx].to.fd);
   }
}
