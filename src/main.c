#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "rend.h"
#include "scene.h"
#include "cgmath/cgmath.h"
#include "imago2.h"
#include "tilerend.h"
#include "script.h"

int opt_verbose, opt_nsamples = 1;
struct scene scn, vis;
char *outfname;
struct rendimage framebuf;
int tilewidth, tileheight;
cgm_vec3 viewpos, viewtarg;

static const char *infname;

static int parse_args(int argc, char **argv);


int main(int argc, char **argv)
{
	struct timeval tv, tv0;
	FILE *infile = stdin;
	char buf[512];

	if(parse_args(argc, argv) == -1) {
		return 1;
	}
	if(infname) {
		if(!(infile = fopen(infname, "rb"))) {
			fprintf(stderr, "failed to open render script: %s\n", infname);
			return 1;
		}
	}

	/* insane defaults */
	if(!outfname) outfname = strdup("output.png");
	framebuf.width = framebuf.height = 512;
	tilewidth = tileheight = 512;
	cgm_vcons(&viewpos, 0, 0, 0);
	cgm_vcons(&viewtarg, 0, 0, -1);

	init_scene(&scn);
	init_scene(&vis);
	rend_init();

	gettimeofday(&tv0, 0);

	while(fgets(buf, sizeof buf, infile)) {
		if(parse_cmd(buf) == -1) {
			return 1;
		}
	}

	gettimeofday(&tv, 0);

	printf("total: %g sec\n", (float)(tv.tv_sec - tv0.tv_sec) + (float)(tv.tv_usec - tv0.tv_usec) / 1000000.0f);

	printf("writing output: %s\n", outfname);
	if(img_save_pixels(outfname, framebuf.pixels, framebuf.width, framebuf.height,IMG_FMT_RGBAF) == -1) {
		fprintf(stderr, "failed to write output image\n");
		return 1;
	}
	return 0;
}

static const char *usage = "usage %s [options] <file>\n"
	"Options:\n"
	" -v,-verbose: verbose output, use multiple 'v's for more verbosity\n"
	" -h,-help: print usage and exit\n";

static int parse_args(int argc, char **argv)
{
	int i;
	char *s;

	for(i=1; i<argc; i++) {
		if(argv[i][0] == '-') {
			if(argv[i][1] == 'v') {
				int vcount = 0;
				s = argv[i];
				while(*++s == 'v') vcount++;
				if(!*s) {
					opt_verbose = vcount;
					continue;
				}
			}

			if(strcmp(argv[i], "-o") == 0) {
				if(!argv[++i]) {
					fprintf(stderr, "-o must be followed by a file path\n");
					return -1;
				}
				if(!(outfname = strdup(argv[i]))) {
					fprintf(stderr, "failed to allocate output filename buffer\n");
					return -1;
				}

			} else if(strcmp(argv[i], "-verbose") == 0) {
				opt_verbose++;

			} else if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-help") == 0) {
				printf(usage, argv[0]);
				exit(0);

			} else {
				fprintf(stderr, "invalid option: %s\n", argv[i]);
				return -1;
			}
		} else {
			if(infname) {
				fprintf(stderr, "unexpected argument: %s\n", argv[i]);
				return -1;
			}
			infname = argv[i];
		}
	}

	return 0;
}
