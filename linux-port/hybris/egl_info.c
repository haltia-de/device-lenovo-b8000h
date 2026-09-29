// SPDX-License-Identifier: GPL-2.0
/*
 * Print what the stock PowerVR EGL/GLES (through libhybris) offers, for the
 * wlroots/phoc port: EGL and GL extension lists, limits, surfaceless
 * contexts.  Also compiles a shader with a float literal, which crashes when
 * the bionic GL TLS slots overwrite glibc's thread data (see tlspad.c).
 *
 * Build (Debian chroot): gcc -O2 -o egl_info egl_info.c \
 *   -I/usr/local/hybris/include -L/usr/local/hybris/lib \
 *   -Wl,-rpath,/usr/local/hybris/lib -lEGL -lGLESv2
 */
#include <stdio.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

static void print_list(const char *title, const char *s)
{
	const char *p = s;

	printf("%s:\n", title);
	while (p && *p) {
		size_t n = strcspn(p, " ");

		if (n)
			printf("  %.*s\n", (int)n, p);
		p += n;
		p += strspn(p, " ");
	}
}

int main(void)
{
	static const EGLint cfg_attr[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8, EGL_NONE
	};
	static const EGLint pbuf_attr[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
	static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	static const char *fs_src =
		"precision mediump float;\n"
		"uniform float k;\n"
		"void main() { gl_FragColor = vec4(0.25 * k, 0.5, 0.75, 1.0); }\n";
	EGLDisplay dpy;
	EGLConfig cfg;
	EGLSurface surf;
	EGLContext ctx;
	EGLint n, maj, min;
	GLint v[2], ok = 0;
	GLuint sh;

	setvbuf(stdout, NULL, _IONBF, 0);
	dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (!eglInitialize(dpy, &maj, &min)) {
		printf("eglInitialize failed 0x%x\n", eglGetError());
		return 1;
	}
	printf("EGL %d.%d vendor \"%s\" version \"%s\" apis \"%s\"\n", maj, min,
	       eglQueryString(dpy, EGL_VENDOR), eglQueryString(dpy, EGL_VERSION),
	       eglQueryString(dpy, EGL_CLIENT_APIS));
	print_list("EGL extensions", eglQueryString(dpy, EGL_EXTENSIONS));
	print_list("EGL client extensions",
		   eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS));

	if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n) || n < 1) {
		printf("eglChooseConfig failed 0x%x\n", eglGetError());
		return 1;
	}
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	if (ctx == EGL_NO_CONTEXT) {
		printf("eglCreateContext failed 0x%x\n", eglGetError());
		return 1;
	}
	if (eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
		printf("surfaceless make-current: OK\n");
	} else {
		printf("surfaceless make-current: failed 0x%x, using pbuffer\n",
		       eglGetError());
		surf = eglCreatePbufferSurface(dpy, cfg, pbuf_attr);
		if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
			printf("eglMakeCurrent failed 0x%x\n", eglGetError());
			return 1;
		}
	}

	printf("GL vendor \"%s\" renderer \"%s\"\nGL version \"%s\" GLSL \"%s\"\n",
	       glGetString(GL_VENDOR), glGetString(GL_RENDERER),
	       glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
	print_list("GL extensions", (const char *)glGetString(GL_EXTENSIONS));
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, v);
	printf("GL_MAX_TEXTURE_SIZE %d\n", v[0]);
	glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, v);
	printf("GL_MAX_RENDERBUFFER_SIZE %d\n", v[0]);
	glGetIntegerv(GL_MAX_VIEWPORT_DIMS, v);
	printf("GL_MAX_VIEWPORT_DIMS %dx%d\n", v[0], v[1]);

	sh = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(sh, 1, &fs_src, NULL);
	glCompileShader(sh);
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	printf("shader compile: %s\n", ok ? "OK" : "FAILED");
	return ok ? 0 : 1;
}
