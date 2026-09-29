// SPDX-License-Identifier: GPL-2.0
/*
 * G3 benchmark: how fast the SGX544 renders, separately from getting the
 * pixels back to the CPU.  Full-panel 1280x800 pbuffer, a per-pixel "plasma"
 * fragment shader (sin/cos per pixel), glFinish()-bounded timing; then the
 * cost of one full-frame glReadPixels.  No display involved.
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define W 1280
#define H 800

static const char vs_src[] =
	"attribute vec2 pos;\n"
	"varying vec2 uv;\n"
	"void main() { uv = pos; gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char fs_src[] =
	"precision mediump float;\n"
	"uniform float t;\n"
	"varying vec2 uv;\n"
	"void main() {\n"
	"  float v = sin(uv.x * 10.0 + t) + sin(uv.y * 8.0 - t)\n"
	"          + sin((uv.x + uv.y) * 6.0 + t * 0.7);\n"
	"  gl_FragColor = vec4(0.5 + 0.5 * sin(v), 0.5 + 0.5 * sin(v + 2.1),\n"
	"                      0.5 + 0.5 * sin(v + 4.2), 1.0);\n"
	"}\n";

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

int main(void)
{
	static const EGLint cfg_attr[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8, EGL_NONE
	};
	static const EGLint pbuf_attr[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
	static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	static unsigned char pixels[W * H * 4];
	EGLDisplay dpy;
	EGLConfig cfg;
	EGLSurface surf;
	EGLContext ctx;
	EGLint n;
	GLuint prog;
	GLint u_t;
	int frames = 0, i;
	double t0, t, tr;

	setvbuf(stdout, NULL, _IONBF, 0);
	dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (!eglInitialize(dpy, NULL, NULL) ||
	    !eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n) || n < 1)
		return 1;
	surf = eglCreatePbufferSurface(dpy, cfg, pbuf_attr);
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	if (!eglMakeCurrent(dpy, surf, surf, ctx))
		return 1;
	printf("GL renderer: %s\n", glGetString(GL_RENDERER));

	prog = glCreateProgram();
	glAttachShader(prog, compile(GL_VERTEX_SHADER, vs_src));
	glAttachShader(prog, compile(GL_FRAGMENT_SHADER, fs_src));
	glBindAttribLocation(prog, 0, "pos");
	glLinkProgram(prog);
	glUseProgram(prog);
	u_t = glGetUniformLocation(prog, "t");
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
	glEnableVertexAttribArray(0);
	glViewport(0, 0, W, H);

	/* Warm up (shader upload, first kick), then time 5 seconds. */
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glFinish();
	t0 = now();
	while ((t = now() - t0) < 5.0) {
		for (i = 0; i < 10; i++) {
			glUniform1f(u_t, (float)(t + i * 0.01));
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
			frames++;
		}
		glFinish();
	}
	t = now() - t0;
	printf("GPU render: %d frames %dx%d in %.2f s = %.1f fps, %.0f Mpix/s "
	       "(4 sin/cos per pixel)\n", frames, W, H, t, frames / t,
	       frames * (double)W * H / t / 1e6);

	t0 = now();
	for (i = 0; i < 10; i++)
		glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	tr = (now() - t0) / 10;
	printf("glReadPixels %dx%d RGBA: %.1f ms per frame (CPU readback)\n",
	       W, H, tr * 1e3);
	printf("sample pixel: %u,%u,%u\n", pixels[0], pixels[1], pixels[2]);

	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglTerminate(dpy);
	return 0;
}
