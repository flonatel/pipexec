pipexec
=======

Handling pipe of commands like a single command

[![Build
Status](https://secure.travis-ci.org/flonatel/pipexec.png)](http://travis-ci.org/flonatel/pipexec)

# Introduction
Most systems to start and run processes during system start-up time do
not support pipe.  If you need to run a pipe of programs from an
/etc/init.d script you are mostly lost.

Depending on your distribution you can be happy if it starts up - but
when it comes to stopping, at least the current Debian
start-stop-daemon and RHEL 6 daemon function fail.

# Purpose
*pipexec* was designed to handle a hole pipe of commands to behave
like a single command.

During start-up *pipexec* fork() / pipe() / exec() the passed in
commands.  When *pipexec* receives a SIGTERM, SIGQUIT or SIGINT it
stops the whole pipe and itself. When *pipexec* received a SIGHUP the
pipe of commands is restarted. When one command in the pipe exits with
a signal, the pipe of commands is restarted.

# Usage
    $ pipexec /bin/ls -l "|" /bin/grep LIC
    -rw-r--r-- 1 florath florath 18025 Mar 16 19:36 LICENSE

Be sure to escape the pipe symbol.

It is possible to specify a fd for logging.

    $ pipexec -l 2 /bin/ls -l "|" /bin/grep LIC
    2014-03-16 19:55:45;pexec;10746;pipexec version 1.0
    2014-03-16 19:55:45;pexec;10746;(c) 2014 by flonatel GmbH & Co, KG
    2014-03-16 19:55:45;pexec;10746;License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl.html>.
    2014-03-16 19:55:45;pexec;10746;Parsing command
    2014-03-16 19:55:45;pexec;10746;Arg pos [  3]: [/bin/ls]
    2014-03-16 19:55:45;pexec;10746;Arg pos [  4]: [-l]
    2014-03-16 19:55:45;pexec;10746;Arg pos [  5]: [|]
    2014-03-16 19:55:45;pexec;10746;Pipe symbol found at pos [5]
    [...]

Or

    $ pipexec -l 7 /bin/ls -l "|" /bin/grep LIC 7>/tmp/pipexec.log
    -rw-r--r-- 1 florath florath 18025 Mar 16 19:53 LICENSE
    $ head -2 /tmp/pipexec.log
    2014-03-16 19:57:31;pexec;10762;pipexec version 1.0
    2014-03-16 19:57:31;pexec;10762;(c) 2014 by flonatel GmbH & Co, KG


