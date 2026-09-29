// SPDX-License-Identifier: GPL-2.0
/*
 * G3: minimal offscreen render through the stock PowerVR SGX544 GLES2 stack.
 *
 * EGL pbuffer 64x64, clear to blue, draw a red triangle, glFinish,
 * glReadPixels and check two pixels: one inside the triangle must be red,
 * one in the corner must stay blue.  Exit 0 only when both match, so a
 * software fallback or a silent no-op cannot pass.  With an argument, the
 * frame is also written there as a binary PPM.
 *
 * Built with the Android NDK (API 19) against the platform libEGL/libGLESv2;
 * run on the tablet through linux-port/gpu/pvr-run.sh.
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ucontext.h>
#include <unistd.h>

#define W 64
#define H 64

#define STAGE(s) fprintf(stderr, "stage: %s\n", s)

#ifdef HYBRIS_TLS_PAD
/*
 * libhybris on glibc/ARM: the Android 4.4 EGL/GLES code writes its TLS slots
 * (TLS_SLOT_OPENGL/_API = tp+12..tp+19) straight through TPIDRURO.  With
 * glibc the executable's static TLS block starts right after the 8-byte TCB,
 * so this pad absorbs those writes instead of libc's own thread data.
 */
__thread void *bionic_tls_pad[8];
#endif

/* Print one /proc/self/maps line that contains addr (library + offset). */
static void print_mapping(const char *what, unsigned long addr)
{
	FILE *f = fopen("/proc/self/maps", "r");
	char line[512];
	unsigned long lo, hi, off;

	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%lx-%lx %*s %lx", &lo, &hi, &off) == 3 &&
		    addr >= lo && addr < hi) {
			fprintf(stderr, "%s=%08lx (+%lx in) %s", what, addr,
				addr - lo + off, strchr(line, '/') ?
				strchr(line, '/') : "anon\n");
			break;
		}
	}
	fclose(f);
}

/* No debuggerd here: report where a crash happened, then exit. */
static void crash_handler(int sig, siginfo_t *info, void *uctx)
{
	ucontext_t *uc = uctx;

	fprintf(stderr, "CRASH signal %d at addr %p\n", sig, info->si_addr);
	print_mapping("pc", uc->uc_mcontext.arm_pc);
	print_mapping("lr", uc->uc_mcontext.arm_lr);
	_exit(3);
}

static const char vs_src[] =
	"attribute vec2 pos;\n"
	"void main() { gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char fs_src[] =
	"precision mediump float;\n"
	"void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";

static int egl_fail(const char *what)
{
	fprintf(stderr, "FAIL %s: EGL error 0x%04x\n", what, eglGetError());
	return 1;
}

static GLuint compile(GLenum type, const char *src)
{
	GLuint sh = glCreateShader(type);
	GLint ok = 0;
	char log[512];

	glShaderSource(sh, 1, &src, NULL);
	glCompileShader(sh);
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		glGetShaderInfoLog(sh, sizeof(log), NULL, log);
		fprintf(stderr, "FAIL compile %s shader: %s\n",
			type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
		return 0;
	}
	return sh;
}

static int is_color(const unsigned char *px, int r, int g, int b)
{
	/* Loose match: RGB565 configs round 255 to 248/252. */
	return (px[0] > 200) == r && (px[1] > 200) == g && (px[2] > 200) == b &&
	       (r || px[0] < 50) && (g || px[1] < 50) && (b || px[2] < 50);
}

int main(int argc, char **argv)
{
	static const EGLint cfg_8888[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_NONE
	};
	static const EGLint cfg_565[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_BLUE_SIZE, 5,
		EGL_NONE
	};
	static const EGLint pbuf_attr[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
	static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	static const GLfloat tri[] = { -0.8f, -0.8f, 0.8f, -0.8f, 0.0f, 0.8f };
	static unsigned char pixels[W * H * 4];
	EGLDisplay dpy;
	EGLConfig cfg;
	EGLSurface surf;
	EGLContext ctx;
	EGLint major, minor, n = 0, r, g, b, a;
	GLuint vs, fs, prog;
	GLint linked = 0;
	const unsigned char *inside, *corner;
	struct sigaction sa;
	int ok;

	setvbuf(stdout, NULL, _IONBF, 0);
#ifdef HYBRIS_TLS_PAD
	fprintf(stderr, "tls pad at %p\n", (void *)bionic_tls_pad);
#endif
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = crash_handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);
	sigaction(SIGILL, &sa, NULL);

	STAGE("eglInitialize");
	dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (dpy == EGL_NO_DISPLAY)
		return egl_fail("eglGetDisplay");
	if (!eglInitialize(dpy, &major, &minor))
		return egl_fail("eglInitialize");
	printf("EGL %d.%d vendor=\"%s\" version=\"%s\"\n", major, minor,
	       eglQueryString(dpy, EGL_VENDOR), eglQueryString(dpy, EGL_VERSION));

	STAGE("eglChooseConfig");
	if (!eglChooseConfig(dpy, cfg_8888, &cfg, 1, &n) || n < 1) {
		if (!eglChooseConfig(dpy, cfg_565, &cfg, 1, &n) || n < 1)
			return egl_fail("eglChooseConfig");
	}
	eglGetConfigAttrib(dpy, cfg, EGL_RED_SIZE, &r);
	eglGetConfigAttrib(dpy, cfg, EGL_GREEN_SIZE, &g);
	eglGetConfigAttrib(dpy, cfg, EGL_BLUE_SIZE, &b);
	eglGetConfigAttrib(dpy, cfg, EGL_ALPHA_SIZE, &a);
	printf("config RGBA %d%d%d%d\n", r, g, b, a);

	STAGE("eglCreatePbufferSurface");
	surf = eglCreatePbufferSurface(dpy, cfg, pbuf_attr);
	if (surf == EGL_NO_SURFACE)
		return egl_fail("eglCreatePbufferSurface");
	STAGE("eglCreateContext");
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	if (ctx == EGL_NO_CONTEXT)
		return egl_fail("eglCreateContext");
	STAGE("eglMakeCurrent");
	if (!eglMakeCurrent(dpy, surf, surf, ctx))
		return egl_fail("eglMakeCurrent");

	printf("GL vendor=\"%s\" renderer=\"%s\" version=\"%s\"\n",
	       glGetString(GL_VENDOR), glGetString(GL_RENDERER),
	       glGetString(GL_VERSION));

	STAGE("compile shaders");
	vs = compile(GL_VERTEX_SHADER, vs_src);
	fs = compile(GL_FRAGMENT_SHADER, fs_src);
	if (!vs || !fs)
		return 1;
	STAGE("link program");
	prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glBindAttribLocation(prog, 0, "pos");
	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &linked);
	if (!linked) {
		fprintf(stderr, "FAIL glLinkProgram\n");
		return 1;
	}

	STAGE("draw");
	glViewport(0, 0, W, H);
	glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glUseProgram(prog);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri);
	glEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glFinish();

	STAGE("glReadPixels");
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	if (glGetError() != GL_NO_ERROR) {
		fprintf(stderr, "FAIL glReadPixels: GL error\n");
		return 1;
	}

	/* Rows are bottom-up; the triangle spans y in [-0.8, 0.8]. */
	inside = &pixels[(H / 2 * W + W / 2) * 4];
	corner = &pixels[(2 * W + 2) * 4];
	printf("pixel inside=(%u,%u,%u,%u) corner=(%u,%u,%u,%u)\n",
	       inside[0], inside[1], inside[2], inside[3],
	       corner[0], corner[1], corner[2], corner[3]);
	ok = is_color(inside, 1, 0, 0) && is_color(corner, 0, 0, 1);

	if (argc > 1) {
		FILE *f = fopen(argv[1], "wb");
		int y, x;

		if (f) {
			fprintf(f, "P6\n%d %d\n255\n", W, H);
			for (y = H - 1; y >= 0; y--)
				for (x = 0; x < W; x++)
					fwrite(&pixels[(y * W + x) * 4], 1, 3, f);
			fclose(f);
		}
	}

	STAGE("teardown");
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(dpy, ctx);
	eglDestroySurface(dpy, surf);
	eglTerminate(dpy);

	printf("%s\n", ok ? "RENDER OK" : "RENDER MISMATCH");
	return ok ? 0 : 2;
}
