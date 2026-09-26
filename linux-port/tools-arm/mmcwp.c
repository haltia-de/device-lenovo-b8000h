/*
 * mmcwp: query / clear eMMC write-protect groups through MMC_IOC_CMD.
 * Freestanding (raw syscalls), armv7 static, no libc:
 *   clang --target=armv7a-linux-gnueabihf -Os -static -nostdlib -ffreestanding \
 *         -fno-stack-protector -o mmcwp mmcwp.c
 * Usage:
 *   mmcwp /dev/mmcblk1 q <sector>          CMD30 + CMD31 at sector, CMD13
 *   mmcwp /dev/mmcblk1 c <sector> <groups> CMD29 each group from sector
 */
typedef unsigned int u32;
typedef unsigned long long u64;

struct mmc_ioc_cmd {
	int write_flag;
	int is_acmd;
	u32 opcode;
	u32 arg;
	u32 response[4];
	unsigned int flags;
	unsigned int blksz;
	unsigned int blocks;
	unsigned int postsleep_min_us;
	unsigned int postsleep_max_us;
	unsigned int data_timeout_ns;
	unsigned int cmd_timeout_ms;
	u32 pad;
	u64 data_ptr;
};

#define MMC_IOC_CMD 0xC048B300u
#define RSP_R1  0x15u
#define RSP_R1B 0x1du
#define CMD_AC   0x00u
#define CMD_ADTC 0x20u

static long sys3(long n, long a, long b, long c)
{
	register long r7 asm("r7") = n;
	register long r0 asm("r0") = a;
	register long r1 asm("r1") = b;
	register long r2 asm("r2") = c;
	asm volatile("svc 0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory");
	return r0;
}

static long sys6(long n, long a, long b, long c, long d, long e, long f)
{
	register long r7 asm("r7") = n;
	register long r0 asm("r0") = a;
	register long r1 asm("r1") = b;
	register long r2 asm("r2") = c;
	register long r3 asm("r3") = d;
	register long r4 asm("r4") = e;
	register long r5 asm("r5") = f;
	asm volatile("svc 0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2), "r"(r3),
		     "r"(r4), "r"(r5) : "memory");
	return r0;
}

static u32 hexnum(const char *s)
{
	u32 v = 0;
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		s += 2;
	for (;; s++) {
		if (*s >= '0' && *s <= '9')
			v = v * 16 + (u32)(*s - '0');
		else if ((*s | 32) >= 'a' && (*s | 32) <= 'f')
			v = v * 16 + (u32)((*s | 32) - 'a' + 10);
		else
			return v;
	}
}

static void out(const char *s)
{
	int n = 0;
	while (s[n])
		n++;
	sys3(4, 1, (long)s, n);
}

static void hex(u32 v)
{
	char b[11] = "0x";
	for (int i = 0; i < 8; i++)
		b[2 + i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15];
	b[10] = 0;
	out(b);
}

static u32 num(const char *s)
{
	u32 v = 0;
	while (*s >= '0' && *s <= '9')
		v = v * 10 + (u32)(*s++ - '0');
	return v;
}

static int fd;

static long cmd(u32 op, u32 arg, u32 flags, void *buf, u32 len, u32 *resp)
{
	struct mmc_ioc_cmd c = {0};
	c.opcode = op;
	c.arg = arg;
	c.flags = flags;
	if (buf) {
		c.blksz = len;
		c.blocks = 1;
		c.data_ptr = (u64)(unsigned long)buf;
	}
	c.cmd_timeout_ms = 2000;
	long r = sys3(54, fd, MMC_IOC_CMD, (long)&c);
	*resp = c.response[0];
	return r;
}

static void report(const char *name, long r, u32 resp)
{
	out(name);
	out(" rc=");
	hex((u32)r);
	out(" r1=");
	hex(resp);
}

static int main2(int argc, char **argv)
{
	unsigned char b[8];
	u32 resp;
	long r;

	if (argc < 4)
		return 2;
	/* mmcwp m <hexaddr> [hexval]: peek/poke a 32-bit MMIO register */
	if (argv[1][0] == 'm') {
		u32 a = hexnum(argv[2]);
		int mf = (int)sys3(5, (long)"/dev/mem", 2 | 0x101000 /* O_SYNC */, 0);
		if (mf < 0) {
			out("open /dev/mem failed\n");
			return 1;
		}
		volatile u32 *p = (volatile u32 *)sys6(192, 0, 4096, 3, 1, mf,
						       (a & ~4095u) >> 12);
		if ((unsigned long)p > 0xfffff000ul) {
			out("mmap failed\n");
			return 1;
		}
		p += (a & 4095u) / 4;
		if (argv[3][0] != '-')
			*p = hexnum(argv[3]);
		hex(a);
		out("=");
		hex(*p);
		out("\n");
		return 0;
	}
	fd = (int)sys3(5, (long)argv[1], 2 /* O_RDWR */, 0);
	if (fd < 0) {
		out("open failed\n");
		return 1;
	}
	u32 sec = num(argv[3]);
	if (argv[2][0] == 'q') {
		for (int i = 0; i < 8; i++)
			b[i] = 0;
		r = cmd(30, sec, RSP_R1 | CMD_ADTC, b, 4, &resp);
		report("CMD30", r, resp);
		out(" wp=");
		hex((u32)b[0] << 24 | (u32)b[1] << 16 | (u32)b[2] << 8 | b[3]);
		out("\n");
		for (int i = 0; i < 8; i++)
			b[i] = 0;
		r = cmd(31, sec, RSP_R1 | CMD_ADTC, b, 8, &resp);
		report("CMD31", r, resp);
		out(" type=");
		hex((u32)b[0] << 24 | (u32)b[1] << 16 | (u32)b[2] << 8 | b[3]);
		hex((u32)b[4] << 24 | (u32)b[5] << 16 | (u32)b[6] << 8 | b[7]);
		out("\n");
	} else if (argv[2][0] == 'w') {
		/* single-block CMD24 of zeros, then status; repeat n times */
		static unsigned char z[512];
		u32 n = argc > 4 ? num(argv[4]) : 1;
		for (u32 i = 0; i < n; i++) {
			struct mmc_ioc_cmd c = {0};
			c.write_flag = 1;
			c.opcode = 24;
			c.arg = sec + i;
			c.flags = RSP_R1 | CMD_ADTC;
			c.blksz = 512;
			c.blocks = 1;
			c.data_ptr = (u64)(unsigned long)z;
			c.cmd_timeout_ms = 2000;
			r = sys3(54, fd, MMC_IOC_CMD, (long)&c);
			report("CMD24", r, c.response[0]);
			r = cmd(13, 1u << 16, RSP_R1 | CMD_AC, 0, 0, &resp);
			report(" CMD13", r, resp);
			out("\n");
		}
	} else if (argv[2][0] == 'c' && argc > 4) {
		u32 n = num(argv[4]), step = 16384; /* 8 MiB group in sectors */
		for (u32 i = 0; i < n; i++) {
			r = cmd(29, sec + i * step, RSP_R1B | CMD_AC, 0, 0, &resp);
			if (r || i == 0 || i == n - 1) {
				report("CMD29", r, resp);
				out(" @");
				hex(sec + i * step);
				out("\n");
			}
		}
	}
	r = cmd(13, 1u << 16, RSP_R1 | CMD_AC, 0, 0, &resp);
	report("CMD13", r, resp);
	out("\n");
	return 0;
}

__attribute__((naked, noreturn)) void _start(void)
{
	asm volatile(
		"ldr r0, [sp]\n"
		"add r1, sp, #4\n"
		"bl start_c\n");
}

__attribute__((used, noreturn)) void start_c(int argc, char **argv)
{
	int rc = main2(argc, argv);
	sys3(1, rc, 0, 0);
	for (;;)
		;
}

void *memset(void *d, int c, unsigned int n)
{
	unsigned char *p = d;
	while (n--)
		*p++ = (unsigned char)c;
	return d;
}
