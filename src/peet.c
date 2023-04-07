/*
 * peet
 *
 * eet (a reverse tee) for pipes / fds.
 * The program reads from different file descriptors and writes to one.
 *
 * Copyright 2015,2022 by Andreas Florath
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <poll.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include "src/version.h"

size_t const buffer_size = 4096;

/* This is the structure which is created for each file descriptor.
   As the boundary read / write needs always complete blocks,
   this includes also the buffer and the amount of valid data in the
   buffer. */
struct fddata_t {
  char * m_buffer;
  ssize_t m_buffer_size;
  ssize_t m_buffer_used;
  int m_use_boundary;
  int m_eof_seen;
};

void fddata_init(struct fddata_t *self, struct pollfd *pfd, int fd,
		 int use_boundary, int block_size) {
  pfd->fd = fd;
  pfd->events = POLLIN;

  int const flags = fcntl(pfd->fd, F_GETFL, 0);
  int const fret = fcntl(pfd->fd, F_SETFL, flags | O_NONBLOCK);
  if (fret == -1) {
    perror("fcntl nonblocking");
    exit(2);
  }

  self->m_buffer_size = block_size;
  self->m_buffer_used = 0;
  self->m_buffer = malloc(block_size);

  self->m_use_boundary = use_boundary;
  self->m_eof_seen = 0;
}

void fddata_write(struct fddata_t *self, int write_fd, int use_debug_log) {

  if (use_debug_log) {
    fprintf(stderr, "WRITE len [%zd]\n", self->m_buffer_used);
  }

  ssize_t const wr = write(write_fd, self->m_buffer, self->m_buffer_used);

  if (wr == -1) {
    perror("write");
    exit(2);
  }

  if (wr != self->m_buffer_used) {
    perror("Could not write all data");
    exit(2);
  }

  self->m_buffer_used = 0;
}

// Returns
// 0 - if everything is fine - e.g. no state change
// 1 - state change: eof_seen the first time
int fddata_read_write(struct fddata_t *self, struct pollfd *pfd,
		      int write_fd, int use_debug_log) {
  // Ignore entries where eof was seen.
  if(self->m_eof_seen) {
    return 0;
  }

  if (pfd->revents & POLLIN) {

    size_t const bytes_to_read = self->m_buffer_size - self->m_buffer_used;
    ssize_t const bytes_read = read(
       pfd->fd, self->m_buffer + self->m_buffer_used, bytes_to_read);
    if (bytes_read < 0 && errno == EINTR)
      return 0;
    if (bytes_read < 0 && errno == EAGAIN)
      // Fd would block
      return 0;
    if (bytes_read <= 0) {
      // EOF from this fd
      // The handling of this was finished.
      // Write possible remaining data.
      fddata_write(self, write_fd, use_debug_log);
      if(use_debug_log) {
	fprintf(stderr, "EOF [%d]", pfd->fd);
      }
      self->m_eof_seen = 1;
      pfd->fd = -1;
      return 1;
    }
    pfd->revents &= (!POLLIN);
    self->m_buffer_used += bytes_read;
    assert(self->m_buffer_used <= self->m_buffer_size);
    if (! self->m_use_boundary || self->m_buffer_used == self->m_buffer_size) {
      fddata_write(self, write_fd, use_debug_log);
      return 0;
    }
  }

  if (pfd->revents & POLLHUP) {
    fddata_write(self, write_fd, use_debug_log);
    self->m_eof_seen = 1;
    pfd->fd = -1;
    return 1;
  }

  // There are no fds to read from
  return 0;
}

static void usage() {
  fprintf(stderr, "peet from pipexec version %s\n", app_version);
  fprintf(stderr, "%s\n", desc_copyight);
  fprintf(stderr, "%s\n", desc_license);
  fprintf(stderr, "\n");
  fprintf(stderr, "Usage: peet [options] fd [fd ...]\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr, " -h              display this help\n");
  fprintf(stderr, " -b num          read num bytes from each input\n");
  fprintf(stderr, " -d              print some debug output\n");
  fprintf(stderr, " -w fd           fd to write to\n");
  exit(1);
}

int readable_fd_available(struct fddata_t *fddata, size_t fd_cnt) {
  for (size_t fdidx = 0; fdidx < fd_cnt; ++fdidx) {
    if (! fddata[fdidx].m_eof_seen) {
      return 1;
    }
  }
  return 0;
}

// Returns:
// 0 - no readable fd available
// 1 - everything ok
int read_write(struct fddata_t * fddata, struct pollfd * fds, int fd_cnt, int write_fd,
	       int use_debug_log) {

  for (int fdidx = 0; fdidx < fd_cnt; ++fdidx) {
    int const eof_seen = fddata_read_write(&fddata[fdidx], &fds[fdidx], write_fd, use_debug_log);
    if (eof_seen) {
      if (use_debug_log) {
	fprintf(stderr, "EOF SEEN idx [%d]\n", fdidx);
      }
      if (!readable_fd_available(fddata, fd_cnt)) {
	return 0;
      }
    }
  }
  return 1;
}

int main(int argc, char *argv[]) {

  int write_fd = 1;
  int use_debug_log = 0;
  int block_size = 4096;
  int use_boundary = 0;

  int opt;
  while ((opt = getopt(argc, argv, "b:dhw:")) != -1) {
    switch (opt) {
    case 'b':
      block_size = atoi(optarg);
      use_boundary = 1;
      break;
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
  // The structure for the fd data structs
  struct fddata_t fddata[fd_cnt];
  struct pollfd fds[fd_cnt];

  size_t fdidx = 0;
  for (int aidx = optind; aidx < argc; ++aidx, ++fdidx) {
    fddata_init(&fddata[fdidx], &fds[fdidx], atoi(argv[aidx]), use_boundary, block_size);
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

    if( ! read_write(fddata, fds, fd_cnt, write_fd, use_debug_log) ) {
      break;
    }
  }

  return 0;
}
