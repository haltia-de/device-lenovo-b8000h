// SPDX-License-Identifier: GPL-2.0
/*
 * G5 step 4: what does it cost the compositor to get a client's shm frame
 * onto the SGX544?  Compares, for a 1280x800 BGRA frame:
 *   teximage  - glTexImage2D of the whole frame (a new texture's upload)
 *   subimage  - glTexSubImage2D of the whole frame into an existing texture
 *   band100   - glTexSubImage2D of 100 full rows (typical partial damage)
 *   gralloc   - memcpy into a locked linear gralloc buffer that the GPU
 *               samples through an EGLImage (EGL_HYBRIS_native_buffer)
 *   memcpy    - plain memcpy of the frame (baseline)
 * Each iteration also draws the texture into a small FBO and glFinish()es,
 * so deferred uploads (twiddling) are counted.
 *
 * Build (Debian chroot): gcc -O2 -o tex_bench tex_bench.c \
 *   -I/usr/local/hybris/include -L/usr/local/hybris/lib \
 *   -Wl,-rpath,/usr/local/hybris/lib -lEGL -lGLESv2
 * Run with LD_PRELOAD=/usr/local/hybris/lib/libtlspad.so (see tlspad.c).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#define W 1280
#define H 800
#define N 20

typedef EGLBoolean (*create_nb_t)(EGLint, EGLint, EGLint, EGLint, EGLint *,
				  EGLClientBuffer *);
typedef EGLBoolean (*lock_nb_t)(EGLClientBuffer, EGLint, EGLint, EGLint,
				EGLint, EGLint, void **);
typedef EGLBoolean (*unlock_nb_t)(EGLClientBuffer);

static GLuint prog, fbo;

static double now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static GLuint shader(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	GLint ok = 0;

	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		fprintf(stderr, "shader compile failed\n");
		exit(1);
	}
	return s;
}

static void setup_draw(void)
{
	static const char *vs =
		"attribute vec2 pos; varying vec2 uv;\n"
		"void main() { uv = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0.0, 1.0); }\n";
	static const char *fs =
		"precision mediump float; varying vec2 uv; uniform sampler2D tex;\n"
		"void main() { gl_FragColor = texture2D(tex, uv); }\n";
	static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	GLuint tex;

	prog = glCreateProgram();
	glAttachShader(prog, shader(GL_VERTEX_SHADER, vs));
	glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs));
	glBindAttribLocation(prog, 0, "pos");
	glLinkProgram(prog);
	glUseProgram(prog);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
	glEnableVertexAttribArray(0);

	/* A 256x160 target: sampling the whole texture, cheap to fill. */
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 160, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, tex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		fprintf(stderr, "FBO incomplete\n");
		exit(1);
	}
	glViewport(0, 0, 256, 160);
}

static void draw(GLuint tex)
{
	glBindTexture(GL_TEXTURE_2D, tex);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glFinish();
}

static GLuint new_tex(void)
{
	GLuint t;

	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return t;
}

static void report(const char *name, double total)
{
	printf("%-9s %6.2f ms/frame\n", name, total / N);
}

int main(void)
{
	static const EGLint cfg_attr[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8, EGL_NONE
	};
	static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	static const EGLint img_attr[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
	/* SW_WRITE_OFTEN | HW_TEXTURE */
	const EGLint usage = 0x30 | 0x100;
	PFNEGLCREATEIMAGEKHRPROC create_image;
	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_tex;
	create_nb_t create_nb;
	lock_nb_t lock_nb;
	unlock_nb_t unlock_nb;
	EGLClientBuffer nb;
	EGLDisplay dpy;
	EGLConfig cfg;
	EGLContext ctx;
	EGLImageKHR img;
	EGLint n, stride;
	unsigned char *frame, *dst;
	double t0, t;
	GLuint tex;
	void *vaddr;
	int i, y;

	setvbuf(stdout, NULL, _IONBF, 0);
	dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (!eglInitialize(dpy, NULL, NULL) ||
	    !eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n) || n < 1)
		return 1;
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
		return 1;
	setup_draw();

	frame = malloc(W * H * 4);
	for (i = 0; i < W * H * 4; i++)
		frame[i] = (unsigned char)(i * 7);
	dst = malloc(W * H * 4);

	t0 = now_ms();
	for (i = 0; i < N; i++)
		memcpy(dst, frame, W * H * 4);
	report("memcpy", now_ms() - t0);

	t = 0;
	for (i = 0; i < N; i++) {
		tex = new_tex();
		t0 = now_ms();
		glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT, W, H, 0, GL_BGRA_EXT,
			     GL_UNSIGNED_BYTE, frame);
		draw(tex);
		t += now_ms() - t0;
		glDeleteTextures(1, &tex);
	}
	report("teximage", t);

	tex = new_tex();
	glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT, W, H, 0, GL_BGRA_EXT,
		     GL_UNSIGNED_BYTE, frame);
	draw(tex);
	t0 = now_ms();
	for (i = 0; i < N; i++) {
		frame[i] ^= 0xff;
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_BGRA_EXT,
				GL_UNSIGNED_BYTE, frame);
		draw(tex);
	}
	report("subimage", now_ms() - t0);

	t0 = now_ms();
	for (i = 0; i < N; i++) {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 300, W, 100, GL_BGRA_EXT,
				GL_UNSIGNED_BYTE, frame + 300 * W * 4);
		draw(tex);
	}
	report("band100", now_ms() - t0);

	create_nb = (create_nb_t)eglGetProcAddress("eglHybrisCreateNativeBuffer");
	lock_nb = (lock_nb_t)eglGetProcAddress("eglHybrisLockNativeBuffer");
	unlock_nb = (unlock_nb_t)eglGetProcAddress("eglHybrisUnlockNativeBuffer");
	create_image = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
	image_tex = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
		eglGetProcAddress("glEGLImageTargetTexture2DOES");
	if (!create_nb || !lock_nb || !unlock_nb ||
	    !create_nb(W, H, usage, 5 /* BGRA_8888 */, &stride, &nb)) {
		printf("gralloc   unavailable\n");
		return 0;
	}
	img = create_image(dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, nb,
			   img_attr);
	if (img == EGL_NO_IMAGE_KHR) {
		printf("gralloc   eglCreateImageKHR failed 0x%x\n", eglGetError());
		return 0;
	}
	tex = new_tex();
	image_tex(GL_TEXTURE_2D, img);
	printf("gralloc   stride %d px, GL error 0x%x\n", stride, glGetError());

	t0 = now_ms();
	for (i = 0; i < N; i++) {
		if (!lock_nb(nb, usage, 0, 0, W, H, &vaddr)) {
			printf("gralloc   lock failed\n");
			return 0;
		}
		for (y = 0; y < H; y++)
			memcpy((unsigned char *)vaddr + y * stride * 4,
			       frame + y * W * 4, W * 4);
		unlock_nb(nb);
		draw(tex);
	}
	report("gralloc", now_ms() - t0);

	t0 = now_ms();
	for (i = 0; i < N; i++) {
		if (!lock_nb(nb, usage, 0, 300, W, 100, &vaddr))
			return 0;
		for (y = 300; y < 400; y++)
			memcpy((unsigned char *)vaddr + y * stride * 4,
			       frame + y * W * 4, W * 4);
		unlock_nb(nb);
		draw(tex);
	}
	report("gr-band", now_ms() - t0);

	/*
	 * Does the GPU see CPU writes into the same gralloc buffer without
	 * re-binding the EGLImage?  Fill with one colour, draw, read back;
	 * then another colour.  BGRA memory 10 20 30 ff reads back as RGBA
	 * 30 20 10 ff.
	 */
	{
		static const unsigned char colours[2][4] = {
			{ 0x10, 0x20, 0x30, 0xff }, { 0x40, 0x50, 0x60, 0xff },
		};
		unsigned char px[4];
		int c, x;

		for (c = 0; c < 2; c++) {
			if (!lock_nb(nb, usage, 0, 0, W, H, &vaddr))
				return 0;
			for (y = 0; y < H; y++)
				for (x = 0; x < W; x++)
					memcpy((unsigned char *)vaddr +
					       (y * stride + x) * 4, colours[c], 4);
			unlock_nb(nb);
			draw(tex);
			glReadPixels(128, 80, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
			printf("coherent  fill %d -> %02x %02x %02x %02x (%s)\n", c,
			       px[0], px[1], px[2], px[3],
			       px[0] == colours[c][2] && px[2] == colours[c][0] ?
			       "fresh" : "STALE");
		}
	}

	t0 = now_ms();
	for (i = 0; i < N; i++) {
		glBindTexture(GL_TEXTURE_2D, tex);
		image_tex(GL_TEXTURE_2D, img);
		draw(tex);
	}
	report("rebind", now_ms() - t0);

	/*
	 * GL_IMG_read_format: glReadPixels in GL_BGRA (0x80E1, same value as
	 * GL_BGRA_EXT) - what wlroots screencopy of ARGB8888 needs.  The FBO
	 * holds BGRA memory 40 50 60 ff from the last fill.
	 */
	{
		unsigned char px[4] = { 0 };
		GLint fmt = 0, type = 0;

		glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &fmt);
		glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE, &type);
		while (glGetError() != GL_NO_ERROR)
			;
		glReadPixels(128, 80, 1, 1, GL_BGRA_EXT, GL_UNSIGNED_BYTE, px);
		printf("bgra-read impl 0x%x/0x%x, error 0x%x -> %02x %02x %02x %02x\n",
		       fmt, type, glGetError(), px[0], px[1], px[2], px[3]);
	}
	return 0;
}
