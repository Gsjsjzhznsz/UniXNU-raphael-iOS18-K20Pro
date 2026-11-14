# Wi-Fi on iOS 18 (xnu-11215): bringing the Helium stack over

Status: **plan + stage A not started**. The only working Wi-Fi stack for raphael is in the iOS 16.5 tree
(`repo`, xnu-8796.121.2, branch `ios16-raphael-cleanup` / `raphael-bringup`), verified there with 2.4 GHz
WPA2 and 5 GHz WPA3 SAE plus real traffic. The iOS 18 tree (`jacky-ios18`, xnu-11215.81.4) has no Wi-Fi at
all (bergamota's `beryllium_wlan.c` is a WCN3990/BT bring-up ladder, not a driver), so Wi-Fi on iOS 18 means
porting the iOS 16 stack.

## What the stack is made of (source: `repo`, all LGPL/BSD except the firmware)

| Piece | Where | Size |
| --- | --- | --- |
| Kernel glue (ICNSS/MPSS/PAS, SMEM, SMP2P, GLINK, QRTR, RMTFS, TFTP, WLAN firmware load) | `xnu/files/bsd/dev/hakios.wifi.c` | 5666 lines |
| Stand-in for the WLAN firmware's QMI service (wifi_lx=1 / step 15) | `hakios.wlfw.c`, `hakios.wlfw_ath11k.h` | 545 |
| QRTR/QMI transport over GLINK | `hakios.qmi.c` | 963 |
| LinuxKPI OS layer (`lx_os_*`: MMIO map, IRQs, threads, net attach, options) | `hakios.lx.c` | 586 |
| Kernel network interface (ifnet/mbuf KPI) | `hakios.net.c` | 428 |
| Station boundary for Apple80211 | `hakios.sta.c` | ~400 |
| Apple80211 user client | `hakios.apple80211.c` | ~700 |
| LinuxKPI + Linux ath10k + FreeBSD 15.1 linuxkpi + raphael shims, built into one object | `wifi/lx/**` (42 files) + `tools/mk-ath10k.py` | 368 KiB + one object |
| Headers | `xnu/files/bsd/sys/hakios.{input,qmi,qrtr,sta,lx,net}.h` | — |

The object exports exactly `lx_prepare`, `lx_start`, `lx_stop`, `lx_transmit` and imports `lx_os_*`,
`qmi_*`, `hakios_qmi_*` from the kernel glue (`mk-ath10k.py` refuses to link if any other symbol is
undefined).

## Boot flow (as in `repo`)

`bsd_init()` → `hakios_input_init()` → `hakios_wifi_init()`: with `wifi=1` it loads `fw-wifi/wpss.mdt`
into MPSS memory, brings up SMEM/SMP2P/GLINK/QRTR, does the ICNSS WLAN handshake (five PM8150L rails,
QDSS vote, MSA permissions), PAS-auths and starts the DSP, waits for the WLAN firmware READY, then hands
the radio to ath10k (`lx_start`), registers the net interface, and Apple80211 talks to it through
`hakios.sta.c`.

## Stage A — compile the kernel glue on xnu-11215 (first milestone)

1. Copy into `xnu/patches-11215/`: `hakios.wifi.c`, `hakios.wlfw.c`, `hakios.wlfw_ath11k.h`,
   `hakios.qmi.c`, `hakios.lx.c`, `hakios.net.c`, `hakios.sta.c`, `hakios.apple80211.c`, and the six
   `hakios.*.h` headers.
2. Stage script `0290-stage-hakios-wifi.sh` (same pattern as `0268`/`0274`): install the `.c`/`.h` into
   `bsd/dev/` and `bsd/sys/`.
3. Patch `0291`: add the eight `bsd/dev/hakios.*.c` lines to `bsd/conf/files` and call `hakios_wifi_init()`
   from `bsd_init()` next to `devfs_kernel_mount()` (repo's hook is in `bsd/kern/bsd_init.c`).
4. Until the ath10k object is linked, add `hakios.lx_stub.c` with the four exported entry points
   (`lx_prepare`/`lx_start`/`lx_stop`/`lx_transmit` returning `ENXIO`), so Stage A links.
5. `.github/workflows/xnu.yml`: the loop copies only `*.patch`/`*.sh` plus `sweet_scanout.*` and
   `raphael_dsi.*`; extend the `cp` lines with `hakios.*` so the stage script finds its sources.
6. Build and fix the API deltas. Expected ones (xnu-8796 → 11215):
   - `hakios.net.c`: `ifnet_allocate_extended`, `ifnet_attach`, `ifnet_input`, `mbuf_*` KPI (Skywalk-era
     signature changes), `ifnet_set_flags`, `kern_event` posting.
   - `kernel_thread_start` / `thread_deallocate`, `kalloc_data`/`zalloc_flags_t`, `ml_io_map` return types,
     `PE_parse_boot_argn`, `lck_mtx_*`, `kern_return_t` vs `int` in a few helpers.
   - `hakios.apple80211.c` `proc_selfname`, `kern_control` registration API.
7. Expect **many** errors on the first run; the CI collects them in `out/errors.txt` (the workflow's
   error-summary step already greps `fatal error:` lines).

## Stage B — build and link the ath10k object

`tools/mk-ath10k.py` must run on the macOS runner (it needs `xnu-deps/host/bin/ld64` and `nm`, and the
FreeBSD 15.1 + Linux 7.0.14 tarballs; cache them with actions/cache). Then link it:

- `dxb/build.sh` builds `link.filelist` and links with `-filelist`; add a hook after that line to append
  `$GITHUB_WORKSPACE/ath10k/out/ath10k.o` (or pass `WIFI_OBJECT`), with a check that the file exists so a
  silent omission fails the build.
- The stage-A stubs must then be dropped, or kept weak so the real object wins.

## Stage C — runtime bring-up on the phone

Same ladder as `repo/docs/RAPHAEL-WIFI.md`, but on 11215: `wifi=1` and `wifi_step=N` to stop at each stage,
`wifi_lx=2` for the driver, `wifi_bdf=bd_f11cn.bin`, `wifi_smmu=2`, `wifi_keep=1`, `wifi_wait=10`,
`wifi_listen=15`. Firmware (`wpss.mdt/b00..b31`, `wlanmdsp.mbn`, `firmware-5.bin`, `bd_f11cn.bin`) is
private and must stay out of the repo and out of CI; it goes into the boot image's `fw-wifi/`.

Known bug to fix on the way: in the iOS 16 tree, Wi-Fi off then on shows a network list but cannot
reconnect (`failed to power on device: -19`). The path to check is `wifi/lx/ath10k/host.c`:
`lx_start()` returns an error after a successful `ath10k_snoc_host_register()` without calling
`lx_stop()`, so the next start hits `platform_driver_register()`'s `if (driver || ...) return -ENODEV`.
A second suspect is the probe-failure path of `platform_driver_register()` (only `irq_detach()`, no
driver/drvdata reset). This fix belongs in `repo` (it is testable there) and should be carried over.

## Order of work

1. Stage A compile (this document's first milestone) — several CI rounds.
2. Stage B link — one or two rounds.
3. Stage C runtime: ICNSS/MPSS first, then QRTR/GLINK, then the driver, then scan/join; each stage is a
   boot with `wifi_step=N`.
4. `-19` off/on fix in `repo`, verified there, then carried over.
