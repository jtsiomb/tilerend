#include <stdio.h>
#include <limits.h>
#include <float.h>
#include <assert.h>
#include "tilerend.h"
#include "scene.h"
#include "rend.h"
#include "dynarr.h"
#include "meshfile.h"
#include "imago2.h"

#define OCT_SPLITCNT	32
#define OCT_MAXDEPTH	10

static void compute_tangents(struct mesh *mesh);

/*static void calc_bounds(struct scene *scn);*/
static int build_octree_rec(struct octnode *oct, int depth);
static void init_aabox(struct aabox *box);
static void expand_aabox(struct aabox *box, const cgm_vec3 *pt);
int aabox_tri_test(const struct aabox *box, const struct meshtri *tri);

static struct material defmtl = {"default", {0.8, 0.2, 0.75}, {0.4, 0.4, 0.4}, {0}, 50.0f, 0, 0};

int init_scene(struct scene *scn)
{
	memset(scn, 0, sizeof *scn);

	if(!(scn->meshes = dynarr_alloc(0, sizeof *scn->meshes))) {
		fprintf(stderr, "init_scene: failed to initialize meshes dynamic array\n");
		return -1;
	}
	if(!(scn->mtl = dynarr_alloc(0, sizeof *scn->mtl))) {
		fprintf(stderr, "init_scene: failed to initialize materials dynamic array\n");
		return -1;
	}
	if(!(scn->lights = dynarr_alloc(0, sizeof *scn->lights))) {
		fprintf(stderr, "init_scene: failed to initialize lights dynamic array\n");
		return -1;
	}
	return 0;
}

void destroy_scene(struct scene *scn)
{
	int i;

	for(i=0; i<dynarr_size(scn->meshes); i++) {
		free_mesh(scn->meshes[i]);
	}
	dynarr_free(scn->meshes);

	for(i=0; i<dynarr_size(scn->mtl); i++) {
		free_material(scn->mtl[i]);
	}
	dynarr_free(scn->mtl);

	for(i=0; i<dynarr_size(scn->lights); i++) {
		free(scn->lights[i]);
	}
	dynarr_free(scn->lights);

	free_octree(scn->octree);
}

void free_material(struct material *mtl)
{
	if(!mtl) return;
	free(mtl->name);
	if(mtl->tex_diffuse) {
		free(mtl->tex_diffuse->pixels);
	}
	if(mtl->tex_normal) {
		free(mtl->tex_normal->pixels);
	}
	free(mtl);
}

void init_mesh(struct mesh *mesh)
{
	memset(mesh, 0, sizeof *mesh);
	cgm_midentity(mesh->xform);
	cgm_midentity(mesh->inv_xform);
}

void free_mesh(struct mesh *mesh)
{
	if(!mesh) return;
	free(mesh->faces);
}

int load_scene(struct scene *scn, const char *fname)
{
	int i, j, k;
	struct mf_meshfile *mf;
	struct mf_mesh *mfm;
	struct mf_material *mfmtl;
	struct mesh *mesh = 0;
	struct material *mtl = 0;
	const char *tex;
	cgm_vec3 va, vb;
	static const cgm_vec3 defnorm = {0, 1, 0};
	static const cgm_vec2 defuv;
	unsigned long faceid = 0;

	if(opt_verbose) {
		printf("loading: %s ...\n", fname);
	}

	if(!(mf = mf_alloc()) || mf_load(mf, fname) == -1) {
		fprintf(stderr, "load_scene: failed to load: %s\n", fname);
		return -1;
	}

	for(i=0; i<mf_num_materials(mf); i++) {
		mfmtl = mf_get_material(mf, i);
		if(!(mtl = calloc(1, sizeof *mtl)) || !(mtl->name = strdup(mfmtl->name))) {
			fprintf(stderr, "load_scene: failed to allocate material\n");
			goto err;
		}
		mtl->kd = *(cgm_vec3*)&mfmtl->attr[MF_COLOR].val;
		mtl->ks = *(cgm_vec3*)&mfmtl->attr[MF_SPECULAR].val;
		mtl->ke = *(cgm_vec3*)&mfmtl->attr[MF_EMISSIVE].val;
		mtl->shin = mfmtl->attr[MF_SHININESS].val.x;

		if(mfmtl->attr[MF_COLOR].map.name) {
			tex = mf_find_asset(mf, mfmtl->attr[MF_COLOR].map.name);
			if(!(mtl->tex_diffuse = load_texture(tex, COLOR_SRGB))) {
				goto err;
			}
		}
		if(mfmtl->attr[MF_BUMP].map.name) {
			tex = mf_find_asset(mf, mfmtl->attr[MF_BUMP].map.name);
			if(!(mtl->tex_normal = load_texture(tex, COLOR_LINEAR))) {
				goto err;
			}
		}

		add_material(scn, mtl);
		mtl = 0;
	}

	for(i=0; i<mf_num_meshes(mf); i++) {
		mfm = mf_get_mesh(mf, i);

		if(!(mesh = malloc(sizeof *mesh))) {
			fprintf(stderr, "load_scene: failed to allocate mesh\n");
			goto err;
		}
		init_mesh(mesh);
		mesh->name = strdup(mfm->name);

		if(!(mesh->faces = malloc(mfm->num_faces * sizeof *mesh->faces))) {
			fprintf(stderr, "load_Scene: failed to allocate %u mesh faces\n", mfm->num_faces);
			goto err;
		}
		mesh->nfaces = mfm->num_faces;

		if(mfm->mtl) {
			if(!(mesh->mtl = find_material(scn, mfm->mtl->name))) {
				mesh->mtl = &defmtl;
			}
		}

		for(j=0; j<mfm->num_faces; j++) {
			struct meshtri *tri = mesh->faces + j;
			tri->mtl = mesh->mtl;
			tri->id = faceid++;

			for(k=0; k<3; k++) {
				int vidx = mfm->faces[j].vidx[k];
				tri->v[k].pos = *(cgm_vec3*)&mfm->vertex[vidx];
				tri->v[k].norm = mfm->normal ? *(cgm_vec3*)&mfm->normal[vidx] : defnorm;
				tri->v[k].uv = mfm->texcoord ? *(cgm_vec2*)&mfm->texcoord[vidx] : defuv;
				cgm_vcons(&tri->v[k].tang, 0, 0, 0);
			}

			va = tri->v[1].pos; cgm_vsub(&va, &tri->v[0].pos);
			vb = tri->v[2].pos; cgm_vsub(&vb, &tri->v[0].pos);
			cgm_vcross(&tri->norm, &va, &vb);
			cgm_vnormalize(&tri->norm);
		}

		mesh->aabb.vmin = *(cgm_vec3*)&mfm->aabox.vmin;
		mesh->aabb.vmax = *(cgm_vec3*)&mfm->aabox.vmax;

		if(mesh->mtl->tex_normal) {
			compute_tangents(mesh);
		}

		add_mesh(scn, mesh);
		mesh = 0;
	}

	mf_free(mf);

	/*
	printf("building octree ...\n");
	build_octree(scn);
	print_octstats(scn);

	calc_bounds(scn);
	*/
	return 0;

err:
	free_material(mtl);
	free_mesh(mesh);
	mf_free(mf);
	return -1;
}

int dump_scene(struct scene *scn, const char *fname)
{
	int i, j, k;
	FILE *fp;
	long vidx = 0;
	struct mesh *mesh;
	struct meshtri *tri;

	if(!(fp = fopen(fname, "wb"))) {
		fprintf(stderr, "failed to open %s for writing\n", fname);
		return -1;
	}

	for(i=0; i<dynarr_size(scn->meshes); i++) {
		mesh = scn->meshes[i];

		fprintf(fp, "o %s\n", mesh->name);

		for(j=0; j<mesh->nfaces; j++) {
			tri = mesh->faces + j;
			for(k=0; k<3; k++) {
				fprintf(fp, "v %f %f %f\n", tri->v[k].pos.x, tri->v[k].pos.y, tri->v[k].pos.z);
			}
		}

		for(j=0; j<mesh->nfaces; j++) {
			tri = mesh->faces + j;
			for(k=0; k<3; k++) {
				fprintf(fp, "vn %f %f %f\n", tri->v[k].norm.x, tri->v[k].norm.y, tri->v[k].norm.z);
			}
		}

		for(j=0; j<mesh->nfaces; j++) {
			tri = mesh->faces + j;
			for(k=0; k<3; k++) {
				fprintf(fp, "vt %f %f\n", tri->v[k].uv.x, tri->v[k].uv.y);
			}
		}

		for(j=0; j<mesh->nfaces; j++) {
			tri = mesh->faces + j;
			fputc('f', fp);
			for(k=0; k<3; k++) {
				vidx++;
				fprintf(fp, " %ld/%ld/%ld", vidx, vidx, vidx);
			}
			fputc('\n', fp);
		}
	}

	fclose(fp);
	return 0;
}

void add_mesh(struct scene *scn, struct mesh *mesh)
{
	if(!(scn->meshes = dynarr_push(scn->meshes, &mesh))) {
		abort();
	}
}

void add_material(struct scene *scn, struct material *mtl)
{
	if(!(scn->mtl = dynarr_push(scn->mtl, &mtl))) {
		abort();
	}
}

void add_light(struct scene *scn, struct light *lt)
{
	if(!(scn->lights = dynarr_push(scn->lights, &lt))) {
		abort();
	}
}

struct mesh *find_mesh(struct scene *scn, const char *name)
{
	int i;

	for(i=0; i<dynarr_size(scn->meshes); i++) {
		if(strcmp(scn->meshes[i]->name, name) == 0) {
			return scn->meshes[i];
		}
	}
	return 0;
}

struct material *find_material(struct scene *scn, const char *name)
{
	int i;

	for(i=0; i<dynarr_size(scn->mtl); i++) {
		if(strcmp(scn->mtl[i]->name, name) == 0) {
			return scn->mtl[i];
		}
	}
	return 0;
}

struct rendimage *load_texture(const char *fname, int srgb)
{
	int i, npixels;
	struct img_pixmap img;
	struct rendimage *ri;
	cgm_vec4 *pptr;

	if(opt_verbose) printf("loading texture: %s\n", fname);
	img_init(&img);
	if(img_load(&img, fname) == -1) {
		fprintf(stderr, "load_texture: failed to load image: %s\n", fname);
		return 0;
	}
	if(img_is_float(&img)) {
		srgb = 0;	/* float image formats are linear */
	}
	if(img_convert(&img, IMG_FMT_RGBAF) == -1) {
		fprintf(stderr, "load_texture: failed to convert to RGBA float format\n");
		img_destroy(&img);
		return 0;
	}

	if(!(ri = malloc(sizeof *ri))) {
		fprintf(stderr, "load_texture: failed to allocate image\n");
		img_destroy(&img);
		return 0;
	}
	ri->pixels = (cgm_vec4*)img.pixels;
	ri->width = img.width;
	ri->height = img.height;

	if(srgb) {
		npixels = img.width * img.height;
		pptr = ri->pixels;
		for(i=0; i<npixels; i++) {
			pptr->x = pow(pptr->x, 2.2f);
			pptr->y = pow(pptr->y, 2.2f);
			pptr->z = pow(pptr->z, 2.2f);
			pptr++;
		}
	}
	return ri;
}

static void compute_rayhit(struct rayhit *hit, struct ray *ray)
{
	struct meshtri *tri = hit->face;
	unsigned int texbits = tri->mtl->tex_diffuse ? 1 : 0;

	if(tri->mtl->tex_normal) texbits |= 2;

	hit->ray = *ray;
	hit->pos.x = ray->origin.x + ray->dir.x * hit->t;
	hit->pos.y = ray->origin.y + ray->dir.y * hit->t;
	hit->pos.z = ray->origin.z + ray->dir.z * hit->t;

	hit->norm.x = tri->v[0].norm.x * hit->bary.x + tri->v[1].norm.x * hit->bary.y +
		tri->v[2].norm.x * hit->bary.z;
	hit->norm.y = tri->v[0].norm.y * hit->bary.x + tri->v[1].norm.y * hit->bary.y +
		tri->v[2].norm.y * hit->bary.z;
	hit->norm.z = tri->v[0].norm.z * hit->bary.x + tri->v[1].norm.z * hit->bary.y +
		tri->v[2].norm.z * hit->bary.z;
	cgm_vnormalize(&hit->norm);

	if(texbits & 2) {
		hit->tang.x = tri->v[0].tang.x * hit->bary.x + tri->v[1].tang.x * hit->bary.y +
			tri->v[2].tang.x * hit->bary.z;
		hit->tang.y = tri->v[0].tang.y * hit->bary.x + tri->v[1].tang.y * hit->bary.y +
			tri->v[2].tang.y * hit->bary.z;
		hit->tang.z = tri->v[0].tang.z * hit->bary.x + tri->v[1].tang.z * hit->bary.y +
			tri->v[2].tang.z * hit->bary.z;
		cgm_vnormalize(&hit->tang);
	}

	if(texbits) {
		hit->uv.x = tri->v[0].uv.x * hit->bary.x + tri->v[1].uv.x * hit->bary.y +
			tri->v[2].uv.x * hit->bary.z;
		hit->uv.y = tri->v[0].uv.y * hit->bary.x + tri->v[1].uv.y * hit->bary.y +
			tri->v[2].uv.y * hit->bary.z;
	}
}

/* adapted from: https://terathon.com/blog/tangent-space.html */
static void compute_tangents(struct mesh *mesh)
{
	int i, j;
	float r, dot;
	cgm_vec3 va, vb, udir, nprojt;
	cgm_vec2 ta, tb;
	struct meshtri *tri = mesh->faces;

	/* assumes tangents are all 0 */

	for(i=0; i<mesh->nfaces; i++) {
		cgm_vcsub(&va, &tri->v[1].pos, &tri->v[0].pos);
		cgm_vcsub(&vb, &tri->v[2].pos, &tri->v[0].pos);
		ta.x = tri->v[1].uv.x - tri->v[0].uv.x;
		ta.y = tri->v[1].uv.y - tri->v[0].uv.y;
		tb.x = tri->v[2].uv.x - tri->v[0].uv.x;
		tb.y = tri->v[2].uv.y - tri->v[0].uv.y;
		r = 1.0f / (ta.x * tb.y - tb.x * ta.y);

		udir.x = (tb.y * va.x - ta.y * vb.x) * r;
		udir.y = (tb.y * va.y - ta.y * vb.y) * r;
		udir.z = (tb.y * va.z - ta.y * vb.z) * r;

		cgm_vadd(&tri->v[0].tang, &udir);
		cgm_vadd(&tri->v[1].tang, &udir);
		cgm_vadd(&tri->v[2].tang, &udir);
		tri++;
	}

	tri = mesh->faces;
	for(i=0; i<mesh->nfaces; i++) {
		for(j=0; j<3; j++) {
			dot = cgm_vdot(&tri->v[j].norm, &tri->v[j].tang);
			cgm_vcscale(&nprojt, &tri->v[j].norm, dot);
			cgm_vsub(&tri->v[j].tang, &nprojt);
			cgm_vnormalize(&tri->v[j].tang);
		}
	}
}

int ray_scene(struct scene *scn, struct ray *ray, struct rayhit *hit)
{
	int i, count;
	struct rayhit tmphit, nearest = {FLT_MAX};

	if(scn->octree) {
		if(ray_octree(scn->octree, ray, hit)) {
			if(hit) compute_rayhit(hit, ray);
			return 1;
		}
		return 0;
	}

	/* fallback to brute force */
	count = dynarr_size(scn->meshes);

	if(!hit) {
		for(i=0; i<count; i++) {
			if(ray_mesh(scn->meshes[i], ray, 0)) {
				return 1;
			}
		}
		return 0;
	}

	for(i=0; i<count; i++) {
		if(ray_mesh(scn->meshes[i], ray, &tmphit) && tmphit.t < nearest.t) {
			nearest = tmphit;
		}
	}

	if(nearest.mesh) {
		*hit = nearest;
		compute_rayhit(hit, ray);
		return 1;
	}
	return 0;
}

int ray_octree(struct octnode *oct, struct ray *ray, struct rayhit *hit)
{
	int i, count;
	struct rayhit tmphit, nearest = {FLT_MAX};

	if(!ray_aabb(&oct->box, ray)) {
		return 0;
	}

	if(oct->faces) {
		/* leaf node, check faces */
		count = dynarr_size(oct->faces);

		if(!hit) {
			for(i=0; i<count; i++) {
				if(ray_triangle(oct->faces + i, ray, 0)) {
					return 1;
				}
			}
			return 0;
		}

		for(i=0; i<count; i++) {
			if(ray_triangle(oct->faces + i, ray, &tmphit) && tmphit.t < nearest.t) {
				nearest = tmphit;
			}
		}

		if(nearest.face) {
			*hit = nearest;
			hit->mesh = (void*)1;
			return 1;
		}
		return 0;
	}

	/* non-leaf, recurse to children */
	/* TODO order front->back to stop early */
	for(i=0; i<8; i++) {
		if(ray_octree(oct->sub[i], ray, &tmphit) && tmphit.t < nearest.t) {
			nearest = tmphit;
		}
	}

	if(nearest.face) {
		if(hit) {
			*hit = nearest;
			hit->mesh = (void*)1;
		}
		return 1;
	}
	return 0;
}

int ray_mesh(struct mesh *mesh, struct ray *ray, struct rayhit *hit)
{
	int i;
	struct ray localray;
	struct rayhit tmphit, nearest = {FLT_MAX};

	localray = *ray;
	cgm_vmul_m4v3(&localray.origin, mesh->inv_xform);
	cgm_vmul_m3v3(&localray.dir, mesh->inv_xform);
	localray.invdir.x = 1.0f / localray.dir.x;
	localray.invdir.y = 1.0f / localray.dir.y;
	localray.invdir.z = 1.0f / localray.dir.z;

	if(!ray_aabb(&mesh->aabb, &localray)) {
		return 0;
	}

	if(!hit) {
		for(i=0; i<mesh->nfaces; i++) {
			if(ray_triangle(mesh->faces + i, &localray, 0)) {
				return 1;
			}
		}
		return 0;
	}

	for(i=0; i<mesh->nfaces; i++) {
		if(ray_triangle(mesh->faces + i, &localray, &tmphit) && tmphit.t < nearest.t) {
			nearest = tmphit;
		}
	}

	if(nearest.face) {
		*hit = nearest;
		hit->mesh = mesh;
		return 1;
	}
	return 0;
}

#define EPSILON	1e-5
int ray_triangle(struct meshtri *tri, struct ray *ray, struct rayhit *hit)
{
	cgm_vec3 vab, vac, tvec, pvec, qvec;
	float det, inv_det, u, v, t;

	vab = tri->v[1].pos; cgm_vsub(&vab, &tri->v[0].pos);
	vac = tri->v[2].pos; cgm_vsub(&vac, &tri->v[0].pos);

	cgm_vcross(&pvec, &ray->dir, &vac);

	if((det = cgm_vdot(&vab, &pvec)) > -EPSILON && det < EPSILON) {
		return 0;
	}
	inv_det = 1.0f / det;

	tvec = ray->origin; cgm_vsub(&tvec, &tri->v[0].pos);

	u = cgm_vdot(&tvec, &pvec) * inv_det;
	if(u < 0.0f || u > 1.0f) return 0;

	cgm_vcross(&qvec, &tvec, &vab);

	v = cgm_vdot(&ray->dir, &qvec) * inv_det;
	if(v < 0.0f || u + v > 1.0f) return 0;

	t = cgm_vdot(&vac, &qvec) * inv_det;
	if(t < EPSILON) {
		return 0;
	}

	if(hit) {
		hit->t = t;
		hit->bary.x = 1.0f - u - v;
		hit->bary.y = u;
		hit->bary.z = v;
		hit->face = tri;
	}
	return 1;
}

int ray_aabb(struct aabox *box, struct ray *ray)
{
	float t0, t1, tmp;
	float tmin = -FLT_MAX, tmax = FLT_MAX;

	if(ray->dir.x == 0.0f) {
		if(ray->origin.x < box->vmin.x || ray->origin.x > box->vmax.x) {
			return 0;
		}
	} else {
		t0 = (box->vmin.x - ray->origin.x) * ray->invdir.x;
		t1 = (box->vmax.x - ray->origin.x) * ray->invdir.x;
		if(t1 < t0) {
			tmp = t0;
			t0 = t1;
			t1 = tmp;
		}
		if(t0 > tmin) tmin = t0;
		if(t1 < tmax) tmax = t1;
		if(tmax < tmin) return 0;
		if(tmax < 0.0f) return 0;
	}

	if(ray->dir.y == 0.0f) {
		if(ray->origin.y < box->vmin.y || ray->origin.y > box->vmax.y) {
			return 0;
		}
	} else {
		t0 = (box->vmin.y - ray->origin.y) * ray->invdir.y;
		t1 = (box->vmax.y - ray->origin.y) * ray->invdir.y;
		if(t1 < t0) {
			tmp = t0;
			t0 = t1;
			t1 = tmp;
		}
		if(t0 > tmin) tmin = t0;
		if(t1 < tmax) tmax = t1;
		if(tmax < tmin) return 0;
		if(tmax < 0.0f) return 0;
	}

	if(ray->dir.z == 0.0f) {
		if(ray->origin.z < box->vmin.z || ray->origin.z > box->vmax.z) {
			return 0;
		}
	} else {
		t0 = (box->vmin.z - ray->origin.z) * ray->invdir.z;
		t1 = (box->vmax.z - ray->origin.z) * ray->invdir.z;
		if(t1 < t0) {
			tmp = t0;
			t0 = t1;
			t1 = tmp;
		}
		if(t0 > tmin) tmin = t0;
		if(t1 < tmax) tmax = t1;
		if(tmax < tmin) return 0;
		if(tmax < 0.0f) return 0;
	}

	return 1;
}

int build_octree(struct scene *scn)
{
	int i, j, k, nmeshes;
	struct octnode *root;

	if(!(root = calloc(1, sizeof *root))) {
		fprintf(stderr, "failed to allocate octree root node\n");
		return -1;
	}
	if(!(root->faces = dynarr_alloc(0, sizeof *root->faces))) {
		fprintf(stderr, "failed to allocate octree root faces array\n");
		free(root);
		return -1;
	}

	init_aabox(&root->box);

	nmeshes = dynarr_size(scn->meshes);
	for(i=0; i<nmeshes; i++) {
		struct mesh *mesh = scn->meshes[i];
		for(j=0; j<mesh->nfaces; j++) {
			struct meshtri tri = mesh->faces[j];
			tri.mtl = mesh->mtl;

			for(k=0; k<3; k++) {
				cgm_vmul_m4v3(&tri.v[k].pos, mesh->xform);
				cgm_vmul_m3v3(&tri.v[k].norm, mesh->xform);
				cgm_vmul_m3v3(&tri.v[k].tang, mesh->xform);

				expand_aabox(&root->box, &tri.v[k].pos);
			}

			if(!(root->faces = dynarr_push(root->faces, &tri))) {
				abort();
			}
		}
	}

	scn->octree = root;

	return build_octree_rec(root, 0);
}

static int build_octree_rec(struct octnode *oct, int depth)
{
	int i, j, count;
	struct aabox *box;
	struct octnode *node;
	float midx, midy, midz;

	if(dynarr_size(oct->faces) < OCT_SPLITCNT || depth >= OCT_MAXDEPTH) {
		return 0;
	}

	midx = (oct->box.vmin.x + oct->box.vmax.x) / 2;
	midy = (oct->box.vmin.y + oct->box.vmax.y) / 2;
	midz = (oct->box.vmin.z + oct->box.vmax.z) / 2;

	count = dynarr_size(oct->faces);
	for(i=0; i<8; i++) {
		if(!(node = calloc(1, sizeof *node))) {
			fprintf(stderr, "build_octree: failed to allocate node\n");
			while(--i >= 0) free_octree(oct->sub[i]);
			return -1;
		}
		oct->sub[i] = node;
		box = &node->box;

		if(!(node->faces = dynarr_alloc(0, sizeof *node->faces))) {
			fprintf(stderr, "build_octree: failed to allocate face list\n");
			do free_octree(oct->sub[i]); while(--i >= 0);
			return -1;
		}

		if(i & 4) {
			box->vmin.x = midx;
			box->vmax.x = oct->box.vmax.x;
		} else {
			box->vmin.x = oct->box.vmin.x;
			box->vmax.x = midx;
		}
		if(i & 2) {
			box->vmin.y = midy;
			box->vmax.y = oct->box.vmax.y;
		} else {
			box->vmin.y = oct->box.vmin.y;
			box->vmax.y = midy;
		}
		if(i & 1) {
			box->vmin.z = midz;
			box->vmax.z = oct->box.vmax.z;
		} else {
			box->vmin.z = oct->box.vmin.z;
			box->vmax.z = midz;
		}

		for(j=0; j<count; j++) {
			if(aabox_tri_test(box, oct->faces + j)) {
				if(!(node->faces = dynarr_push(node->faces, oct->faces + j))) {
					abort();
				}
			}
		}
	}

	dynarr_free(oct->faces);
	oct->faces = 0;

	for(i=0; i<8; i++) {
		if(build_octree_rec(oct->sub[i], depth + 1) == -1) {
			return -1;
		}
	}
	return 0;
}

struct octstats {
	int min_faces, max_faces;
	int height;
};

static void get_octstats(struct octnode *n, struct octstats *s, int h)
{
	int i;

	if(h > s->height) {
		s->height = h;
	}

	if(n->faces) {
		int nfaces = dynarr_size(n->faces);
		if(nfaces > 0 && nfaces < s->min_faces) {
			s->min_faces = nfaces;
		}
		if(nfaces > s->max_faces) {
			s->max_faces = nfaces;
		}
		assert(!n->sub[0] && !n->sub[1] && !n->sub[2] && !n->sub[3]);
		assert(!n->sub[4] && !n->sub[5] && !n->sub[6] && !n->sub[7]);
		return;
	}

	for(i=0; i<8; i++) {
		get_octstats(n->sub[i], s, h + 1);
	}
}

void print_octstats(struct scene *scn)
{
	struct octstats st = {INT_MAX, 0, 0};

	if(!scn->octree) {
		printf("no octree\n");
		return;
	}

	get_octstats(scn->octree, &st, 1);

	printf("octree stats\n------------\n");
	printf(" height: %d\n", st.height);
	printf(" least faces: %d\n", st.min_faces);
	printf(" most faces: %d\n", st.max_faces);
}

static void init_aabox(struct aabox *box)
{
	box->vmin.x = box->vmin.y = box->vmin.z = FLT_MAX;
	box->vmax.x = box->vmax.y = box->vmax.z = -FLT_MAX;
}

static void expand_aabox(struct aabox *box, const cgm_vec3 *pt)
{
	if(pt->x < box->vmin.x) box->vmin.x = pt->x;
	if(pt->y < box->vmin.y) box->vmin.y = pt->y;
	if(pt->z < box->vmin.z) box->vmin.z = pt->z;
	if(pt->x > box->vmax.x) box->vmax.x = pt->x;
	if(pt->y > box->vmax.y) box->vmax.y = pt->y;
	if(pt->z > box->vmax.z) box->vmax.z = pt->z;
}


static inline float fltmin(float a, float b) { return a < b ? a : b; }
static inline float fltmax(float a, float b) { return a > b ? a : b; }
#define fltmin3(a, b, c)	fltmin(fltmin(a, b), c)
#define fltmax3(a, b, c)	fltmax(fltmax(a, b), c)

/* aabox/triangle intersection test based on algorithm from
 * "Realtime Collision Detection" by Christer Ericson. ch.5.2.9, p.171.
 */
#if 0
int aabox_tri_test(const struct aabox *box, const struct meshtri *tri)
{
	int i, j;
	float p0, p1, p2, r, e0, e1, e2, minp, maxp, pdist, s;
	cgm_vec3 c, v0, v1, v2, f[3];
	cgm_vec3 ax, pnorm;
	static const cgm_vec3 bax[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

	/* compute aabox center/extents */
	c.x = (box->vmin.x + box->vmax.x) * 0.5f;
	c.y = (box->vmin.y + box->vmax.y) * 0.5f;
	c.z = (box->vmin.z + box->vmax.z) * 0.5f;
	e0 = (box->vmax.x - box->vmin.x) * 0.5f;
	e1 = (box->vmax.y - box->vmin.y) * 0.5f;
	e2 = (box->vmax.z - box->vmin.z) * 0.5f;

	/* translate triangle to the coordinate system of the bounding box */
	v0 = tri->v[0].pos; cgm_vsub(&v0, &c);
	v1 = tri->v[1].pos; cgm_vsub(&v1, &c);
	v2 = tri->v[2].pos; cgm_vsub(&v2, &c);

	/* my own addition: this seems to fail sometimes when the triangle is
	 * entirely inside the aabox. let's add a hack to catch that...
	 */
	if(fabs(v0.x) <= e0 && fabs(v0.y) <= e1 && fabs(v0.z) <= e2) return 1;
	if(fabs(v1.x) <= e0 && fabs(v1.y) <= e1 && fabs(v1.z) <= e2) return 1;
	if(fabs(v2.x) <= e0 && fabs(v2.y) <= e1 && fabs(v2.z) <= e2) return 1;

	/* compute edge vectors for triangle */
	f[0] = v1; cgm_vsub(f, &v0);
	f[1] = v2; cgm_vsub(f + 1, &v1);
	f[2] = v0; cgm_vsub(f + 2, &v2);

	/* test axes a00..a22 */
	for(i=0; i<3; i++) {
		for(j=0; j<3; j++) {
			cgm_vcross(&ax, bax + i, f + j);
			p0 = cgm_vdot(&v0, &ax);
			p1 = cgm_vdot(&v1, &ax);
			p2 = cgm_vdot(&v2, &ax);
			r = e0 * fabs(ax.x) + e1 * fabs(ax.y) + e2 * fabs(ax.z);

			minp = fltmin3(p0, p1, p2);
			maxp = fltmax3(p0, p1, p2);

			if(minp > r || maxp < -r) return 0;		/* found separating axis */
		}
	}

	if(fltmax3(v0.x, v1.x, v2.x) < -e0 || fltmin3(v0.x, v1.x, v2.x) > e0) return 0;
	if(fltmax3(v0.y, v1.y, v2.y) < -e1 || fltmin3(v0.y, v1.y, v2.y) > e1) return 0;
	if(fltmax3(v0.z, v1.z, v2.z) < -e2 || fltmin3(v0.z, v1.z, v2.z) > e2) return 0;

	cgm_vcross(&pnorm, f, f + 1);
	pdist = cgm_vdot(&pnorm, &v0);

	r = e0 * fabs(pnorm.x) + e1 * fabs(pnorm.y) + e2 * fabs(pnorm.z);
	s = cgm_vdot(&pnorm, &c) - pdist;
	return fabs(s) <= r;
}
#endif

int aabox_tri_test(const struct aabox *box, const struct meshtri *tri)
{
	struct aabox tribox;

	tribox.vmin = tribox.vmax = tri->v[0].pos;
	expand_aabox(&tribox, &tri->v[1].pos);
	expand_aabox(&tribox, &tri->v[2].pos);

	if(tribox.vmin.x > box->vmax.x || tribox.vmax.x < box->vmin.x) return 0;
	if(tribox.vmin.y > box->vmax.y || tribox.vmax.y < box->vmin.y) return 0;
	if(tribox.vmin.z > box->vmax.z || tribox.vmax.z < box->vmin.z) return 0;
	return 1;
}

void free_octree(struct octnode *tree)
{
	int i;

	if(!tree) return;

	if(tree->faces) dynarr_free(tree->faces);

	for(i=0; i<8; i++) {
		free_octree(tree->sub[i]);
	}
	free(tree);
}

/*
static void calc_bounds(struct scene *scn)
{
	int i, j, k, nmeshes;
	struct mesh *mesh;
	float dsq;

	scn->bsph_cent.x = (scn->octree->box.vmin.x + scn->octree->box.vmax.x) * 0.5f;
	scn->bsph_cent.y = (scn->octree->box.vmin.y + scn->octree->box.vmax.y) * 0.5f;
	scn->bsph_cent.z = (scn->octree->box.vmin.z + scn->octree->box.vmax.z) * 0.5f;

	scn->bsph_rad = 0.0f;
	nmeshes = dynarr_size(scn->meshes);
	for(i=0; i<nmeshes; i++) {
		mesh = scn->meshes[i];
		for(j=0; j<mesh->nfaces; j++) {
			for(k=0; k<3; k++) {
				dsq = cgm_vdist_sq(&mesh->faces[j].v[k].pos, &scn->bsph_cent);
				if(dsq > scn->bsph_rad) {
					scn->bsph_rad = dsq;
				}
			}
		}
	}

	if(scn->bsph_rad > 0.0f) {
		scn->bsph_rad = sqrt(scn->bsph_rad);
	}
}
*/
