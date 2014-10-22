// take a series of ply files and produce a digital elevation map


#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "smapa.h"
#include "xmalloc.c"
#include "iio.h"
#include "lists.c"
SMART_PARAMETER(PLY_RECORD_LENGTH, 27)


// fast forward "f" until "lin" is found
static void eat_until_this_line(FILE *f, char *lin)
{
	char buf[FILENAME_MAX] = {0};
	while (fgets(buf, FILENAME_MAX, f))
		if (0 == strcmp(buf, lin))
			return;
}

static void update_min_max(float *min, float *max, float x)
{
    if (x < *min) *min = x;
    if (x > *max) *max = x;
}

// re-scale a float between 0 and w
static int rescale_float_to_int(float x, float min, float max, int w)
{
	int r = w * (x - min)/(max - min);
	if (r < 0) r = 0;
	if (r >= w) r = w -1;
	return r;
}

struct images {
	float *min;
	float *max;
	float *cnt;
	float *avg;
	int w, h;
};

// update the output images with a new height
static void add_height_to_images(struct images *x, int i, int j, float v)
{
	int k = x->w * j + i;
	x->min[k] = fmin(x->min[k], v);
	x->max[k] = fmax(x->max[k], v);
	x->avg[k] = (v + x->cnt[k] * x->avg[k]) / (1 + x->cnt[k]);
	x->cnt[k] += 1;
}

// open a ply file and update the known extrema
static void parse_ply_points_for_extrema(float *xmin, float *xmax, float *ymin,
        float *ymax, char *fname)
{
	FILE *f = fopen(fname, "r");
	if (!f) {
		fprintf(stderr, "WARNING: can not open file \"%s\"\n", fname);
		return;
	}

	eat_until_this_line(f, "end_header\n");

	size_t n = PLY_RECORD_LENGTH();
	char cbuf[n];
	float *fbuf = (void*) cbuf;
	while (n == fread(cbuf, 1, n, f))
	{
        update_min_max(xmin, xmax, fbuf[0]);
        update_min_max(ymin, ymax, fbuf[1]);
		//fprintf(stderr, "\t%f %f\n", fbuf[0], fbuf[1]);
	}

	fclose(f);
}

// open a ply file, and accumulata its points to the image
static void add_ply_points_to_images(struct images *x,
		float xmin, float xmax, float ymin, float ymax,
		char *fname)
{
	FILE *f = fopen(fname, "r");
	if (!f) {
		fprintf(stderr, "WARNING: can not open file \"%s\"\n", fname);
		return;
	}

	eat_until_this_line(f, "end_header\n");

	size_t n = PLY_RECORD_LENGTH();
	char cbuf[n];
	float *fbuf = (void*)cbuf;
	while (n == fread(cbuf, 1, n, f))
	{
		int i = rescale_float_to_int(fbuf[0], xmin, xmax, x->w);
		int j = rescale_float_to_int(fbuf[1], ymin, ymax, x->h);
		//fprintf(stderr, "\t%8.8lf %8.8lf %8.8lf %d %d\n",
		//		fbuf[0], fbuf[1], fbuf[2], i, j);
		add_height_to_images(x, i, j, fbuf[2]);
	}

	fclose(f);
}


int main(int c, char *v[])
{
	// process input arguments
	if (c != 4) {
		fprintf(stderr, "usage:\n\t"
				"ls files | %s w h out.tif\n", *v);
		return 1;
	}
	int w = atoi(v[1]);
	int h = atoi(v[2]);
	char *filename_out = v[3];

    // initialize x,y extrema values
    float xmin = INFINITY;
    float xmax = -INFINITY;
    float ymin = INFINITY;
    float ymax = -INFINITY;

	// allocate and initialize output images
	struct images x;
	x.w = w;
	x.h = h;
	x.min = xmalloc(w*h*sizeof(float));
	x.max = xmalloc(w*h*sizeof(float));
	x.cnt = xmalloc(w*h*sizeof(float));
	x.avg = xmalloc(w*h*sizeof(float));
	for (int i = 0; i < w*h; i++)
	{
		x.min[i] = INFINITY;
		x.max[i] = -INFINITY;
		x.cnt[i] = 0;
		x.avg[i] = 0;
	}

	// process each filename from stdin to determine x,y extremas
	char fname[FILENAME_MAX];
    struct list *l = NULL;
	while (fgets(fname, FILENAME_MAX, stdin))
	{
		strtok(fname, "\n");
		printf("FILENAME: \"%s\"\n", fname);
        l = push(l, fname);
		parse_ply_points_for_extrema(&xmin, &xmax, &ymin, &ymax, fname);
	}

    // fprintf(stderr, "xmin: %f, xmax: %f, ymin: %f, ymax: %f\n", xmin, xmax,
    //         ymin, ymax);
    // list_print(l);

	// process each filename to accumulate points in the dem
	while (l != NULL)
	{
		printf("FILENAME: \"%s\"\n", l->current);
		add_ply_points_to_images(&x, xmin, xmax, ymin, ymax, l->current);
        l = l->next;
	}

	// set unknown values to NAN
	for (int i = 0; i < w*h; i++)
		if (!x.cnt[i])
			x.avg[i] = NAN;

	// save output image
	iio_save_image_float(filename_out, x.avg, w, h);

	// cleanup and exit
	free(x.min);
	free(x.max);
	free(x.cnt);
	free(x.avg);
	return 0;
}
