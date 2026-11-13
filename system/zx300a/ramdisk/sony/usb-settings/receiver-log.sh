#!/sony/busybox sh
umask 077
exec /vendor/bin/sony-receiverd /data/walkdroid/receiver /data/sony-receiver/sessions - "$@" 2>>/dev/sony-receiver-session.log
