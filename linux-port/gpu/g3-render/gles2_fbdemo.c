// SPDX-License-Identifier: GPL-2.0
/*
 * G3 visual demo: the SGX544 renders a spinning rainbow triangle into an
 * EGL pbuffer; every frame is read back and copied (scaled x2) to the Linux
 * framebuffer, so the GPU output is visible without any window system.
 * The desktop must be stopped while it runs (it owns the display).
 *
 * Usage: gles2_fbdemo [seconds] [fbdev]   (default 20 s, /dev/fb0)
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define W 640
#define H 400

static const char vs_src[] =
	"attribute vec2 pos;\n"
	"attribute vec3 col;\n"
	"uniform float angle;\n"
	"uniform vec2 aspect;\n"
	"varying vec3 v_col;\n"
	"void main() {\n"
	"  float c = cos(angle), s = sin(angle);\n"
	"  vec2 p = vec2(c * pos.x - s * pos.y, s * pos.x + c * pos.y);\n"
	"  gl_Position = vec4(p * aspect, 0.0, 1.0);\n"
	"  v_col = col;\n"
	"}\n";
static const char fs_src[] =
	"precision mediump float;\n"
	"varying vec3 v_col;\n"
	"void main() { gl_FragColor = vec4(v_col, 1.0); }\n";

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
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
		fprintf(stderr, "shader: %s\n", log);
		exit(1);
	}
	return sh;
}

int main(int argc, char **argv)
{
	static const EGLint cfg_attr[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8, EGL_NONE
	};
	static const EGLint pbuf_attr[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
	static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	static const GLfloat tri[] = {
		0.0f, 0.9f, 1, 0, 0,
		-0.78f, -0.45f, 0, 1, 0,
		0.78f, -0.45f, 0, 0, 1,
	};
	static uint32_t pixels[W * H];
	double seconds = argc > 1 ? atof(argv[1]) : 20.0;
	const char *fbdev = argc > 2 ? argv[2] : "/dev/fb0";
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	EGLDisplay dpy;
	EGLConfig cfg;
	EGLSurface surf;
	EGLContext ctx;
	EGLint n;
	GLuint prog;
	GLint u_angle, u_aspect;
	unsigned char *fb;
	unsigned int ox, oy, frames = 0;
	double t0, t;
	int fd;

	fd = open(fbdev, O_RDWR);
	if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &var) ||
	    ioctl(fd, FBIOGET_FSCREENINFO, &fix)) {
		perror(fbdev);
		return 1;
	}
	if ((var.bits_per_pixel != 16 && var.bits_per_pixel != 32) ||
	    var.xres < 2 * W || var.yres < 2 * H) {
		fprintf(stderr, "unsupported fb %ux%u %ubpp\n", var.xres,
			var.yres, var.bits_per_pixel);
		return 1;
	}
	fb = mmap(NULL, fix.line_length * var.yres, PROT_READ | PROT_WRITE,
		  MAP_SHARED, fd, 0);
	if (fb == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	ox = (var.xres - 2 * W) / 2;
	oy = (var.yres - 2 * H) / 2;
	memset(fb, 0, fix.line_length * var.yres);
	printf("fb %ux%u %ubpp stride %u\n", var.xres, var.yres,
	       var.bits_per_pixel, fix.line_length);

	dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (!eglInitialize(dpy, NULL, NULL) ||
	    !eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n) || n < 1) {
		fprintf(stderr, "EGL init/config failed 0x%x\n", eglGetError());
		return 1;
	}
	surf = eglCreatePbufferSurface(dpy, cfg, pbuf_attr);
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	if (surf == EGL_NO_SURFACE || ctx == EGL_NO_CONTEXT ||
	    !eglMakeCurrent(dpy, surf, surf, ctx)) {
		fprintf(stderr, "EGL surface/context failed 0x%x\n", eglGetError());
		return 1;
	}
	printf("GL renderer: %s\n", glGetString(GL_RENDERER));

	prog = glCreateProgram();
	glAttachShader(prog, compile(GL_VERTEX_SHADER, vs_src));
	glAttachShader(prog, compile(GL_FRAGMENT_SHADER, fs_src));
	glBindAttribLocation(prog, 0, "pos");
	glBindAttribLocation(prog, 1, "col");
	glLinkProgram(prog);
	glUseProgram(prog);
	u_angle = glGetUniformLocation(prog, "angle");
	u_aspect = glGetUniformLocation(prog, "aspect");
	glUniform2f(u_aspect, (float)H / W, 1.0f);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), tri);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), tri + 2);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glViewport(0, 0, W, H);

	t0 = now();
	while ((t = now() - t0) < seconds) {
		unsigned int x, y;

		glClearColor(0.05f, 0.05f, 0.15f + 0.1f * (float)sin(t), 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glUniform1f(u_angle, (float)t * 1.5f);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

		/* GL rows are bottom-up; scale x2 into the framebuffer. */
		for (y = 0; y < H; y++) {
			const uint32_t *src = &pixels[(H - 1 - y) * W];
			unsigned char *d0 = fb + (oy + 2 * y) * fix.line_length;
			unsigned char *d1 = d0 + fix.line_length;

			for (x = 0; x < W; x++) {
				uint32_t p = src[x];
				unsigned int r = p & 0xff, g = (p >> 8) & 0xff,
					     b = (p >> 16) & 0xff;

				if (var.bits_per_pixel == 16) {
					uint16_t v = (r >> 3) << 11 | (g >> 2) << 5 | b >> 3;
					uint16_t *a = (uint16_t *)d0 + ox + 2 * x;
					uint16_t *c = (uint16_t *)d1 + ox + 2 * x;

					a[0] = a[1] = c[0] = c[1] = v;
				} else {
					uint32_t v = r << 16 | g << 8 | b;
					uint32_t *a = (uint32_t *)d0 + ox + 2 * x;
					uint32_t *c = (uint32_t *)d1 + ox + 2 * x;

					a[0] = a[1] = c[0] = c[1] = v;
				}
			}
		}
		frames++;
	}
	printf("%u frames in %.1f s: %.1f fps (GPU render + readback + copy)\n",
	       frames, t, frames / t);

	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(dpy, ctx);
	eglDestroySurface(dpy, surf);
	eglTerminate(dpy);
	munmap(fb, fix.line_length * var.yres);
	close(fd);
	return 0;
}
