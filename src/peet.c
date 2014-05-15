/*
 * peet
 *
 * eet (a reverse tee) for pipes / fds.
 * The program reads from different file descriptors and writes to one.
 */

#define _POSIX_C_SOURCE 200809L

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <poll.h>
#include <fcntl.h>
#include "src/version.h"

static void usage() {
  fprintf(stderr, "peet from pipexec version %d.%d\n", app_version,
          app_subversion);
  fprintf(stderr, "%s\n", desc_copyight);
  fprintf(stderr, "%s\n", desc_license);
  fprintf(stderr, "\n");
  fprintf(stderr, "Usage: peet [options] fd [fd ...]\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr, " -h              display this help\n");
  fprintf(stderr, " -w fd           fd to write to\n");
  exit(1);
}

int main(int argc, char *argv[]) {

  int write_fd = 1;

  int opt;
  while ((opt = getopt(argc, argv, "hw:")) != -1) {
    switch (opt) {
    case 'h':
      usage();
      break;
    case 'w':
      write_fd = atoi(optarg);
      break;
    default: /* '?' */
      usage();
    }
  }

  if (optind == argc) {
    fprintf(stderr, "Error: No fds given\n");
    usage();
  }

  // All parameters are fds.
  size_t const fd_cnt = argc - optind;
  // The structure for fd_cnt events
  struct pollfd fds[fd_cnt];

  size_t fdidx = 0;
  for (int aidx = optind; aidx < argc; ++aidx, ++fdidx) {
    fds[fdidx].fd = atoi(argv[aidx]);
    fds[fdidx].events = POLLIN;
  }

  // Set all the fds to nonblocking
  for (size_t fdidx = 0; fdidx < fd_cnt; ++fdidx) {
    int const flags = fcntl(fds[fdidx].fd, F_GETFL, 0);
    int const fret = fcntl(fds[fdidx].fd, F_SETFL, flags | O_NONBLOCK);
    if (fret == -1) {
      perror("fcntl nonblocking");
      exit(2);
    }
  }

  while (1) {
    // Wait forever
    int const ret = poll(fds, fd_cnt, -1);

    // Check if poll actually succeed
    if (ret == -1) {
      perror("poll");
      exit(2);
    } else if (ret == 0) {
      perror("timeout");
      exit(2);
    }

    for (size_t fdidx = 0; fdidx < fd_cnt; ++fdidx) {
      if (fds[fdidx].revents & POLLIN) {
        fds[fdidx].revents = 0;

        char buffer[4096];
        while (1) {
          ssize_t const bytes_read =
              read(fds[fdidx].fd, buffer, sizeof(buffer));
          if (bytes_read < 0 && errno == EINTR)
            continue;
          if (bytes_read < 0 && errno == EAGAIN)
            // Fd would block
            break;
          if (bytes_read <= 0) {
            // EOF
            // ???
            abort();
          }

          ssize_t const wr = write(write_fd, buffer, bytes_read);

          if (wr == -1) {
            perror("write");
            exit(2);
          }

          if (wr != bytes_read) {
            perror("Could not write all data");
            exit(2);
          }
        }
      }
    }
  }
}
