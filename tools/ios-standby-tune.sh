#!/bin/sh
# Standby tune for the iOS 18 root on raphael: stop the daemons that crash-loop
# or spin under this port (no baseband, no audio hardware, no Bluetooth), which
# was most of the heat and battery drain while the screen was off.
#
#   sh tools/ios-standby-tune.sh          # apply
#   sh tools/ios-standby-tune.sh --show   # list what is disabled
#
# Run on the phone (root). launchctl disable persists in
# /var/db/com.apple.xpc.launchd/disabled.plist on the darwin volume.
set -eu
LC=/var/jb/usr/bin/launchctl

LABELS="
com.apple.audiomxd
com.apple.corespeechd
com.apple.siriactionsd
com.apple.speechmodeltrainingd
com.apple.bluetoothd
com.apple.bluetoothuserd
com.apple.BluetoothUIService
com.apple.siri.acousticsignature
com.apple.wifip2pd
com.apple.FileProvider
com.apple.spaceattributiond
com.apple.WebBookmarks.webbookmarksd
com.apple.MTLAssetUpgraderD
com.apple.modelmanagerd
com.apple.mobile.NRDUpdated
com.apple.accessibility.axassetsd
com.apple.healthd
com.apple.parsecd
com.apple.analyticsagent
com.apple.inputanalyticsd
com.apple.PerfPowerServices
com.apple.nsurlsessiond
com.apple.cloudd
com.apple.appstored
com.apple.itunesstored
com.apple.itunescloudd
com.apple.storekitd
com.apple.appstorecomponentsd
"

if [ "${1:-}" = "--show" ]; then
	for l in $LABELS; do
		st=$($LC print system/$l 2>/dev/null | sed -n 's/.*state = //p' | head -1)
		printf '%-45s %s\n' "$l" "${st:-not loaded}"
	done
	exit 0
fi

[ "$(id -u)" = "0" ] || { echo "run as root"; exit 1; }

for l in $LABELS; do
	for d in system user/501; do
		$LC disable "$d/$l" 2>/dev/null || true
		$LC bootout "$d/$l" 2>/dev/null || true
	done
done

# The asset downloader is not a launchd job of its own; it respawns under the
# asset manager, so nudge it out once the jobs above are disabled.
killall mobileassetd 2>/dev/null || true

echo "done; disabled $(echo $LABELS | wc -w) labels"
