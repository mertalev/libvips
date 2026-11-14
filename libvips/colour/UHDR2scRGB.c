/* Turn UHDR (RGB plus a gainmap) to scRGB colourspace.
 *
 * 26/11/25
 * 	- from XYZ2scRGB.c.c
 */

/*

	This file is part of VIPS.

	VIPS is free software; you can redistribute it and/or modify
	it under the terms of the GNU Lesser General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
	02110-1301  USA

 */

/*

	These files are distributed with VIPS - http://www.vips.ecs.soton.ac.uk

 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /*HAVE_CONFIG_H*/
#include <glib/gi18n-lib.h>

#include <stdio.h>
#include <math.h>

#include <vips/vips.h>

#include "pcolour.h"

gboolean
vips__image_is_uhdr(VipsImage *image)
{
	return
		vips_image_get_typeof(image, "gainmap-max-content-boost") &&
		vips_image_get_typeof(image, "gainmap-min-content-boost") &&
		vips_image_get_typeof(image, "gainmap-gamma") &&
		vips_image_get_typeof(image, "gainmap-offset-sdr") &&
		vips_image_get_typeof(image, "gainmap-offset-hdr");
}

typedef struct _VipsUHDR2scRGB {
	VipsColour parent;

	VipsImage *in;

	/* Gainmap metadata.
	 */
	float gamma[3];
	float min_content_boost[3];
	float max_content_boost[3];
	float offset_hdr[3];
	float offset_sdr[3];

	/* XYZ to the base image's linear RGB, and that RGB to scRGB.
	 */
	float xyz2rgb[3][3];
	float rgb2scrgb[3][3];

	/* The gain for each gainmap value.
	 */
	float gain[3][256];

} VipsUHDR2scRGB;

typedef VipsColourClass VipsUHDR2scRGBClass;

G_DEFINE_TYPE(VipsUHDR2scRGB, vips_UHDR2scRGB, VIPS_TYPE_COLOUR);

/* Derived from the apache-licensed applyGain() method of libuhdr.
 */

/* A mono gainmap has one gain for all three bands.
 */
static inline void
vips_UHDR2scRGB_gainmap(VipsUHDR2scRGB *uhdr,
	float *restrict q, const float *restrict p1, const VipsPel *restrict p2,
	int width, const int bands)
{
	const float (*m)[3] = uhdr->xyz2rgb;
	const float (*n)[3] = uhdr->rgb2scrgb;

	for (int i = 0; i < width; i++) {
		float gain[3];
		float rgb[3];

		for (int b = 0; b < bands; b++)
			gain[b] = uhdr->gain[b][p2[b]];
		p2 += bands;

		for (int b = 0; b < 3; b++) {
			const int k = bands == 1 ? 0 : b;

			// in the base image's linear RGB
			float v = m[b][0] * p1[0] + m[b][1] * p1[1] + m[b][2] * p1[2];

			rgb[b] = ((v + uhdr->offset_sdr[k]) * gain[k]) -
				uhdr->offset_hdr[k];
		}
		p1 += 3;

		// back to scrgb
		for (int b = 0; b < 3; b++)
			q[b] = n[b][0] * rgb[0] + n[b][1] * rgb[1] + n[b][2] * rgb[2];
		q += 3;
	}
}

static void
vips_UHDR2scRGB_line(VipsColour *colour, VipsPel *out, VipsPel **in, int width)
{
	VipsUHDR2scRGB *uhdr = (VipsUHDR2scRGB *) colour;
	float *q = (float *) out;
	float *p1 = (float *) in[0];
	VipsPel *p2 = (VipsPel *) in[1];

	g_assert(colour->in[0]->Xsize == colour->in[1]->Xsize);
	g_assert(colour->in[0]->Ysize == colour->in[1]->Ysize);

	// constant band counts, so the compiler can specialise
	if (colour->in[1]->Bands == 1)
		vips_UHDR2scRGB_gainmap(uhdr, q, p1, p2, width, 1);
	else
		vips_UHDR2scRGB_gainmap(uhdr, q, p1, p2, width, 3);
}

/* Import the base image's primaries to get its linear RGB <-> XYZ matrices.
 */
static int
vips_UHDR2scRGB_primaries(VipsUHDR2scRGB *uhdr)
{
	static const VipsPel primaries[9] = { 255, 0, 0, 0, 255, 0, 0, 0, 255 };
	static const double xyz2scrgb[3][3] = {
		{ 3.240625, -1.537208, -0.498629 },
		{ -0.968931, 1.875756, 0.041518 },
		{ 0.055710, -0.204021, 1.056996 }
	};

	VipsImage **t = (VipsImage **)
		vips_object_local_array(VIPS_OBJECT(uhdr), 4);

	if (!(t[0] = vips_image_new_from_memory_copy(primaries, 9,
			  3, 1, 3, VIPS_FORMAT_UCHAR)) ||
		vips_copy(t[0], &t[1],
			"interpretation", VIPS_INTERPRETATION_sRGB,
			NULL))
		return -1;

	if (vips_image_get_typeof(uhdr->in, VIPS_META_ICC_NAME)) {
		const void *data;
		size_t length;

		if (vips_image_get_blob(uhdr->in, VIPS_META_ICC_NAME, &data, &length))
			return -1;
		vips_image_set_blob_copy(t[1], VIPS_META_ICC_NAME, data, length);
	}

	if (vips_icc_import(t[1], &t[2],
			"embedded", TRUE,
			"pcs", VIPS_PCS_XYZ,
			"input_profile", "srgb",
			NULL) ||
		!(t[3] = vips_image_copy_memory(t[2])))
		return -1;

	// columns are the XYZ of each primary
	float *p = (float *) VIPS_IMAGE_ADDR(t[3], 0, 0);
	double m[3][3];
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			m[i][j] = p[j * 3 + i] / 100.0;

	double det =
		m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
		m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
		m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
	if (fabs(det) < 1e-6) {
		vips_error("UHDR2scRGB", "%s", _("bad profile primaries"));
		return -1;
	}

	// the input is XYZ in 0 - 100
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++) {
			int i1 = (i + 1) % 3, i2 = (i + 2) % 3;
			int j1 = (j + 1) % 3, j2 = (j + 2) % 3;

			uhdr->xyz2rgb[j][i] = (m[i1][j1] * m[i2][j2] -
				m[i1][j2] * m[i2][j1]) / (det * 100.0);
		}

	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			uhdr->rgb2scrgb[i][j] =
				xyz2scrgb[i][0] * m[0][j] +
				xyz2scrgb[i][1] * m[1][j] +
				xyz2scrgb[i][2] * m[2][j];

	return 0;
}

// pass in the array to fill, size must match
static int
image_get_array_float(VipsImage *image, const char *name,
	float *out, int n_out)
{
	double *d;
	int n;
	if (vips_image_get_array_double(image, name, &d, &n))
		return -1;
	if (n != n_out) {
		vips_error("image_get_array_float", _("bad size"));
		return -1;
	}

	for (int i = 0; i < n; i++)
		out[i] = d[i];

	return 0;
}

static int
vips_UHDR2scRGB_build(VipsObject *object)
{
	VipsObjectClass *class = VIPS_OBJECT_GET_CLASS(object);
	VipsColour *colour = VIPS_COLOUR(object);
	VipsUHDR2scRGB *uhdr = (VipsUHDR2scRGB *) object;

	/* We allow one-band gainmap plus 3 band main image, so we have to turn
	 * off the automatic detach-attach alpha processing.
	 */
	colour->input_bands = 0;

	colour->n = 2;
	colour->in = (VipsImage **) vips_object_local_array(object, 2);

	/* Helpful error if this is not a UHDR image.
	 *
	 * UHDR images can also be missing fields if we've been built without
	 * UHDR support.
	 */
	if (uhdr->in &&
		!vips__image_is_uhdr(uhdr->in)) {
		vips_error(class->nickname, "%s", _("not a UHDR image, "
				"or UHDR is not enabled"));
		return -1;
	}

	/* Get the gainmap image metadata.
	 */
	if (uhdr->in) {
		if (vips_check_bands(class->nickname, uhdr->in, 3))
			return -1;
		if (uhdr->in->BandFmt != VIPS_FORMAT_UCHAR) {
			vips_error(class->nickname, "%s", _("image must be uchar"));
			return -1;
		}

		/* Import the main image to XYZ to get linear light.
		 */
		if (vips_icc_import(uhdr->in, &colour->in[0],
			"embedded", TRUE,
			"pcs", VIPS_PCS_XYZ,
			"input_profile", "srgb",
			NULL) ||
			vips_UHDR2scRGB_primaries(uhdr))
			return -1;

		if (image_get_array_float(uhdr->in,
				"gainmap-max-content-boost", &uhdr->max_content_boost[0], 3) ||
			image_get_array_float(uhdr->in,
				"gainmap-min-content-boost", &uhdr->min_content_boost[0], 3) ||
			image_get_array_float(uhdr->in,
				"gainmap-gamma", &uhdr->gamma[0], 3) ||
			image_get_array_float(uhdr->in,
				"gainmap-offset-sdr", &uhdr->offset_sdr[0], 3) ||
			image_get_array_float(uhdr->in,
				"gainmap-offset-hdr", &uhdr->offset_hdr[0], 3))
			return -1;

		for (int b = 0; b < 3; b++) {
			float min_boost = log2f(uhdr->min_content_boost[b]);
			float max_boost = log2f(uhdr->max_content_boost[b]);

			for (int i = 0; i < 256; i++) {
				// gainmaps are already linear
				float g = powf(i / 255.0F, 1.0F / uhdr->gamma[b]);

				uhdr->gain[b][i] = exp2f(min_boost * (1.0F - g) + max_boost * g);
			}
		}

		VipsImage *gainmap;
		if (!(gainmap = vips_image_get_gainmap(uhdr->in)))
			return -1;
		if (vips_check_bands_1or3(class->nickname, gainmap)) {
			VIPS_UNREF(gainmap);
			return -1;
		}

		/* And now scale the gainmap image to match the main image 1:1.
		 */
		if (vips_resize(gainmap, &colour->in[1],
			(double) uhdr->in->Xsize / gainmap->Xsize,
			"vscale", (double) uhdr->in->Ysize / gainmap->Ysize,
			"kernel", VIPS_KERNEL_LINEAR,
			NULL)) {
			VIPS_UNREF(gainmap);
			return -1;
		}
		VIPS_UNREF(gainmap);
	}

	if (VIPS_OBJECT_CLASS(vips_UHDR2scRGB_parent_class)->build(object))
		return -1;

	return 0;
}

static void
vips_UHDR2scRGB_class_init(VipsUHDR2scRGBClass *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(class);
	VipsObjectClass *object_class = (VipsObjectClass *) class;
	VipsColourClass *colour_class = VIPS_COLOUR_CLASS(class);

	gobject_class->set_property = vips_object_set_property;
	gobject_class->get_property = vips_object_get_property;

	object_class->nickname = "UHDR2scRGB";
	object_class->description = _("transform UHDR to scRGB");
	object_class->build = vips_UHDR2scRGB_build;

	colour_class->process_line = vips_UHDR2scRGB_line;

	VIPS_ARG_IMAGE(class, "in", 1,
		_("Input"),
		_("Input image"),
		VIPS_ARGUMENT_REQUIRED_INPUT,
		G_STRUCT_OFFSET(VipsColourTransform, in));
}

static void
vips_UHDR2scRGB_init(VipsUHDR2scRGB *uhdr)
{
	VipsColour *colour = VIPS_COLOUR(uhdr);

	colour->interpretation = VIPS_INTERPRETATION_scRGB;
	colour->format = VIPS_FORMAT_FLOAT;
	colour->bands = 3;
}

/**
 * vips_UHDR2scRGB: (method)
 * @in: input image
 * @out: (out): output image
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Transform a UHDR image (three band sRGB with an attached gainmap) to
 * scRGB.
 *
 * Returns: 0 on success, -1 on error
 */
int
vips_UHDR2scRGB(VipsImage *in, VipsImage **out, ...)
{
	va_list ap;
	int result;

	va_start(ap, out);
	result = vips_call_split("UHDR2scRGB", ap, in, out);
	va_end(ap);

	return result;
}

/* Kept for compat. Use vips_UHDR2scRGB() instead.
 */
typedef VipsUHDR2scRGB VipsUhdr2scRGB;
typedef VipsColourClass VipsUhdr2scRGBClass;

G_DEFINE_TYPE(VipsUhdr2scRGB, vips_uhdr2scRGB, VIPS_TYPE_COLOUR);

static void
vips_uhdr2scRGB_class_init(VipsUhdr2scRGBClass *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(class);
	VipsObjectClass *object_class = (VipsObjectClass *) class;
	VipsOperationClass *operation_class = (VipsOperationClass *) class;
	VipsColourClass *colour_class = VIPS_COLOUR_CLASS(class);

	gobject_class->set_property = vips_object_set_property;
	gobject_class->get_property = vips_object_get_property;

	object_class->nickname = "uhdr2scRGB";
	object_class->description = _("transform uhdr to scRGB");
	object_class->build = vips_UHDR2scRGB_build;

	operation_class->flags |= VIPS_OPERATION_DEPRECATED;

	colour_class->process_line = vips_UHDR2scRGB_line;

	VIPS_ARG_IMAGE(class, "in", 1,
		_("Input"),
		_("Input image"),
		VIPS_ARGUMENT_REQUIRED_INPUT,
		G_STRUCT_OFFSET(VipsColourTransform, in));
}

static void
vips_uhdr2scRGB_init(VipsUhdr2scRGB *uhdr)
{
	VipsColour *colour = VIPS_COLOUR(uhdr);

	colour->interpretation = VIPS_INTERPRETATION_XYZ;
	colour->format = VIPS_FORMAT_FLOAT;
	colour->bands = 3;
}

/**
 * vips_uhdr2scRGB: (skip)
 * @in: input image
 * @out: (out): output image
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Deprecated, please use [method@Image.UHDR2scRGB].
 *
 * Returns: 0 on success, -1 on error
 */
int
vips_uhdr2scRGB(VipsImage *in, VipsImage **out, ...)
{
	va_list ap;
	int result;

	va_start(ap, out);
	result = vips_call_split("uhdr2scRGB", ap, in, out);
	va_end(ap);

	return result;
}
