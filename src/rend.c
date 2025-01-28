#include <stdio.h>
#include <assert.h>
#include "rend.h"
#include "scene.h"
#include "dynarr.h"
#include "tpool.h"

#ifdef USE_THREADS
#define BLKSZ	16

struct block {
	int x, y, width, height;
};

static struct thread_pool *tpool;
static struct block *blklist;
static int numblk;
static int num_proc;
#endif


#undef DBG_SDR_NORMALS

int dbgpixel;
extern int opt_verbose;

struct rendimage *rendfb;

static struct scene *scn;

static struct rect vp;
static float aspect;
static float vpdist, raymag, orthosz, zmin, zmax;
static float view_xform[16];
static int max_iter = 5;
static void (*primray)(struct ray*, int, int, int);
static int num_samples = 1;
static float sample_scale = 1.0f;
static cgm_vec2 *samples;
static unsigned int optbits;


static int raytrace(cgm_vec4 *color, struct ray *ray, struct scene *scn, int iter);
static void shade(cgm_vec4 *color, struct scene *scn, struct rayhit *hit, int iter);
static void texlookup(cgm_vec4 *color, struct rendimage *img, cgm_vec2 uv, cgm_vec2 duv);
static void calc_invdir(struct ray *ray);
static void perspray(struct ray *ray, int x, int y, int n);
static void orthoray(struct ray *ray, int x, int y, int n);
static void calc_sample_pos(int sidx, cgm_vec2 *pos);


int rend_init(void)
{
	rend_perspective(cgm_deg_to_rad(50), 1000.0f);
	cgm_midentity(view_xform);
	rend_samples(1);

#ifdef USE_THREADS
	num_proc = tpool_num_processors();
	if(opt_verbose) {
		printf("creating thread pool (%d processors detected)\n", num_proc);
	}
	if(!(tpool = tpool_create(0))) {
		fprintf(stderr, "failed to create thread pool\n");
		return -1;
	}
#endif
	return 0;
}

void rend_cleanup(void)
{
	free(samples);
	samples = 0;

#ifdef USE_THREADS
	tpool_destroy(tpool);
#endif
}

void rend_enable(unsigned int opt)
{
	optbits |= 1 << opt;
}

void rend_disable(unsigned int opt)
{
	optbits &= ~(1 << opt);
}

void rend_viewport(int x, int y, int width, int height)
{
#ifdef USE_THREADS
	if(width != vp.width || height != vp.height) {
		free(blklist);
		blklist = 0;
	}
#endif

	vp.x = x;
	vp.y = y;
	vp.width = width;
	vp.height = height;

	aspect = (float)width / (float)height;
}

void rend_perspective(float fov, float zfar)
{
	primray = perspray;
	vpdist = 1.0f / tan(fov * 0.5f);
	raymag = zfar;
}

void rend_ortho(float ysz, float z0, float z1)
{
	zmin = z0;
	zmax = z1;
	orthosz = ysz;
	primray = orthoray;
}

void rend_view(float *xform)
{
	memcpy(view_xform, xform, sizeof view_xform);
}

void rend_samples(int n)
{
	if(n <= 0) return;

	num_samples = n;
	sample_scale = 1.0f / (float)n;
	free(samples);
	samples = 0;
}

static int prepare_render(void)
{
	int i;

	if(!samples) {
		if(!(samples = malloc(num_samples * sizeof *samples))) {
			fprintf(stderr, "failed to allocate subpixels sample positions\n");
			return -1;
		}
		for(i=0; i<num_samples; i++) {
			calc_sample_pos(i, samples + i);
		}
	}

#ifdef USE_THREADS
	if(!blklist && num_proc > 1) {
		int j, xblk, yblk;
		struct block *blk;

		xblk = (vp.width + BLKSZ - 1) / BLKSZ;
		yblk = (vp.height + BLKSZ - 1) / BLKSZ;
		numblk = xblk * yblk;
		if(!(blklist = malloc(numblk * sizeof *blklist))) {
			fprintf(stderr, "failed to allocate block list (%d %dx%d blocks)\n",
					numblk, BLKSZ, BLKSZ);
			return -1;
		}
		blk = blklist;

		for(i=0; i<yblk; i++) {
			int y = i * BLKSZ;
			int height = i < yblk - 1 ? BLKSZ : vp.height - y;
			for(j=0; j<xblk; j++) {
				blk->x = j * BLKSZ;
				blk->y = y;
				blk->width = j < xblk - 1 ? BLKSZ : vp.width - blk->x;
				blk->height = height;
				blk++;
			}
		}
	}
#endif
	return 0;
}

static void render_pixel(cgm_vec4 *pixel, int x, int y)
{
	int i, num_hit;
	struct ray ray;
	cgm_vec4 color;

	pixel->x = pixel->y = pixel->z = pixel->w = 0.0f;

	if(optbits & (1 << REND_AAMASK)) {
		/* regular coverage-based alpha */
		for(i=0; i<num_samples; i++) {
			primray(&ray, x, y, i);
			raytrace(&color, &ray, scn, 0);

			pixel->x += color.x;
			pixel->y += color.y;
			pixel->z += color.z;
			pixel->w += color.w;
		}

		pixel->x = pow(pixel->x * sample_scale, 1.0f / 2.2f);
		pixel->y = pow(pixel->y * sample_scale, 1.0f / 2.2f);
		pixel->z = pow(pixel->z * sample_scale, 1.0f / 2.2f);
		pixel->w *= sample_scale;

	} else {
		/* binary alpha mask, and discard samples that don't hit */
		num_hit = 0;

		for(i=0; i<num_samples; i++) {
			primray(&ray, x, y, i);
			if(raytrace(&color, &ray, scn, 0)) {
				num_hit++;
				pixel->x += color.x;
				pixel->y += color.y;
				pixel->z += color.z;
			}
		}

		if(num_hit) {
			float s = 1.0f / (float)num_hit;
			pixel->x = pow(pixel->x * s, 1.0f / 2.2f);
			pixel->y = pow(pixel->y * s, 1.0f / 2.2f);
			pixel->z = pow(pixel->z * s, 1.0f / 2.2f);
			pixel->w = 1.0f;
		} else {
			pixel->w = 0.0f;
		}
	}
}

#ifdef USE_THREADS
static void render_worker(void *data)
{
	int i, j, x, y;
	struct block *blk = data;
	cgm_vec4 *pixptr;

	pixptr = rendfb->pixels + (vp.y + blk->y) * rendfb->width + vp.x + blk->x;

	y = blk->y;
	for(i=0; i<blk->height; i++) {
		x = blk->x;
		for(j=0; j<blk->width; j++) {
			render_pixel(pixptr++, x++, y);
		}
		y++;
		pixptr += rendfb->width - blk->width;
	}
}
#endif

void render(struct scene *scene)
{
	int i, j;
	cgm_vec4 *pixels;

	if(prepare_render() == -1) {
		return;
	}
	scn = scene;

#ifdef USE_THREADS
	/* skip the threaded version on machines with a single processor to avoid
	 * the overhead of a sad single thread pool worker pumping jobs in the dark
	 */
	if(num_proc > 1) {
		tpool_begin_batch(tpool);
		for(i=0; i<numblk; i++) {
			tpool_enqueue(tpool, blklist + i, render_worker, 0);
		}
		tpool_end_batch(tpool);
		tpool_wait(tpool);
		return;
	}
#endif

	pixels = rendfb->pixels + vp.y * rendfb->width + vp.x;
	for(i=0; i<vp.height; i++) {
		for(j=0; j<vp.width; j++) {
			render_pixel(pixels++, j, i);
		}
		pixels += rendfb->width - vp.width;
	}
}

static int raytrace(cgm_vec4 *color, struct ray *ray, struct scene *scn, int iter)
{
	struct rayhit hit;

	if(iter >= max_iter || !ray_scene(scn, ray, &hit)) {
		color->x = color->y = color->z = color->w = 0;
		return 0;
	}

	shade(color, scn, &hit, iter);
	return 1;
}

static void cons_tbn_matrix(float *tbn, struct rayhit *hit)
{
	cgm_vec3 bitan;
	cgm_vcross(&bitan, &hit->tang, &hit->norm);

	tbn[0] = hit->tang.x;
	tbn[1] = hit->tang.y;
	tbn[2] = hit->tang.z;
	tbn[4] = bitan.x;
	tbn[5] = bitan.y;
	tbn[6] = bitan.z;
	tbn[8] = hit->norm.x;
	tbn[9] = hit->norm.y;
	tbn[10] = hit->norm.z;
	tbn[3] = tbn[7] = tbn[11] = tbn[12] = tbn[13] = tbn[14] = 0.0f;
	tbn[15] = 1.0f;
}

static void shade(cgm_vec4 *color, struct scene *scn, struct rayhit *hit, int iter)
{
	int i, numlights;
	float ndotl, ndoth, spec;
	cgm_vec3 norm, hdir, vdir, ldir;
	struct material *mtl = hit->face->mtl;
	struct light *lt;
	struct ray sray;
	cgm_vec3 basecol;
	cgm_vec4 texel;
	float tbn[16];
	cgm_vec2 duv = {0, 0};

	cgm_wcons(color, mtl->ke.x, mtl->ke.y, mtl->ke.z, 1.0f);

	if(mtl->tex_normal) {
		cons_tbn_matrix(tbn, hit);
		texlookup(&texel, mtl->tex_normal, hit->uv, duv);

		cgm_vcons(&norm, texel.x * 2.0f - 1.0f, texel.y * 2.0f - 1.0f, texel.z * 2.0f - 1.0f);
		cgm_vmul_m3v3(&norm, tbn);
	} else {
		norm = hit->norm;
	}
	cgm_vnormalize(&norm);

#ifdef DBG_SDR_NORMALS
	color->x = norm.x * 0.5 + 0.5;
	color->y = norm.y * 0.5 + 0.5;
	color->z = norm.z * 0.5 + 0.5;
	return;
#endif

	if(mtl->tex_diffuse) {
		texlookup(&texel, mtl->tex_diffuse, hit->uv, duv);
		basecol = *(cgm_vec3*)&texel;
	} else {
		basecol = mtl->kd;
	}
	color->x += basecol.x * 0.05f;
	color->y += basecol.y * 0.05f;
	color->z += basecol.z * 0.05f;

	numlights = dynarr_size(scn->lights);
	for(i=0; i<numlights; i++) {
		lt = scn->lights[i];

		switch(lt->type) {
		case PTLIGHT:
			ldir = lt->posdir;
			cgm_vsub(&ldir, &hit->pos);
			break;

		case DIRLIGHT:
			ldir = lt->posdir;
			break;

		default:
			continue;	/* ignore invalid light type */
		}

		if(lt->shadows) {
			sray.origin = hit->pos;
			sray.dir = ldir;
			if(lt->type == DIRLIGHT) {
				cgm_vscale(&sray.dir, scn->bsph_rad * 4.0f);
			}
			calc_invdir(&sray);

			if(ray_scene(scn, &sray, 0)) {
				continue;
			}
		}

		if(lt->type == PTLIGHT) {
			cgm_vnormalize(&ldir);
		}

		ndotl = cgm_vdot(&norm, &ldir);
		if(ndotl < 0.0f) ndotl = 0.0f;

		vdir = hit->ray.dir;
		cgm_vcons(&vdir, -hit->ray.dir.x, -hit->ray.dir.y, -hit->ray.dir.z);
		cgm_vnormalize(&vdir);
		hdir = ldir; cgm_vadd(&hdir, &vdir);
		cgm_vnormalize(&hdir);

		ndoth = cgm_vdot(&norm, &hdir);
		if(ndoth < 0.0f) ndoth = 0.0f;
		spec = pow(ndoth, mtl->shin);

		color->x += basecol.x * ndotl + mtl->ks.x * spec;
		color->y += basecol.y * ndotl + mtl->ks.y * spec;
		color->z += basecol.z * ndotl + mtl->ks.z * spec;
	}
}

static void texlookup(cgm_vec4 *color, struct rendimage *img, cgm_vec2 uv, cgm_vec2 duv)
{
	int tx, ty;
	float u = fmod(uv.x, 1.0f);
	float v = fmod(uv.y, 1.0f);

	if(u < 0.0f) u += 1.0f;
	if(v < 0.0f) v += 1.0f;

	tx = (int)(u * (float)img->width);
	ty = (int)(v * (float)img->height);
	*color = img->pixels[ty * img->width + tx];
}

static void calc_invdir(struct ray *ray)
{
	ray->invdir.x = ray->dir.x == 0.0f ? 0.0f : 1.0f / ray->dir.x;
	ray->invdir.y = ray->dir.y == 0.0f ? 0.0f : 1.0f / ray->dir.y;
	ray->invdir.z = ray->dir.z == 0.0f ? 0.0f : 1.0f / ray->dir.z;
}

static void perspray(struct ray *ray, int x, int y, int n)
{
	float fx = (float)x + samples[n].x;
	float fy = (float)y + samples[n].y;

	ray->origin.x = ray->origin.y = ray->origin.z = 0.0f;

	ray->dir.x = (fx / (float)vp.width - 0.5f) * aspect;
	ray->dir.y = 0.5f - fy / (float)vp.height;
	ray->dir.z = -vpdist;
	cgm_vnormalize(&ray->dir);
	cgm_vscale(&ray->dir, raymag);

	cgm_vmul_m4v3(&ray->origin, view_xform);
	cgm_vmul_m3v3(&ray->dir, view_xform);

	calc_invdir(ray);
}

static void orthoray(struct ray *ray, int x, int y, int n)
{
	float fx = (float)x + samples[n].x;
	float fy = (float)y + samples[n].y;

	ray->origin.x = (fx / (float)vp.width - 0.5f) * orthosz * aspect;
	ray->origin.y = (0.5f - fy / (float)vp.height) * orthosz;
	ray->origin.z = zmax;

	ray->dir.x = ray->dir.y = 0;
	ray->dir.z = zmin - zmax;

	cgm_vmul_m4v3(&ray->origin, view_xform);
	cgm_vmul_m3v3(&ray->dir, view_xform);

	calc_invdir(ray);
}

static void calc_sample_pos_rec(int sidx, float xsz, float ysz, cgm_vec2 *pos)
{
	int quadrant;
	static const cgm_vec2 subpt[4] = {
		{-0.25, -0.25}, {0.25, -0.25}, {-0.25, 0.25}, {0.25, 0.25}
	};

	if(!sidx) {
		pos->x += (float)rand() / (float)RAND_MAX * xsz - xsz / 2.0f;
		pos->y += (float)rand() / (float)RAND_MAX * ysz - ysz / 2.0f;
		return;
	}

	quadrant = (sidx - 1) & 3;
	pos->x += subpt[quadrant].x * xsz;
	pos->y += subpt[quadrant].y * ysz;

	calc_sample_pos_rec((sidx - 1) / 4, xsz / 2.0f, ysz / 2.0f, pos);
}

static void calc_sample_pos(int sidx, cgm_vec2 *pos)
{
	pos->x = pos->y = 0.0f;
	calc_sample_pos_rec(sidx, 1.0f, 1.0f, pos);
}
