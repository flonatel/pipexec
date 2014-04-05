#
# Regular cron jobs for the pipexec package
#
0 4	* * *	root	[ -x /usr/bin/pipexec_maintenance ] && /usr/bin/pipexec_maintenance
