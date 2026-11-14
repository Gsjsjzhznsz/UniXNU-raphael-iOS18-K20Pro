#!/usr/bin/env bash
# Stage the Wi-Fi stack's kernel glue (patch 0291 registers it).
#
# The sources are the iOS 16.5 tree's (repo, xnu-8796.121.2), where the WCN3990
# on raphael was verified end to end; they are staged here unchanged so the
# first build can report what xnu-11215 changed. Nothing runs unless the boot
# argument wifi=1 (or wifi_step=N) is given, so a normal boot is untouched.
# hakios.wifi_shim.c stands in for the neighbours those sources had in the iOS
# 16 tree and do not have here yet (see its header comment).
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
test -f bsd/conf/files
test -d bsd/dev
test -d bsd/sys
install -m 0644 \
	"$here/hakios.wifi.c" \
	"$here/hakios.wlfw.c" \
	"$here/hakios.qmi.c" \
	"$here/hakios.lx.c" \
	"$here/hakios.net.c" \
	"$here/hakios.sta.c" \
	"$here/hakios.apple80211.c" \
	"$here/hakios.wifi_shim.c" \
	bsd/dev/
install -m 0644 \
	"$here/hakios.input.h" \
	"$here/hakios.qmi.h" \
	"$here/hakios.qrtr.h" \
	"$here/hakios.sta.h" \
	"$here/hakios.lx.h" \
	"$here/hakios.net.h" \
	bsd/sys/
