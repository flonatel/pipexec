
if !USE_VERSION_FILE
.FORCE: src/app_version.c
src/app_version.c:
	 bash $(top_srcdir)/build/version-gen.sh ${top_srcdir}
endif

# pipexec itself

bin_PROGRAMS += bin/pipexec

bin_pipexec_SOURCES = \
        src/pipexec.c \
	src/logging.c \
	src/version.c \
	src/app_version.c \
	src/command_info.c \
	src/pipe_info.c

# ptee

bin_PROGRAMS += bin/ptee

bin_ptee_SOURCES = \
	src/version.c \
	src/app_version.c \
        src/ptee.c

# peet

bin_PROGRAMS += bin/peet

bin_peet_SOURCES = \
	src/version.c \
	src/app_version.c \
        src/peet.c


# Local Variables:
# mode: makefile
# End:
