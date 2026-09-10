#!/bin/sh
### BEGIN INIT INFO
# Provides:          deskview-login
# Required-Start:    $all
# Required-Stop:     $all
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: DeskView Login Manager
### END INIT INFO

DAEMON=/usr/local/bin/dvlogin
PIDFILE=/run/dvlogin.pid
NAME=dvlogin
VT=7

case "$1" in
	start)
		echo "Starting $NAME on vt${VT}..."
		if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
			echo "$NAME is already running (PID $(cat "$PIDFILE"))."
			exit 0
		fi
		$DAEMON -vt $VT &
		echo $! > "$PIDFILE"
		;;
	stop)
		echo "Stopping $NAME..."
		if [ -f "$PIDFILE" ]; then
			pid=$(cat "$PIDFILE")
			if kill -0 "$pid" 2>/dev/null; then
				kill "$pid"
				# wait up to 10 seconds for clean exit
				i=0
				while [ $i -lt 100 ] && kill -0 "$pid" 2>/dev/null; do
					usleep 100000 2>/dev/null || sleep 1
					i=$((i + 1))
				done
				# force kill if still alive
				if kill -0 "$pid" 2>/dev/null; then
					kill -9 "$pid"
				fi
			fi
			rm -f "$PIDFILE"
		else
			echo "$NAME is not running."
		fi
		;;
	restart)
		$0 stop
		sleep 2
		$0 start
		;;
	status)
		if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
			echo "$NAME is running (PID $(cat "$PIDFILE"))."
		else
			echo "$NAME is not running."
			rm -f "$PIDFILE"
		fi
		;;
	*)
		echo "Usage: $0 {start|stop|restart|status}"
		exit 1
		;;
esac

exit 0