# Version 2.6.1

* Fixes 32 bit build
  On 32 bit platforms size_t is not unsigned long.
  Fixes size_t output.

# Version 2.6.0

* Check for duplicate pipe definition
  pipe endpoints can only be used once. If two times the same
  source or sink were specified in the command line, the behavior
  was (and still is) undefined.
  Now a check was added that emits a error log, if a pipe endpoint
  is specified twice.
* Added json logging
  Using the -j option pipexec now logs in json format that can be
  parsed.  This provides information about status and exit codes
  of child processes.
* Exit with 1 when one child fails
  When at least one child fails, pipexec also fails with '1'.
* Check for rubbish on the command line
  Additional or unparsable command line arguments are now seen
  as an error. Before there were silently ignored.
* Fixed false positive dangling pointer check of gcc 12
  GCC introduces a new dangling pointer check which runs into
  a false positive.
* Add compile check for wide range of compilers
  As public travis access was switched off some time, pipexec now
  uses github CI/CD for compile check. The following compilers
  are checked:
  - gcc: 9, 10, 11, 12
  - clang: 10, 11, 12, 13, 14
