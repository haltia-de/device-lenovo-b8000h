// SPDX-License-Identifier: GPL-2.0
/*
 * LD_PRELOAD shim for libhybris on glibc/ARM: reserve the bionic TLS slots.
 *
 * The Android 4.4 EGL/GLES code writes TLS_SLOT_OPENGL_API/TLS_SLOT_OPENGL
 * (tp+12..tp+19) straight through TPIDRURO.  glibc (TLS variant I) places the
 * first static TLS block right after the 8-byte TCB, and static TLS modules
 * are laid out in load order: executable, LD_PRELOAD objects, then DT_NEEDED
 * libraries.  For an executable without its own PT_TLS this object is
 * therefore first, and its pad takes those writes instead of libc's thread
 * data (locale pointer, errno...).
 *
 * Build: gcc -O2 -shared -fPIC -o libtlspad.so tlspad.c
 * Use:   LD_PRELOAD=/usr/local/hybris/lib/libtlspad.so program
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Initialised, so it lands in .tdata: the block then starts with the pad. */
__attribute__((used, tls_model("initial-exec")))
__thread void *hybris_bionic_tls_pad[8] = { (void *)1 };

__attribute__((constructor))
static void tlspad_check(void)
{
	const char *tp = __builtin_thread_pointer();
	const char *pad = (const char *)hybris_bionic_tls_pad;
	const char *dbg = getenv("HYBRIS_TLSPAD_DEBUG");

	hybris_bionic_tls_pad[0] = NULL;
	if (pad <= tp + 12 && pad + sizeof(hybris_bionic_tls_pad) >= tp + 20) {
		if (dbg && *dbg)
			fprintf(stderr, "tlspad: ok, pad at tp+%d\n", (int)(pad - tp));
		return;
	}
	/*
	 * The executable has its own TLS block in front: the GL slots would
	 * land on it.  Warn loudly; HYBRIS_TLSPAD_STRICT=1 makes it fatal.
	 */
	fprintf(stderr, "tlspad: pad at tp+%d does not cover tp+12..19 "
		"(executable has its own TLS?); bionic GL slots will corrupt it\n",
		(int)(pad - tp));
	dbg = getenv("HYBRIS_TLSPAD_STRICT");
	if (dbg && !strcmp(dbg, "1"))
		abort();
}
