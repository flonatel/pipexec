#include <unistd.h>

int main() {
   while(1) {
      (void)write(1, "ptest out\n", 10);
      char buf[10];
      (void)read(0, buf, 10);
      (void)write(3, buf, 10);
      sleep(1);
   }
   return 0;
}
