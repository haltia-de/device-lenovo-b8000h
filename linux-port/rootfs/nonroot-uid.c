// SPDX-License-Identifier: GPL-2.0
/*
 * LD_PRELOAD shim for GNOME Software on the B8000-H: the Phosh session runs
 * as root, and gnome-software's main() exits with "Software should be run
 * as a non-root user" when getuid() or geteuid() is 0.
 *
 * Only calls made from the main executable get "nobody": libraries keep the
 * real uid (GTK/libwayland check the runtime directory against it and fail
 * to open the display otherwise).  The process keeps its real root
 * credentials, so D-Bus, polkit and file access are unchanged.
 *
 * No libc link dependency (dl_iterate_phdr is resolved from musl at load
 * time), so it builds with any armhf compiler:
 *   gcc -O2 -shared -fPIC -nostdlib -o nonroot-uid.so nonroot-uid.c
 * Used by /usr/local/bin/gnome-software (gnome-software-nonroot.sh).
 */
typedef unsigned int uid_t;

#define NOBODY 65534
#define PT_LOAD 1
#define NR_GETUID32 199
#define NR_GETEUID32 201

struct phdr32 {
	unsigned int p_type, p_offset, p_vaddr, p_paddr;
	unsigned int p_filesz, p_memsz, p_flags, p_align;
};

struct dl_phdr_info {
	unsigned long dlpi_addr;
	const char *dlpi_name;
	const struct phdr32 *dlpi_phdr;
	unsigned short dlpi_phnum;
};

extern int dl_iterate_phdr(int (*cb)(struct dl_phdr_info *, unsigned long,
				     void *), void *data);

static unsigned long exe_lo, exe_hi;

/* The first object reported is the main program. */
static int exe_range(struct dl_phdr_info *info, unsigned long size, void *data)
{
	unsigned long lo = ~0UL, hi = 0;
	int i;

	(void)size;
	(void)data;
	for (i = 0; i < info->dlpi_phnum; i++) {
		const struct phdr32 *ph = &info->dlpi_phdr[i];

		if (ph->p_type != PT_LOAD)
			continue;
		if (info->dlpi_addr + ph->p_vaddr < lo)
			lo = info->dlpi_addr + ph->p_vaddr;
		if (info->dlpi_addr + ph->p_vaddr + ph->p_memsz > hi)
			hi = info->dlpi_addr + ph->p_vaddr + ph->p_memsz;
	}
	exe_lo = lo;
	exe_hi = hi;
	return 1;
}

static int from_exe(void *ret)
{
	unsigned long a = (unsigned long)ret;

	if (!exe_hi)
		dl_iterate_phdr(exe_range, 0);
	return a >= exe_lo && a < exe_hi;
}

static uid_t sys_uid(int nr)
{
	register unsigned int r7 __asm__("r7") = nr;
	register unsigned int r0 __asm__("r0");

	__asm__ volatile("svc 0" : "=r"(r0) : "r"(r7) : "memory");
	return r0;
}

uid_t getuid(void)
{
	if (from_exe(__builtin_return_address(0)))
		return NOBODY;
	return sys_uid(NR_GETUID32);
}

uid_t geteuid(void)
{
	if (from_exe(__builtin_return_address(0)))
		return NOBODY;
	return sys_uid(NR_GETEUID32);
}
