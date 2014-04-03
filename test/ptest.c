#include <unistd.h>

int main() {
   size_t r;
   while(1) {
      r=write(1, "ptest out\n", 10);
      char buf[10];
      r=read(0, buf, 10);
      r=write(3, buf, 10);
      sleep(1);
   }
   (void)r;
   return 0;
}
