
# pipexec itself

bin_PROGRAMS += bin/pipexec

bin_pipexec_SOURCES = \
        src/pipexec.c \
	src/logging.c \
	src/version.c \
	src/command_info.c \
	src/pipe_info.c \
	src/parent_pipe_info.c

# ptee

bin_PROGRAMS += bin/ptee

bin_ptee_SOURCES = \
        src/ptee.c


# Local Variables:
# mode: makefile
# End:
