// SPDX-License-Identifier: GPL-2.0
/*
 * G4 zero-copy demo: the SGX544 renders straight into the buffers the
 * display scans out - no CPU copy.
 *
 *  - two 1280x800 BGRA buffers from the stock gralloc (ION -> ion_legacy,
 *    physically contiguous dma-bufs);
 *  - the GPU draws into them through EGLImage (EGL_NATIVE_BUFFER_ANDROID)
 *    and an FBO;
 *  - the same dma-bufs are imported into mediatek-drm (PRIME) as
 *    framebuffers and page-flipped on vblank.
 *
 * Needs DRM master: stop the desktop first (kms-demo.sh does).  Built with
 * the NDK (API 19) against the stock libEGL/libGLESv2; gralloc is reached
 * through the stock libhardware with dlopen().
 *
 * Usage: gles2_kmsdemo [seconds] [scene|heavy|light]   (default 30 scene)
 *   scene - plasma evaluated per vertex of a grid + spinning triangles
 *   heavy - the same plasma per pixel (fill-rate bound on SGX544)
 *   light - flat gradient, to separate GPU cost from display timing
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <dlfcn.h>
#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <hardware/gralloc.h>
#include <system/window.h>

#define NBUF 2

static const char fs_light_src[] =
	"precision mediump float;\n"
	"uniform float t;\n"
	"uniform vec2 aspect;\n"
	"varying vec2 uv;\n"
	"void main() {\n"
	"  gl_FragColor = vec4(0.5 + 0.5 * uv.x, fract(t * 0.25), 0.5 - 0.5 * uv.y, 1.0);\n"
	"}\n";

static const char vs_src[] =
	"attribute vec2 pos;\n"
	"varying vec2 uv;\n"
	"void main() { uv = pos; gl_Position = vec4(pos.x, -pos.y, 0.0, 1.0); }\n";
/* Full-screen animated plasma plus a spinning rainbow triangle (per pixel). */
static const char fs_src[] =
	"precision mediump float;\n"
	"uniform float t;\n"
	"uniform vec2 aspect;\n"
	"varying vec2 uv;\n"
	"void main() {\n"
	"  vec2 p = uv * aspect;\n"
	"  float v = sin(p.x * 4.0 + t) + sin(p.y * 3.0 - t * 1.3)\n"
	"          + sin((p.x + p.y) * 2.5 + t * 0.7);\n"
	"  vec3 bg = 0.35 + 0.35 * vec3(sin(v), sin(v + 2.1), sin(v + 4.2));\n"
	"  float c = cos(t), s = sin(t);\n"
	"  vec2 q = vec2(c * p.x + s * p.y, -s * p.x + c * p.y) * 1.6;\n"
	"  float e0 = q.y + 0.5, e1 = -0.866 * q.x - 0.5 * q.y + 0.5,\n"
	"        e2 = 0.866 * q.x - 0.5 * q.y + 0.5;\n"
	"  if (min(e0, min(e1, e2)) > 0.0) {\n"
	"    vec3 w = vec3(e0, e1, e2) / (e0 + e1 + e2);\n"
	"    gl_FragColor = vec4(w.yzx, 1.0);\n"
	"  } else {\n"
	"    gl_FragColor = vec4(bg, 1.0);\n"
	"  }\n"
	"}\n";

/* Scene mode: the plasma runs per vertex of a GW x GH grid (pixels only
 * interpolate), spinning vertex-coloured triangles are drawn on top. */
#define GW 64
#define GH 40
static const char vs_grid_src[] =
	"attribute vec2 pos;\n"
	"uniform float t;\n"
	"uniform vec2 aspect;\n"
	"varying vec3 col;\n"
	"void main() {\n"
	"  vec2 p = pos * aspect;\n"
	"  float v = sin(p.x * 4.0 + t) + sin(p.y * 3.0 - t * 1.3)\n"
	"          + sin((p.x + p.y) * 2.5 + t * 0.7);\n"
	"  col = 0.35 + 0.35 * vec3(sin(v), sin(v + 2.1), sin(v + 4.2));\n"
	"  gl_Position = vec4(pos.x, -pos.y, 0.0, 1.0);\n"
	"}\n";
static const char vs_tri_src[] =
	"attribute vec2 pos;\n"
	"attribute vec3 vcol;\n"
	"uniform float angle;\n"
	"uniform vec3 place;\n"
	"uniform vec2 aspect;\n"
	"varying vec3 col;\n"
	"void main() {\n"
	"  float c = cos(angle), s = sin(angle);\n"
	"  vec2 p = vec2(c * pos.x - s * pos.y, s * pos.x + c * pos.y) * place.z\n"
	"         + place.xy;\n"
	"  col = vcol;\n"
	"  gl_Position = vec4(p.x / aspect.x, -p.y, 0.0, 1.0);\n"
	"}\n";
static const char fs_col_src[] =
	"precision mediump float;\n"
	"varying vec3 col;\n"
	"void main() { gl_FragColor = vec4(col, 1.0); }\n";

static GLuint build(const char *vs, const char *fs)
{
	const char *src[2] = { vs, fs };
	GLuint prog = glCreateProgram(), sh;
	GLint ok;
	int i;

	for (i = 0; i < 2; i++) {
		sh = glCreateShader(i ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
		glShaderSource(sh, 1, &src[i], NULL);
		glCompileShader(sh);
		glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
		if (!ok) {
			char log[512];

			glGetShaderInfoLog(sh, sizeof(log), NULL, log);
			fprintf(stderr, "shader: %s\n", log);
			exit(1);
		}
		glAttachShader(prog, sh);
	}
	glBindAttribLocation(prog, 0, "pos");
	glBindAttribLocation(prog, 1, "vcol");
	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (!ok) {
		fprintf(stderr, "link failed\n");
		exit(1);
	}
	return prog;
}

enum { MODE_SCENE, MODE_HEAVY, MODE_LIGHT };
static int draw_mode;
static GLuint p_full, p_grid, p_tri;
static GLint u_full_t, u_grid_t, u_angle, u_place;
static GLfloat grid_v[(GW + 1) * (GH + 1) * 2];
static GLushort grid_i[GW * GH * 6];
static const GLfloat quad_v[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
static const GLfloat tri_v[] = { 0.0f, 1.0f, -0.866f, -0.5f, 0.866f, -0.5f };
static const GLfloat tri_c[] = { 1, 0.2f, 0.2f, 0.2f, 1, 0.2f, 0.2f, 0.3f, 1 };

static void scene_setup(float aspect)
{
	int x, y, k = 0;

	if (draw_mode != MODE_SCENE) {
		p_full = build(vs_src, draw_mode == MODE_LIGHT ? fs_light_src : fs_src);
		glUseProgram(p_full);
		u_full_t = glGetUniformLocation(p_full, "t");
		glUniform2f(glGetUniformLocation(p_full, "aspect"), aspect, 1.0f);
		return;
	}
	for (y = 0; y <= GH; y++)
		for (x = 0; x <= GW; x++) {
			grid_v[k++] = -1.0f + 2.0f * x / GW;
			grid_v[k++] = -1.0f + 2.0f * y / GH;
		}
	k = 0;
	for (y = 0; y < GH; y++)
		for (x = 0; x < GW; x++) {
			GLushort a = y * (GW + 1) + x, b = a + 1, c = a + GW + 1, d = c + 1;

			grid_i[k++] = a; grid_i[k++] = b; grid_i[k++] = c;
			grid_i[k++] = b; grid_i[k++] = d; grid_i[k++] = c;
		}
	p_grid = build(vs_grid_src, fs_col_src);
	glUseProgram(p_grid);
	u_grid_t = glGetUniformLocation(p_grid, "t");
	glUniform2f(glGetUniformLocation(p_grid, "aspect"), aspect, 1.0f);
	p_tri = build(vs_tri_src, fs_col_src);
	glUseProgram(p_tri);
	u_angle = glGetUniformLocation(p_tri, "angle");
	u_place = glGetUniformLocation(p_tri, "place");
	glUniform2f(glGetUniformLocation(p_tri, "aspect"), aspect, 1.0f);
}

static void draw(float t)
{
	int k;

	if (draw_mode != MODE_SCENE) {
		glUseProgram(p_full);
		glUniform1f(u_full_t, t);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad_v);
		glEnableVertexAttribArray(0);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		return;
	}
	glUseProgram(p_grid);
	glUniform1f(u_grid_t, t);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, grid_v);
	glEnableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	glDrawElements(GL_TRIANGLES, GW * GH * 6, GL_UNSIGNED_SHORT, grid_i);

	glUseProgram(p_tri);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri_v);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, tri_c);
	glEnableVertexAttribArray(1);
	glUniform1f(u_angle, t);
	glUniform3f(u_place, 0.0f, 0.0f, 0.55f);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	for (k = 0; k < 8; k++) {
		float a = t * 0.6f + k * 0.785398f;

		glUniform1f(u_angle, -2.0f * t + k);
		glUniform3f(u_place, 1.2f * cosf(a), 0.72f * sinf(a), 0.15f);
		glDrawArrays(GL_TRIANGLES, 0, 3);
	}
}

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void die(const char *what)
{
	perror(what);
	exit(1);
}

/* ---- DRM (raw uapi ioctls, no libdrm) ---- */

static int drm_fd = -1;
static uint32_t crtc_id, conn_id;
static struct drm_mode_modeinfo mode;

static void drm_setup(void)
{
	struct drm_mode_card_res res = { 0 };
	uint32_t conns[8], crtcs[8], encs[8], fbs[8];
	struct drm_mode_get_connector conn;
	struct drm_mode_get_encoder enc = { 0 };
	struct drm_mode_modeinfo modes[16];
	unsigned int i;

	drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (drm_fd < 0)
		die("/dev/dri/card0");
	if (ioctl(drm_fd, DRM_IOCTL_SET_MASTER, 0))
		perror("DRM_IOCTL_SET_MASTER (continuing)");

	if (ioctl(drm_fd, DRM_IOCTL_MODE_GETRESOURCES, &res))
		die("GETRESOURCES");
	if (res.count_connectors > 8 || res.count_crtcs > 8 ||
	    res.count_encoders > 8 || res.count_fbs > 8) {
		fprintf(stderr, "too many DRM objects\n");
		exit(1);
	}
	res.connector_id_ptr = (uintptr_t)conns;
	res.crtc_id_ptr = (uintptr_t)crtcs;
	res.encoder_id_ptr = (uintptr_t)encs;
	res.fb_id_ptr = (uintptr_t)fbs;
	if (ioctl(drm_fd, DRM_IOCTL_MODE_GETRESOURCES, &res))
		die("GETRESOURCES 2");

	for (i = 0; i < res.count_connectors; i++) {
		memset(&conn, 0, sizeof(conn));
		conn.connector_id = conns[i];
		if (ioctl(drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn))
			continue;
		if (conn.connection != 1 || !conn.count_modes ||
		    conn.count_modes > 16)
			continue;
		conn.count_props = 0;
		conn.count_encoders = 0;
		conn.modes_ptr = (uintptr_t)modes;
		if (ioctl(drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn))
			continue;
		conn_id = conn.connector_id;
		mode = modes[0];
		enc.encoder_id = conn.encoder_id;
		break;
	}
	if (!conn_id) {
		fprintf(stderr, "no connected DRM connector\n");
		exit(1);
	}
	if (enc.encoder_id && !ioctl(drm_fd, DRM_IOCTL_MODE_GETENCODER, &enc))
		crtc_id = enc.crtc_id;
	if (!crtc_id)
		crtc_id = crtcs[0];
	printf("DRM connector %u crtc %u mode %s %ux%u@%u\n", conn_id, crtc_id,
	       mode.name, mode.hdisplay, mode.vdisplay, mode.vrefresh);
}

static uint32_t drm_add_fb(int dmabuf_fd, int width, int height, int stride)
{
	struct drm_prime_handle prime = { .fd = dmabuf_fd };
	struct drm_mode_fb_cmd2 fb = { 0 };

	if (ioctl(drm_fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &prime))
		return 0;
	fb.width = width;
	fb.height = height;
	/* gralloc BGRA_8888 = bytes B,G,R,A = DRM ARGB8888 (little endian). */
	fb.pixel_format = DRM_FORMAT_ARGB8888;
	fb.handles[0] = prime.handle;
	fb.pitches[0] = stride * 4;
	if (ioctl(drm_fd, DRM_IOCTL_MODE_ADDFB2, &fb)) {
		perror("ADDFB2");
		return 0;
	}
	return fb.fb_id;
}

static void drm_set(uint32_t fb_id)
{
	struct drm_mode_crtc crtc = { 0 };

	crtc.crtc_id = crtc_id;
	crtc.fb_id = fb_id;
	crtc.set_connectors_ptr = (uintptr_t)&conn_id;
	crtc.count_connectors = 1;
	crtc.mode = mode;
	crtc.mode_valid = 1;
	if (ioctl(drm_fd, DRM_IOCTL_MODE_SETCRTC, &crtc))
		die("SETCRTC");
}

static void drm_flip_and_wait(uint32_t fb_id)
{
	struct drm_mode_crtc_page_flip flip = { 0 };
	struct pollfd pfd = { .fd = drm_fd, .events = POLLIN };
	char buf[256];

	flip.crtc_id = crtc_id;
	flip.fb_id = fb_id;
	flip.flags = DRM_MODE_PAGE_FLIP_EVENT;
	if (ioctl(drm_fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip)) {
		perror("PAGE_FLIP");
		return;
	}
	/* One flip-complete event per flip; give up after 100 ms. */
	if (poll(&pfd, 1, 100) > 0)
		(void)!read(drm_fd, buf, sizeof(buf));
}

/* ---- gralloc buffers wrapped as ANativeWindowBuffer for EGLImage ---- */

static void nb_ref(struct android_native_base_t *base)
{
	(void)base;
}

struct frame {
	ANativeWindowBuffer_t nb;
	buffer_handle_t handle;
	int stride;
	uint32_t fb_id;
	GLuint fbo;
};

int main(int argc, char **argv)
{
	static const EGLint cfg_attr[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8, EGL_NONE
	};
	static const EGLint pbuf_attr[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
	static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	static const EGLint img_attr[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
	const int usage = GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE |
			  GRALLOC_USAGE_HW_COMPOSER;
	double seconds = argc > 1 ? atof(argv[1]) : 30.0;
	int (*get_module)(const char *, const struct hw_module_t **);
	PFNEGLCREATEIMAGEKHRPROC create_image;
	PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC image_rb;
	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_tex;
	const struct hw_module_t *module;
	struct alloc_device_t *alloc;
	struct frame fr[NBUF];
	EGLDisplay dpy;
	EGLConfig cfg;
	EGLSurface surf;
	EGLContext ctx;
	EGLint n;
	int W, H, i, cur = 0;
	double t_render = 0, t_flip = 0, ta;
	unsigned int frames = 0, last_frames = 0;
	double t0, t, last;
	void *lh;

	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc > 2)
		draw_mode = !strcmp(argv[2], "light") ? MODE_LIGHT :
			    !strcmp(argv[2], "heavy") ? MODE_HEAVY : MODE_SCENE;
	drm_setup();
	W = mode.hdisplay;
	H = mode.vdisplay;

	lh = dlopen("libhardware.so", RTLD_NOW);
	get_module = lh ? dlsym(lh, "hw_get_module") : NULL;
	if (!get_module || get_module(GRALLOC_HARDWARE_MODULE_ID, &module) ||
	    gralloc_open(module, &alloc)) {
		fprintf(stderr, "gralloc not available\n");
		return 1;
	}

	dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (!eglInitialize(dpy, NULL, NULL) ||
	    !eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n) || n < 1)
		return 1;
	surf = eglCreatePbufferSurface(dpy, cfg, pbuf_attr);
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
		fprintf(stderr, "eglMakeCurrent failed 0x%x\n", eglGetError());
		return 1;
	}
	printf("GL renderer: %s\n", glGetString(GL_RENDERER));
	create_image = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
	image_rb = (PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC)
		eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");
	image_tex = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
		eglGetProcAddress("glEGLImageTargetTexture2DOES");

	for (i = 0; i < NBUF; i++) {
		struct frame *f = &fr[i];
		EGLImageKHR img;
		GLuint rb, tex;
		int k;

		memset(f, 0, sizeof(*f));
		if (alloc->alloc(alloc, W, H, HAL_PIXEL_FORMAT_BGRA_8888, usage,
				 &f->handle, &f->stride)) {
			fprintf(stderr, "gralloc alloc failed\n");
			return 1;
		}
		/* The IMG handle carries the ION (dma-buf) fd among its fds. */
		for (k = 0; k < f->handle->numFds && !f->fb_id; k++)
			f->fb_id = drm_add_fb(f->handle->data[k], W, H, f->stride);
		if (!f->fb_id) {
			fprintf(stderr, "buffer %d: no fd imports into DRM "
				"(not contiguous?)\n", i);
			return 1;
		}

		f->nb.common.magic = ANDROID_NATIVE_BUFFER_MAGIC;
		f->nb.common.version = sizeof(ANativeWindowBuffer_t);
		f->nb.common.incRef = nb_ref;
		f->nb.common.decRef = nb_ref;
		f->nb.width = W;
		f->nb.height = H;
		f->nb.stride = f->stride;
		f->nb.format = HAL_PIXEL_FORMAT_BGRA_8888;
		f->nb.usage = usage;
		f->nb.handle = f->handle;
		img = create_image(dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
				   (EGLClientBuffer)&f->nb, img_attr);
		if (img == EGL_NO_IMAGE_KHR) {
			fprintf(stderr, "eglCreateImageKHR failed 0x%x\n", eglGetError());
			return 1;
		}
		glGenFramebuffers(1, &f->fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, f->fbo);
		glGenRenderbuffers(1, &rb);
		glBindRenderbuffer(GL_RENDERBUFFER, rb);
		if (image_rb)
			image_rb(GL_RENDERBUFFER, img);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					  GL_RENDERBUFFER, rb);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			/* Fall back to a texture target for the EGLImage. */
			glGenTextures(1, &tex);
			glBindTexture(GL_TEXTURE_2D, tex);
			image_tex(GL_TEXTURE_2D, img);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
					       GL_TEXTURE_2D, tex, 0);
		}
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			fprintf(stderr, "FBO on EGLImage incomplete\n");
			return 1;
		}
		printf("buffer %d: stride %d, fb %u\n", i, f->stride, f->fb_id);
	}

	scene_setup((float)W / H);
	glViewport(0, 0, W, H);

	/* Show buffer 0 first, then render into the other one and flip. */
	glBindFramebuffer(GL_FRAMEBUFFER, fr[0].fbo);
	draw(0.0f);
	glFinish();
	drm_set(fr[0].fb_id);

	t0 = last = now();
	while ((t = now() - t0) < seconds) {
		int back = cur ^ 1;

		glBindFramebuffer(GL_FRAMEBUFFER, fr[back].fbo);
		ta = now();
		draw((float)t);
		glFinish();
		t_render += now() - ta;
		ta = now();
		drm_flip_and_wait(fr[back].fb_id);
		t_flip += now() - ta;
		cur = back;
		frames++;
		if (now() - last >= 2.0) {
			unsigned int d = frames - last_frames;

			printf("%.1f fps  render %.1f ms  flip wait %.1f ms\n",
			       d / (now() - last), t_render * 1e3 / d,
			       t_flip * 1e3 / d);
			last = now();
			last_frames = frames;
			t_render = t_flip = 0;
		}
	}
	printf("%u frames in %.1f s: %.1f fps, zero-copy (GPU -> scanout)\n",
	       frames, t, frames / t);
	return 0;
}
