/* boolean.c -- various bit operations
 *
 * Modified:
 * 15/12/94 JC
 * 	- ANSIfied
 * 	- adapted to partials with im_wrap...
 * 25/1/95 JC
 *	- added check1ary(), check2ary()
 * 8/2/95 JC
 *	- new im_wrapmany
 * 19/7/95 JC
 *	- added im_shiftleft() and im_shiftright()
 * 6/7/98 JC
 *	- added _vec forms
 * 	- removed *p++ stuff
 * 10/9/99 JC
 *	- and/or/eor now do all int types
 * 10/10/02 JC
 *	- renamed im_and() etc. as im_andimage() to remove breakage in the C++
 *	  layer if operator names are turned on
 * 30/6/04
 *	- now cast float/complex args to int
 * 11/9/09
 * 	- use new im__cast_and__call()
 * 	- therefore now supports 1-band $op n-band
 * 17/9/09
 * 	- moved to im__arith_binary*()
 * 	- renamed im_eor_vec() as im_eorimage_vec() for C++ sanity
 * 21/11/10
 * 	- oop, constants are always (int) now, so (^-1) works for unsigned
 * 	  types
 * 12/11/11
 * 	- redo as a class
 */

/*

	Copyright (C) 1991-2005 The National Gallery

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 2.1 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public
	License along with this library; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
	02110-1301  USA

 */

/*

	These files are distributed with VIPS - http://www.vips.ecs.soton.ac.uk

 */

/*
#define DEBUG
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /*HAVE_CONFIG_H*/
#include <glib/gi18n-lib.h>

#include <stdio.h>
#include <stdlib.h>

#include <vips/vips.h>
#include <vips/internal.h>

#include "binary.h"
#include "unaryconst.h"

typedef struct _VipsBoolean {
	VipsBinary parent_instance;

	VipsOperationBoolean operation;

} VipsBoolean;

typedef VipsBinaryClass VipsBooleanClass;

G_DEFINE_TYPE(VipsBoolean, vips_boolean, VIPS_TYPE_BINARY);

static int
vips_boolean_build(VipsObject *object)
{
	VipsObjectClass *class = VIPS_OBJECT_GET_CLASS(object);
	VipsBinary *binary = (VipsBinary *) object;

	if (binary->left &&
		vips_check_noncomplex(class->nickname, binary->left))
		return -1;
	if (binary->right &&
		vips_check_noncomplex(class->nickname, binary->right))
		return -1;

	if (VIPS_OBJECT_CLASS(vips_boolean_parent_class)->build(object))
		return -1;

	return 0;
}

#define LOOP(TYPE, OP) \
	{ \
		TYPE *restrict left = (TYPE *) in[0]; \
		TYPE *restrict right = (TYPE *) in[1]; \
		TYPE *restrict q = (TYPE *) out; \
\
		for (x = 0; x < sz; x++) \
			q[x] = left[x] OP right[x]; \
	}

#define FLOOP(TYPE, OP) \
	{ \
		TYPE *restrict left = (TYPE *) in[0]; \
		TYPE *restrict right = (TYPE *) in[1]; \
		int *restrict q = (int *) out; \
\
		for (x = 0; x < sz; x++) \
			q[x] = ((int) left[x]) OP((int) right[x]); \
	}

#define SWITCH(I, F, OP) \
	switch (vips_image_get_format(im)) { \
	case VIPS_FORMAT_UCHAR: \
		I(unsigned char, OP); \
		break; \
	case VIPS_FORMAT_CHAR: \
		I(signed char, OP); \
		break; \
	case VIPS_FORMAT_USHORT: \
		I(unsigned short, OP); \
		break; \
	case VIPS_FORMAT_SHORT: \
		I(signed short, OP); \
		break; \
	case VIPS_FORMAT_UINT: \
		I(unsigned int, OP); \
		break; \
	case VIPS_FORMAT_INT: \
		I(signed int, OP); \
		break; \
	case VIPS_FORMAT_FLOAT: \
		F(float, OP); \
		break; \
	case VIPS_FORMAT_DOUBLE: \
		F(double, OP); \
		break; \
\
	default: \
		g_assert_not_reached(); \
	}

#define FNLOOP(TYPE, FN) \
	{ \
		TYPE *restrict left = (TYPE *) in[0]; \
		TYPE *restrict right = (TYPE *) in[1]; \
		int *restrict q = (int *) out; \
\
		for (x = 0; x < sz; x++) \
			q[x] = FN(left[x], right[x]); \
	}

static void
vips_boolean_buffer(VipsArithmetic *arithmetic,
	VipsPel *out, VipsPel **in, int width)
{
	VipsBoolean *boolean = (VipsBoolean *) arithmetic;
	VipsImage *im = arithmetic->ready[0];
	const int sz = width * vips_image_get_bands(im);

	int x;

	switch (boolean->operation) {
	case VIPS_OPERATION_BOOLEAN_AND:
		SWITCH(LOOP, FLOOP, &);
		break;

	case VIPS_OPERATION_BOOLEAN_OR:
		SWITCH(LOOP, FLOOP, |);
		break;

	case VIPS_OPERATION_BOOLEAN_EOR:
		SWITCH(LOOP, FLOOP, ^);
		break;

	/* Special case: we need to be able to use VIPS_LSHIFT_INT().
	 */
	case VIPS_OPERATION_BOOLEAN_LSHIFT:
		switch (vips_image_get_format(im)) {
		case VIPS_FORMAT_UCHAR:
			LOOP(unsigned char, <<);
			break;
		case VIPS_FORMAT_CHAR:
			FNLOOP(signed char, VIPS_LSHIFT_INT);
			break;
		case VIPS_FORMAT_USHORT:
			LOOP(unsigned short, <<);
			break;
		case VIPS_FORMAT_SHORT:
			FNLOOP(signed short, VIPS_LSHIFT_INT);
			break;
		case VIPS_FORMAT_UINT:
			LOOP(unsigned int, <<);
			break;
		case VIPS_FORMAT_INT:
			FNLOOP(signed int, VIPS_LSHIFT_INT);
			break;
		case VIPS_FORMAT_FLOAT:
			FLOOP(float, <<);
			break;
		case VIPS_FORMAT_DOUBLE:
			FLOOP(double, <<);
			break;

		default:
			g_assert_not_reached();
		}
		break;

	case VIPS_OPERATION_BOOLEAN_RSHIFT:
		SWITCH(LOOP, FLOOP, >>);
		break;

	default:
		g_assert_not_reached();
	}
}

/* Save a bit of typing.
 */
#define UC VIPS_FORMAT_UCHAR
#define C VIPS_FORMAT_CHAR
#define US VIPS_FORMAT_USHORT
#define S VIPS_FORMAT_SHORT
#define UI VIPS_FORMAT_UINT
#define I VIPS_FORMAT_INT
#define F VIPS_FORMAT_FLOAT
#define X VIPS_FORMAT_COMPLEX
#define D VIPS_FORMAT_DOUBLE
#define DX VIPS_FORMAT_DPCOMPLEX

/* Type conversions for boolean.
 */
static const VipsBandFormat vips_boolean_format_table[10] = {
	/* Band format:  UC  C  US  S  UI  I  F  X  D  DX */
	/* Promotion: */ UC, C, US, S, UI, I, I, I, I, I
};

static void
vips_boolean_class_init(VipsBooleanClass *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(class);
	VipsObjectClass *object_class = (VipsObjectClass *) class;
	VipsArithmeticClass *aclass = VIPS_ARITHMETIC_CLASS(class);

	gobject_class->set_property = vips_object_set_property;
	gobject_class->get_property = vips_object_get_property;

	object_class->nickname = "boolean";
	object_class->description = _("boolean operation on two images");
	object_class->build = vips_boolean_build;

	aclass->process_line = vips_boolean_buffer;

	vips_arithmetic_set_format_table(aclass, vips_boolean_format_table);

	VIPS_ARG_ENUM(class, "boolean", 200,
		_("Operation"),
		_("Boolean to perform"),
		VIPS_ARGUMENT_REQUIRED_INPUT,
		G_STRUCT_OFFSET(VipsBoolean, operation),
		VIPS_TYPE_OPERATION_BOOLEAN,
		VIPS_OPERATION_BOOLEAN_AND);
}

static void
vips_boolean_init(VipsBoolean *boolean)
{
}

static int
vips_booleanv(VipsImage *left, VipsImage *right, VipsImage **out,
	VipsOperationBoolean operation, va_list ap)
{
	return vips_call_split("boolean", ap, left, right, out, operation);
}

/**
 * vips_boolean: (method)
 * @left: left-hand input [class@Image]
 * @right: right-hand input [class@Image]
 * @out: (out): output [class@Image]
 * @boolean: boolean operation to perform
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Perform various boolean operations on pairs of images.
 *
 * The output image is the same format as the upcast input images for integer
 * types. Float types are cast to int before processing. Complex types are not
 * supported.
 *
 * If the images differ in size, the smaller image is enlarged to match the
 * larger by adding zero pixels along the bottom and right.
 *
 * If the number of bands differs, one of the images
 * must have one band. In this case, an n-band image is formed from the
 * one-band image by joining n copies of the one-band image together, and then
 * the two n-band images are operated upon.
 *
 * The two input images are cast up to the smallest common format (see table
 * Smallest common format in
 * [arithmetic](libvips-arithmetic.html)).
 *
 * ::: seealso
 *     [method@Image.boolean_const].
 *
 * Returns: 0 on success, -1 on error
 */
int
vips_boolean(VipsImage *left, VipsImage *right, VipsImage **out,
	VipsOperationBoolean boolean, ...)
{
	va_list ap;
	int result;

	va_start(ap, boolean);
	result = vips_booleanv(left, right, out, boolean, ap);
	va_end(ap);

	return result;
}

/**
 * vips_andimage: (method)
 * @left: left-hand input [class@Image]
 * @right: right-hand input [class@Image]
 * @out: (out): output [class@Image]
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Perform [enum@Vips.OperationBoolean.AND] on a pair of images. See
 * [method@Image.boolean].
 *
 * Returns: 0 on success, -1 on error
 */
int
vips_andimage(VipsImage *left, VipsImage *right, VipsImage **out, ...)
{
	va_list ap;
	int result;

	va_start(ap, out);
	result = vips_booleanv(left, right, out,
		VIPS_OPERATION_BOOLEAN_AND, ap);
	va_end(ap);

	return result;
}

/**
 * vips_orimage: (method)
 * @left: left-hand input [class@Image]
 * @right: right-hand input [class@Image]
 * @out: (out): output [class@Image]
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Perform [enum@Vips.OperationBoolean.OR] on a pair of images. See
 * [method@Image.boolean].
 *
 * Returns: 0 on success, -1 on error
 */
int
vips_orimage(VipsImage *left, VipsImage *right, VipsImage **out, ...)
{
	va_list ap;
	int result;

	va_start(ap, out);
	result = vips_booleanv(left, right, out,
		VIPS_OPERATION_BOOLEAN_OR, ap);
	va_end(ap);

	return result;
}

/**
 * vips_eorimage: (method)
 * @left: left-hand input [class@Image]
 * @right: right-hand input [class@Image]
 * @out: (out): output [class@Image]
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Perform [enum@Vips.OperationBoolean.EOR] on a pair of images. See
 * [method@Image.boolean].
 *
 * Returns: 0 on success, -1 on error
 */
int
vips_eorimage(VipsImage *left, VipsImage *right, VipsImage **out, ...)
{
	va_list ap;
	int result;

	va_start(ap, out);
	result = vips_booleanv(left, right, out,
		VIPS_OPERATION_BOOLEAN_EOR, ap);
	va_end(ap);

	return result;
}

/**
 * vips_lshift: (method)
 * @left: left-hand input [class@Image]
 * @right: right-hand input [class@Image]
 * @out: (out): output [class@Image]
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Perform [enum@Vips.OperationBoolean.LSHIFT] on a pair of images. See
 * [method@Image.boolean].
 *
 * Returns: 0 on success, -1 on error
 */
int
vips_lshift(VipsImage *left, VipsImage *right, VipsImage **out, ...)
{
	va_list ap;
	int result;

	va_start(ap, out);
	result = vips_booleanv(left, right, out,
		VIPS_OPERATION_BOOLEAN_LSHIFT, ap);
	va_end(ap);

	return result;
}

/**
 * vips_rshift: (method)
 * @left: left-hand input [class@Image]
 * @right: right-hand input [class@Image]
 * @out: (out): output [class@Image]
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Perform [enum@Vips.OperationBoolean.RSHIFT] on a pair of images. See
 * [method@Image.boolean].
 *
 * Returns: 0 on success, -1 on error
 */
int
vips_rshift(VipsImage *left, VipsImage *right, VipsImage **out, ...)
{
	va_list ap;
	int result;

	va_start(ap, out);
	result = vips_booleanv(left, right, out,
		VIPS_OPERATION_BOOLEAN_RSHIFT, ap);
	va_end(ap);

	return result;
}

typedef struct _VipsBooleanConst {
	VipsUnaryConst parent_instance;

	VipsOperationBoolean operation;
} VipsBooleanConst;

typedef VipsUnaryConstClass VipsBooleanConstClass;

G_DEFINE_TYPE(VipsBooleanConst,
	vips_boolean_const, VIPS_TYPE_UNARY_CONST);

static int
vips_boolean_const_build(VipsObject *object)
{
	VipsObjectClass *class = VIPS_OBJECT_GET_CLASS(object);
	VipsUnary *unary = (VipsUnary *) object;

	if (unary->in &&
		vips_check_noncomplex(class->nickname, unary->in))
		return -1;

	if (VIPS_OBJECT_CLASS(vips_boolean_const_parent_class)->build(object))
		return -1;

	return 0;
}

#define LOOPC(TYPE, OP) \
	{ \
		TYPE *restrict p = (TYPE *) in[0]; \
		TYPE *restrict q = (TYPE *) out; \
		int *restrict c = uconst->c_int; \
\
		for (i = 0, x = 0; x < width; x++) \
			for (b = 0; b < bands; b++, i++) \
				q[i] = p[i] OP c[b]; \
	}

#define FLOOPC(TYPE, OP) \
	{ \
		TYPE *restrict p = (TYPE *) in[0]; \
		int *restrict q = (int *) out; \
		int *restrict c = uconst->c_int; \
\
		for (i = 0, x = 0; x < width; x++) \
			for (b = 0; b < bands; b++, i++) \
				q[i] = ((int) p[i]) OP((int) c[b]); \
	}

static void
vips_boolean_const_buffer(VipsArithmetic *arithmetic,
	VipsPel *out, VipsPel **in, int width)
{
	VipsUnaryConst *uconst = (VipsUnaryConst *) arithmetic;
	VipsBooleanConst *bconst = (VipsBooleanConst *) arithmetic;
	VipsImage *im = arithmetic->ready[0];
	int bands = im->Bands;

	int i, x, b;

	switch (bconst->operation) {
	case VIPS_OPERATION_BOOLEAN_AND:
		SWITCH(LOOPC, FLOOPC, &);
		break;

	case VIPS_OPERATION_BOOLEAN_OR:
		SWITCH(LOOPC, FLOOPC, |);
		break;

	case VIPS_OPERATION_BOOLEAN_EOR:
		SWITCH(LOOPC, FLOOPC, ^);
		break;

	case VIPS_OPERATION_BOOLEAN_LSHIFT:
		SWITCH(LOOPC, FLOOPC, <<);
		break;

	case VIPS_OPERATION_BOOLEAN_RSHIFT:
		SWITCH(LOOPC, FLOOPC, >>);
		break;

	default:
		g_assert_not_reached();
	}
}

static void
vips_boolean_const_class_init(VipsBooleanConstClass *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(class);
	VipsObjectClass *object_class = (VipsObjectClass *) class;
	VipsArithmeticClass *aclass = VIPS_ARITHMETIC_CLASS(class);

	gobject_class->set_property = vips_object_set_property;
	gobject_class->get_property = vips_object_get_property;

	object_class->nickname = "boolean_const";
	object_class->description =
		_("boolean operations against a constant");
	object_class->build = vips_boolean_const_build;

	aclass->process_line = vips_boolean_const_buffer;

	vips_arithmetic_set_format_table(aclass, vips_boolean_format_table);

	VIPS_ARG_ENUM(class, "boolean", 200,
		_("Operation"),
		_("Boolean to perform"),
		VIPS_ARGUMENT_REQUIRED_INPUT,
		G_STRUCT_OFFSET(VipsBooleanConst, operation),
		VIPS_TYPE_OPERATION_BOOLEAN,
		VIPS_OPERATION_BOOLEAN_AND);
}

static void
vips_boolean_const_init(VipsBooleanConst *boolean_const)
{
}

static int
vips_boolean_constv(VipsImage *in, VipsImage **out,
	VipsOperationBoolean operation, const double *c, int n, va_list ap)
{
	VipsArea *area_c;
	double *array;
	int result;
	int i;

	area_c = vips_area_new_array(G_TYPE_DOUBLE, sizeof(double), n);
	array = (double *) area_c->data;
	for (i = 0; i < n; i++)
		array[i] = c[i];

	result = vips_call_split("boolean_const", ap,
		in, out, operation, area_c);

	vips_area_unref(area_c);

	return result;
}

/**
 * vips_boolean_const: (method)
 * @in: input image
 * @out: (out): output image
 * @boolean: boolean operation to perform
 * @c: (array length=n): array of constants
 * @n: number of constants in @c
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Perform various boolean operations on an image against an array of
 * constants.
 *
 * The output type is always uchar, with 0 for `FALSE` and 255 for `TRUE`.
 *
 * If the array of constants has just one element, that constant is used for
 * all image bands. If the array has more than one element and they have
 * the same number of elements as there are bands in the image, then
 * one array element is used for each band. If the arrays have more than one
 * element and the image only has a single band, the result is a many-band
 * image where each band corresponds to one array element.
 *
 * ::: seealso
 *     [method@Image.boolean], [method@Image.boolean_const1].
 *
 * Returns: 0 on success, -1 on error
 */
int
vips_boolean_const(VipsImage *in, VipsImage **out,
	VipsOperationBoolean boolean, const double *c, int n, ...)
{
	va_list ap;
	int result;

	va_start(ap, n);
	result = vips_boolean_constv(in, out, boolean, c, n, ap);
	va_end(ap);

	return result;
}

/**
 * vips_andimage_const: (method)
 * @in: input image
 * @out: (out): output image
 * @c: (array length=n): array of constants
 * @n: number of constants in @c
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Perform [enum@Vips.OperationBoolean.AND] on an image and an array of constants.
 * See [method@Image.boolean_const].
 *
 * ::: seealso
 *     [method@Image.boolean], [method@Image.boolean_const1].
 *
 * Returns: 0 on success, -1 on error
 */
int
vips_andimage_const(VipsImage *in, VipsImage **out,
	const double *c, int n, ...)
{
	va_list ap;
	int result;

	va_start(ap, n);
	result = vips_boolean_constv(in, out,
		VIPS_OPERATION_BOOLEAN_AND, c, n, ap);
	va_end(ap);

	return result;
}

/**
 * vips_orimage_const: (method)
 * @in: input image
 * @out: (out): output image
 * @c: (array length=n): array of constants
 * @n: number of constants in @c
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Perform [enum@Vips.OperationBoolean.OR] on an image and an array of constants.
 * See [method@Image.boolean_const].
 *
 * ::: seealso
 *     [method@Image.boolean], [method@Image.boolean_const1].
 *
 * Returns: 0 on success, -1 on error
 */
int
vips_orimage_const(VipsImage *in, VipsImage **out,
	const double *c, int n, ...)
{
	va_list ap;
	int result;

	va_start(ap, n);
	result = vips_boolean_constv(in, out,
		VIPS_OPERATION_BOOLEAN_OR, c, n, ap);
	va_end(ap);

	return result;
}

/**
 * vips_eorimage_const: (method)
 * @in: input image
 * @out: (out): output image
 * @c: (array length=n): array of constants
 * @n: number of constants in @c
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Perform [enum@Vips.OperationBoolean.EOR] on an image and an array of constants.
 * See [method@Image.boolean_const].
 *
 * ::: seealso
 *     [method@Image.boolean], [method@Image.boolean_const1].
 *
 * Returns: 0 on success, -1 on error
 */
int
vips_eorimage_const(VipsImage *in, VipsImage **out,
	const double *c, int n, ...)
{
	va_list ap;
	int result;

	va_start(ap, n);
	result = vips_boolean_constv(in, out,
		VIPS_OPERATION_BOOLEAN_EOR, c, n, ap);
	va_end(ap);

	return result;
}

/**
 * vips_lshift_const: (method)
 * @in: input image
 * @out: (out): output image
 * @c: (array length=n): array of constants
 * @n: number of constants in @c
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Perform [enum@Vips.OperationBoolean.LSHIFT] on an image and an array of constants.
 * See [method@Image.boolean_const].
 *
 * ::: seealso
 *     [method@Image.boolean], [method@Image.boolean_const1].
 *
 * Returns: 0 on success, -1 on error
 */
int
vips_lshift_const(VipsImage *in, VipsImage **out, const double *c, int n, ...)
{
	va_list ap;
	int result;

	va_start(ap, n);
	result = vips_boolean_constv(in, out,
		VIPS_OPERATION_BOOLEAN_LSHIFT, c, n, ap);
	va_end(ap);

	return result;
}

/**
 * vips_rshift_const: (method)
 * @in: input image
 * @out: (out): output image
 * @c: (array length=n): array of constants
 * @n: number of constants in @c
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Perform [enum@Vips.OperationBoolean.LSHIFT] on an image and an array of constants.
 * See [method@Image.boolean_const].
 *
 * ::: seealso
 *     [method@Image.boolean], [method@Image.boolean_const1].
 *
 * Returns: 0 on success, -1 on error
 */
int
vips_rshift_const(VipsImage *in, VipsImage **out, const double *c, int n, ...)
{
	va_list ap;
	int result;

	va_start(ap, n);
	result = vips_boolean_constv(in, out,
		VIPS_OPERATION_BOOLEAN_RSHIFT, c, n, ap);
	va_end(ap);

	return result;
}

/**
 * vips_boolean_const1: (method)
 * @in: input image
 * @out: (out): output image
 * @boolean: boolean operation to perform
 * @c: constant
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Perform various boolean operations on an image with a single constant. See
 * [method@Image.boolean_const].
 *
 * ::: seealso
 *     [method@Image.boolean], [method@Image.boolean_const].
 *
 * Returns: 0 on success, -1 on error
 */
int
vips_boolean_const1(VipsImage *in, VipsImage **out,
	VipsOperationBoolean boolean, double c, ...)
{
	va_list ap;
	int result;

	va_start(ap, c);
	result = vips_boolean_constv(in, out, boolean, &c, 1, ap);
	va_end(ap);

	return result;
}

/**
 * vips_andimage_const1: (method)
 * @in: input image
 * @out: (out): output image
 * @c: constant
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Perform [enum@Vips.OperationBoolean.AND] on an image and a constant.
 * See [method@Image.boolean_const1].
 *
 * ::: seealso
 *     [method@Image.boolean], [method@Image.boolean_const].
 *
 * Returns: 0 on success, -1 on error
 */
int
vips_andimage_const1(VipsImage *in, VipsImage **out, double c, ...)
{
	va_list ap;
	int result;

	va_start(ap, c);
	result = vips_boolean_constv(in, out,
		VIPS_OPERATION_BOOLEAN_AND, &c, 1, ap);
	va_end(ap);

	return result;
}

/**
 * vips_orimage_const1: (method)
 * @in: input image
 * @out: (out): output image
 * @c: constant
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Perform [enum@Vips.OperationBoolean.OR] on an image and a constant.
 * See [method@Image.boolean_const1].
 *
 * ::: seealso
 *     [method@Image.boolean], [method@Image.boolean_const].
 *
 * Returns: 0 on success, -1 on error
 */
int
vips_orimage_const1(VipsImage *in, VipsImage **out, double c, ...)
{
	va_list ap;
	int result;

	va_start(ap, c);
	result = vips_boolean_constv(in, out,
		VIPS_OPERATION_BOOLEAN_OR, &c, 1, ap);
	va_end(ap);

	return result;
}

/**
 * vips_eorimage_const1: (method)
 * @in: input image
 * @out: (out): output image
 * @c: constant
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Perform [enum@Vips.OperationBoolean.EOR] on an image and a constant.
 * See [method@Image.boolean_const1].
 *
 * ::: seealso
 *     [method@Image.boolean], [method@Image.boolean_const].
 *
 * Returns: 0 on success, -1 on error
 */
int
vips_eorimage_const1(VipsImage *in, VipsImage **out, double c, ...)
{
	va_list ap;
	int result;

	va_start(ap, c);
	result = vips_boolean_constv(in, out,
		VIPS_OPERATION_BOOLEAN_EOR, &c, 1, ap);
	va_end(ap);

	return result;
}

/**
 * vips_lshift_const1: (method)
 * @in: input image
 * @out: (out): output image
 * @c: constant
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Perform [enum@Vips.OperationBoolean.LSHIFT] on an image and a constant.
 * See [method@Image.boolean_const1].
 *
 * ::: seealso
 *     [method@Image.boolean], [method@Image.boolean_const].
 *
 * Returns: 0 on success, -1 on error
 */
int
vips_lshift_const1(VipsImage *in, VipsImage **out, double c, ...)
{
	va_list ap;
	int result;

	va_start(ap, c);
	result = vips_boolean_constv(in, out,
		VIPS_OPERATION_BOOLEAN_LSHIFT, &c, 1, ap);
	va_end(ap);

	return result;
}

/**
 * vips_rshift_const1: (method)
 * @in: input image
 * @out: (out): output image
 * @c: constant
 * @...: `NULL`-terminated list of optional named arguments
 *
 * Perform [enum@Vips.OperationBoolean.RSHIFT] on an image and a constant.
 * See [method@Image.boolean_const1].
 *
 * ::: seealso
 *     [method@Image.boolean], [method@Image.boolean_const].
 *
 * Returns: 0 on success, -1 on error
 */
int
vips_rshift_const1(VipsImage *in, VipsImage **out, double c, ...)
{
	va_list ap;
	int result;

	va_start(ap, c);
	result = vips_boolean_constv(in, out,
		VIPS_OPERATION_BOOLEAN_RSHIFT, &c, 1, ap);
	va_end(ap);

	return result;
}
