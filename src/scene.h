#ifndef SCENE_H_
#define SCENE_H_

#include "cgmath/cgmath.h"
#include "rend.h"

struct rendimage;

struct material {
	char *name;
	cgm_vec3 kd, ks, ke;
	float shin;
	struct rendimage *tex_diffuse;
	struct rendimage *tex_normal;
};

struct aabox {
	cgm_vec3 vmin, vmax;
};

struct vertex {
	cgm_vec3 pos, norm, tang;
	cgm_vec2 uv;
};

struct meshtri {
	unsigned long id;
	struct vertex v[3];
	cgm_vec3 norm;
	struct material *mtl;
};

struct mesh {
	char *name;

	struct meshtri *faces;
	int nfaces;

	struct material *mtl;

	struct aabox aabb;
	float xform[16], inv_xform[16];
};

enum light_type { PTLIGHT, DIRLIGHT };

struct light {
	enum light_type type;
	cgm_vec3 posdir;
	cgm_vec3 color;
	int shadows;
};

struct octnode {
	struct meshtri *faces;
	struct aabox box;
	struct octnode *sub[8];
};

struct scene {
	struct mesh **meshes;
	struct material **mtl;
	struct light **lights;

	struct octnode *octree;

	struct aabox aabb;
	cgm_vec3 bsph_cent;
	float bsph_rad;
};

struct rayhit {
	float t;
	struct ray ray;
	struct mesh *mesh;
	struct meshtri *face;
	cgm_vec3 bary;

	cgm_vec3 pos, norm, tang;
	cgm_vec2 uv;
};

int init_scene(struct scene *scn);
void destroy_scene(struct scene *scn);

void free_material(struct material *mtl);
void init_mesh(struct mesh *mesh);
void free_mesh(struct mesh *mesh);

int load_scene(struct scene *scn, const char *fname);
int dump_scene(struct scene *scn, const char *fname);

void add_mesh(struct scene *scn, struct mesh *mesh);
void add_material(struct scene *scn, struct material *mtl);
void add_light(struct scene *scn, struct light *lt);

struct mesh *find_mesh(struct scene *scn, const char *name);
struct material *find_material(struct scene *scn, const char *name);

enum { COLOR_LINEAR, COLOR_SRGB };
struct rendimage *load_texture(const char *fname, int srgb);

int ray_scene(struct scene *scn, struct ray *ray, struct rayhit *hit);
int ray_octree(struct octnode *oct, struct ray *ray, struct rayhit *hit);
int ray_mesh(struct mesh *mesh, struct ray *ray, struct rayhit *hit);
int ray_triangle(struct meshtri *tri, struct ray *ray, struct rayhit *hit);

int ray_aabb(struct aabox *box, struct ray *ray);

int build_octree(struct scene *scn);
void free_octree(struct octnode *tree);
void print_octstats(struct scene *scn);

#endif /* SCENE_H_ */
