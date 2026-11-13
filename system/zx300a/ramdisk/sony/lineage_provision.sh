#!/sony/busybox sh
set -e
. /sony/framework_env.sh
/system/bin/cmd settings put global device_provisioned 1
/system/bin/cmd settings put secure user_setup_complete 1
/system/bin/cmd settings put global device_name 'Sony Walkman ZX300A'
/system/bin/cmd settings put secure doze_enabled 0
/system/bin/cmd settings put secure doze_always_on 0
/system/bin/cmd settings put secure doze_pulse_on_pick_up 0
/system/bin/cmd settings put secure doze_pulse_on_double_tap 0
/system/bin/cmd package disable --user 0 org.lineageos.lineageparts/.lineagestats.ReportingServiceManager
/system/bin/cmd package disable --user 0 org.lineageos.lineageparts/.lineagestats.ReportingService
/system/bin/cmd package disable --user 0 org.lineageos.lineageparts/.lineagestats.StatsUploadJobService
/system/bin/setprop sys.zx300a.lineage_configured 1
