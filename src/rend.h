#ifndef REND_H_
#define REND_H_

#include "cgmath/cgmath.h"

struct scene;

struct ray {
	cgm_vec3 origin, dir, invdir;
};

struct rect {
	int x, y, width, height;
};

struct rendimage {
	int width, height;
	cgm_vec4 *pixels;
};

enum {
	REND_AAMASK	= 1
};

extern struct rendimage *rendfb;

int rend_init(void);
void rend_cleanup(void);

void rend_enable(unsigned int opt);
void rend_disable(unsigned int opt);

void rend_viewport(int x, int y, int width, int height);
void rend_perspective(float fov, float zfar);
void rend_ortho(float ysz, float zmin, float zmax);
void rend_view(float *xform);
void rend_samples(int n);

void render(struct scene *scn);

#endif	/* REND_H_ */
