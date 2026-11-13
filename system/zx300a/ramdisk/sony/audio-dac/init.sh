#!/sony/busybox sh
set -eu
export LD_LIBRARY_PATH=/vendor/lib:/system/lib
trap '/system/bin/setprop sys.walkdroid.dac_tables failed' EXIT
/system/bin/setprop sys.walkdroid.cfw_gain unconfirmed
/system/bin/setprop sys.walkdroid.dac_tables initializing
/sony/audio-dac/sony-audio-dac-init --initialize /sony/audio-dac >/dev/walkdroid-dac-init.log 2>&1
SELECT=$(/sony/audio-gain/sony-audio-gain-select /data/sony-audio-controls-v11)
/sony/audio-gain/sony-audio-gain-tables --apply /sony/audio-gain/assets "$SELECT" >/dev/walkdroid-gain-init.log 2>&1
/system/bin/setprop sys.walkdroid.cfw_gain "$SELECT"
/system/bin/setprop sys.walkdroid.dac_tables ready
trap - EXIT
