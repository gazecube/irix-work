#!/bin/sh
# Private end-to-end test: Xvfb + the real 4Dwm + ewmhd + test-ewmhd.  Stops only what it started.
cd "$(dirname "$0")" || exit 1
D=${TESTDISPLAY:-:96}
R=$HOME/opt/claude-work/ewmhd-test
rm -rf "${R:?}" && mkdir -p "$R/home" "$R/run" "$R/tmp"
chmod 700 "$R/run"
pids=
cleanup() { for p in $pids; do kill "$p" 2>/dev/null; done; }
trap cleanup EXIT INT TERM
export DISPLAY=$D HOME=$R/home XDG_RUNTIME_DIR=$R/run TMPDIR=$R/tmp
unset XAUTHORITY WAYLAND_DISPLAY DBUS_SESSION_BUS_ADDRESS
Xvfb $D -screen 0 1280x1024x24 -nolisten tcp >"$R/xvfb.log" 2>&1 & pids="$pids $!"
i=0; while [ ! -S /tmp/.X11-unix/X${D#:} ] && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done
xrdb -merge <<X
*SG_UseBackgrounds: False
*freezeOnConfig: false
X
IRIX_DEFINE_ROOT_POINTER_CURSOR=1 /usr/lib/irix-recomp/native/usr/bin/X11/4Dwm >"$R/4Dwm.log" 2>&1 & pids="$pids $!"
i=0; until xprop -root _MOTIF_WM_INFO 2>/dev/null | grep -q 'WM_INFO'; do sleep 0.5; i=$((i+1)); [ $i -gt 40 ] && { echo "4Dwm did not come up"; tail -5 "$R/4Dwm.log"; exit 3; }; done
echo "4Dwm is managing $D"
./ewmhd 2>"$R/ewmhd.log" & pids="$pids $!"
sleep 1; cat "$R/ewmhd.log"
./test-ewmhd; rc=$?
echo "(logs in $R)"
exit $rc
