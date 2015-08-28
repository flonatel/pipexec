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

size_t const buffer_size = 4096;

static void usage() {
  fprintf(stderr, "peet from pipexec version %s\n", app_version);
  fprintf(stderr, "%s\n", desc_copyight);
  fprintf(stderr, "%s\n", desc_license);
  fprintf(stderr, "\n");
  fprintf(stderr, "Usage: peet [options] fd [fd ...]\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr, " -h              display this help\n");
  fprintf(stderr, " -d              print some debug output\n");
  fprintf(stderr, " -w fd           fd to write to\n");
  exit(1);
}

int readable_fd_available(struct pollfd *fds, size_t fd_cnt) {
  for (size_t fdidx = 0; fdidx < fd_cnt; ++fdidx) {
    if (fds[fdidx].fd >= 0) {
      return 1;
    }
  }
  return 0;
}

/*
 * Read from any of the signaled fds.
 * When a EOF is seen, handle the removal of the fd from the fds.
 */
ssize_t read_from_fd(struct pollfd *fds, size_t fd_cnt, char *buffer) {

  for (size_t fdidx = 0; fdidx < fd_cnt; ++fdidx) {

    // Ignore removed fd
    if (fds[fdidx].fd < 0) {
      continue;
    }

    if (fds[fdidx].revents & POLLIN) {
      ssize_t const bytes_read = read(fds[fdidx].fd, buffer, buffer_size);
      if (bytes_read < 0 && errno == EINTR)
        continue;
      if (bytes_read < 0 && errno == EAGAIN)
        // Fd would block
        continue;
      if (bytes_read <= 0) {
        // EOF from this fd
        // The handling of this was finished.
        fds[fdidx].fd = -1;
        if (!readable_fd_available(fds, fd_cnt)) {
          // There are no fds to read from
          return -1;
        }
        continue;
      }
      fds[fdidx].revents &= (!POLLIN);
      return bytes_read;
    }

    if (fds[fdidx].revents & POLLHUP) {
      fds[fdidx].fd = -1;
      if (!readable_fd_available(fds, fd_cnt)) {
        // There are no fds to read from
        return -1;
      }
      continue;
    }
  }
  // There are no fds to read from
  return -1;
}

int main(int argc, char *argv[]) {

  int write_fd = 1;
  int use_debug_log = 0;

  int opt;
  while ((opt = getopt(argc, argv, "dhw:")) != -1) {
    switch (opt) {
    case 'd':
      use_debug_log = 1;
      break;
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
  size_t fd_cnt = argc - optind;
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

    char buffer[buffer_size];
    ssize_t const bytes_read = read_from_fd(fds, fd_cnt, buffer);
    if (bytes_read == -1) {
      // No open FDs any more.
      break;
    }

    if (use_debug_log) {
      ssize_t const wrl = write(write_fd, "PEET_DEBUG[", 11);
      (void)wrl;
    }

    ssize_t const wr = write(write_fd, buffer, bytes_read);

    if (use_debug_log) {
      ssize_t const wrl = write(write_fd, "]", 1);
      (void)wrl;
    }

    if (wr == -1) {
      perror("write");
      exit(2);
    }

    if (wr != bytes_read) {
      perror("Could not write all data");
      exit(2);
    }
  }

  return 0;
}
