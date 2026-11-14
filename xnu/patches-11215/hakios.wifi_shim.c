/*
 * What the Wi-Fi glue (bsd/dev/hakios.*.c) expects from the rest of the iOS 16
 * tree but xnu-11215 does not have yet.
 *
 * The glue came over from the iOS 16.5 tree unchanged, so it still asks for a
 * few symbols its neighbours there provide: the board's name (hakios.input.c),
 * the SPMI arbiter lock (hakios.buttons.c), the firmware files of the small
 * image (hakios.image.c), the SNOC's MSIs (osfmk/arm64/gic_v3_irq.c) and the
 * Apple80211 boundary (iokit/Kernel/hakios.wlan.cpp).
 *
 * Each of these is a stub until the device it belongs to is ported; none of
 * them changes what the glue compiles to. The interrupt ones report failure,
 * which is what the LinuxKPI layer handles when the radio is started without
 * them (Stage C ports the real GICv3 driver).
 */
#include <kern/locks.h>
#include <libkern/libkern.h>
#include <mach/vm_types.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/hakios.input.h>
#include <dev/sweet_board.h>

/* ---- the board (bsd/dev/hakios.input.c on iOS 16) ---- */

int
hakios_is_raphael(void)
{
	return sweet_board_raphael() ? 1 : 0;
}

/* ---- the SPMI arbiter (bsd/dev/hakios.buttons.c on iOS 16) ----
 *
 * The glue only ever takes this around its own arbiter transaction; with no
 * other SPMI driver in this kernel the lock protects nothing yet, but it must
 * exist so the code stays the same when the buttons driver arrives.
 */
static LCK_GRP_DECLARE(hakios_spmi_lck_grp, "hakios_spmi");
static LCK_SPIN_DECLARE(hakios_spmi_lock, &hakios_spmi_lck_grp);

void
hakios_spmi_lock(void)
{
	lck_spin_lock(&hakios_spmi_lock);
}

void
hakios_spmi_unlock(void)
{
	lck_spin_unlock(&hakios_spmi_lock);
}

/* ---- the small image's fw/ (bsd/dev/hakios.image.c on iOS 16) ----
 *
 * The firmware is staged into the boot image under fw-wifi/ for Stage C; until
 * that reader is ported the glue is told there is no file, which is a case it
 * already handles (it says so and fails the step).
 */
const uint8_t *
hakios_image_file(__unused const char *dir, __unused const char *name, __unused uint64_t *size,
    __unused int *disk_out)
{
	return NULL;
}

/* bsd/dev/hakios.image.c exports this over bsd/dev/memdev.c; memdev.c in 11215
 * has mdevlookup() only. Nothing here can map a disk image's bytes yet. */
int
mdevgetkernelrange(__unused dev_t dev, __unused vm_offset_t *base, __unused uint64_t *size)
{
	return -1;
}

/* ---- en0's Apple80211 side (iokit/Kernel/hakios.wlan.cpp on iOS 16) ---- */

void
HakiosWLANStart(__unused const char *interface, __unused const uint8_t *address)
{
}

void
hakios_virtio_net_init(void)
{
}

/* ---- the SNOC's interrupts (osfmk/arm64/gic_v3_irq.c on iOS 16) ----
 *
 * Stage C ports that file. Until then the LinuxKPI layer cannot get an IRQ and
 * says so; the radio's bring-up still runs as far as polling takes it.
 */
kern_return_t
gic_v3_spi_attach(__unused uint32_t intid, __unused bool edge,
    __unused void (*handler)(void *context, uint32_t intid), __unused void *context)
{
	return KERN_FAILURE;
}

void
gic_v3_spi_mask(__unused uint32_t intid)
{
}

void
gic_v3_spi_unmask(__unused uint32_t intid)
{
}

void
gic_v3_spi_detach(__unused uint32_t intid)
{
}

uint64_t
gic_v3_spi_count(__unused uint32_t intid)
{
	return 0;
}
