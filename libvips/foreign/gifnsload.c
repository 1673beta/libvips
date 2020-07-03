/* load a GIF with libnsgif
 *
 * 6/10/18
 * 	- from gifload.c
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

/*
#define VERBOSE
#define VIPS_DEBUG
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /*HAVE_CONFIG_H*/
#include <vips/intl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include <vips/vips.h>
#include <vips/buf.h>
#include <vips/internal.h>
#include <vips/debug.h>

/* TODO:
 *
 * - libnsgif does not seem to support comment metadata
 *
 * - dispose_previous is not handled correctly by libnsgif
 *
 * - it always loads the entire source file into memory
 *
 * Notes:
 *
 * - hard to detect mono images -- local_colour_table in libnsgif is only set
 *   when we decode a frame, so we can't tell just from init whether any
 *   frames
 *
 * - don't bother detecting alpha -- if we can't detect RGB, alpha won't help
 *   much
 *
 */

#ifdef HAVE_LIBNSGIF

#include <libnsgif/libnsgif.h>

#define VIPS_TYPE_FOREIGN_LOAD_GIF (vips_foreign_load_gifns_get_type())
#define VIPS_FOREIGN_LOAD_GIF( obj ) \
	(G_TYPE_CHECK_INSTANCE_CAST( (obj), \
	VIPS_TYPE_FOREIGN_LOAD_GIF, VipsForeignLoadGifns ))
#define VIPS_FOREIGN_LOAD_GIF_CLASS( klass ) \
	(G_TYPE_CHECK_CLASS_CAST( (klass), \
	VIPS_TYPE_FOREIGN_LOAD_GIF, VipsForeignLoadGifnsClass))
#define VIPS_IS_FOREIGN_LOAD_GIF( obj ) \
	(G_TYPE_CHECK_INSTANCE_TYPE( (obj), VIPS_TYPE_FOREIGN_LOAD_GIF ))
#define VIPS_IS_FOREIGN_LOAD_GIF_CLASS( klass ) \
	(G_TYPE_CHECK_CLASS_TYPE( (klass), VIPS_TYPE_FOREIGN_LOAD_GIF ))
#define VIPS_FOREIGN_LOAD_GIF_GET_CLASS( obj ) \
	(G_TYPE_INSTANCE_GET_CLASS( (obj), \
	VIPS_TYPE_FOREIGN_LOAD_GIF, VipsForeignLoadGifnsClass ))

typedef struct _VipsForeignLoadGifns {
	VipsForeignLoad parent_object;

	/* Load this page (frame number).
	 */
	int page;

	/* Load this many pages.
	 */
	int n;

	/* The animation created by libnsgif.
	 */
	gif_animation *anim;

	/* The data/size pair we pass to libnsgif.
	 */
	unsigned char *data;
	size_t size;

	/* The frame_count, after we have removed undisplayable frames.
	 */
	int frame_count_displayable;

	/* Delays between frames (in milliseconds). Array of length @n.
	 */
	int *delay;

	/* A single centisecond value for compatibility.
	 */
	int gif_delay;

} VipsForeignLoadGifns;

typedef struct _VipsForeignLoadGifnsClass {
	VipsForeignLoadClass parent_class;

	/* Close and reopen.
	 */
	int (*open)( VipsForeignLoadGifns *gif );

	/* Close any underlying file resource.
	 */
	void (*close)( VipsForeignLoadGifns *gif );
} VipsForeignLoadGifnsClass;

G_DEFINE_ABSTRACT_TYPE( VipsForeignLoadGifns, vips_foreign_load_gifns, 
	VIPS_TYPE_FOREIGN_LOAD );

static const char *
vips_foreign_load_gifns_errstr( gif_result result )
{
	switch( result ) {
		case GIF_WORKING:
		return( _( "Working" ) ); 

	case GIF_OK:
		return( _( "OK" ) ); 

	case GIF_INSUFFICIENT_FRAME_DATA:
		return( _( "Insufficient data to complete frame" ) ); 

	case GIF_FRAME_DATA_ERROR:
		return( _( "GIF frame data error" ) ); 

	case GIF_INSUFFICIENT_DATA:
		return( _( "Insufficient data to do anything" ) ); 

	case GIF_DATA_ERROR:
		return( _( "GIF header data error" ) ); 

	case GIF_INSUFFICIENT_MEMORY:
		return( _( "Insuficient memory to process" ) ); 

	case GIF_FRAME_NO_DISPLAY:
		return( _( "No display" ) ); 

	case GIF_END_OF_FRAME:
		return( _( "At end of frame" ) ); 

	default:
		return( _( "Unknown error" ) ); 
	}
}

static void
vips_foreign_load_gifns_error( VipsForeignLoadGifns *gif, gif_result result )
{
	VipsObjectClass *class = VIPS_OBJECT_GET_CLASS( gif );

	vips_error( class->nickname, "%s", 
		vips_foreign_load_gifns_errstr( result ) );
}

static void
vips_foreign_load_gifns_dispose( GObject *gobject )
{
	VipsForeignLoadGifns *gif = (VipsForeignLoadGifns *) gobject;
	VipsForeignLoadGifnsClass *class = 
		VIPS_FOREIGN_LOAD_GIF_GET_CLASS( gif );

	VIPS_DEBUG_MSG( "vips_foreign_load_gifns_dispose:\n" );

	class->close( gif );
	if( gif->anim ) {
		gif_finalise( gif->anim );
		VIPS_FREE( gif->anim );
	}
	VIPS_FREE( gif->delay );

	G_OBJECT_CLASS( vips_foreign_load_gifns_parent_class )->
		dispose( gobject );
}

static VipsForeignFlags
vips_foreign_load_gifns_get_flags_filename( const char *filename )
{
	return( VIPS_FOREIGN_SEQUENTIAL );
}

static VipsForeignFlags
vips_foreign_load_gifns_get_flags( VipsForeignLoad *load )
{
	return( VIPS_FOREIGN_SEQUENTIAL );
}

static gboolean
vips_foreign_load_gifns_is_a_buffer( const void *buf, size_t len )
{
	const guchar *str = (const guchar *) buf;

	VIPS_DEBUG_MSG( "vips_foreign_load_gifns_is_a_buffer:\n" );

	if( len >= 4 &&
		str[0] == 'G' && 
		str[1] == 'I' &&
		str[2] == 'F' &&
		str[3] == '8' )
		return( 1 );

	return( 0 );
}

static gboolean
vips_foreign_load_gifns_is_a( const char *filename )
{
	unsigned char buf[4];

	if( vips__get_bytes( filename, buf, 4 ) == 4 &&
		vips_foreign_load_gifns_is_a_buffer( buf, 4 ) )
		return( 1 );

	return( 0 );
}

#ifdef VERBOSE
static void
print_frame( gif_frame *frame )
{
	printf( "frame:\n" );
	printf( "  display = %d\n", frame->display );
	printf( "  frame_delay = %d\n", frame->frame_delay );
	printf( "  virgin = %d\n", frame->virgin );
	printf( "  opaque = %d\n", frame->opaque );
	printf( "  redraw_required = %d\n", frame->redraw_required );
	printf( "  disposal_method = %d\n", frame->disposal_method );
	printf( "  transparency = %d\n", frame->transparency );
	printf( "  transparency_index = %d\n", frame->transparency_index );
	printf( "  redraw_x = %d\n", frame->redraw_x );
	printf( "  redraw_y = %d\n", frame->redraw_y );
	printf( "  redraw_width = %d\n", frame->redraw_width );
	printf( "  redraw_height = %d\n", frame->redraw_height );
}

static void
print_animation( gif_animation *anim )
{
	int i;

	printf( "animation:\n" );
	printf( "  width = %d\n", anim->width );
	printf( "  height = %d\n", anim->height );
	printf( "  frame_count = %d\n", anim->frame_count );
	printf( "  frame_count_partial = %d\n", anim->frame_count_partial );
	printf( "  decoded_frame = %d\n", anim->decoded_frame );
	printf( "  frame_image = %p\n", anim->frame_image );
	printf( "  loop_count = %d\n", anim->loop_count );
	printf( "  frame_holders = %d\n", anim->frame_holders );
	printf( "  background_index = %d\n", anim->background_index );
	printf( "  colour_table_size = %d\n", anim->colour_table_size );
	printf( "  global_colours = %d\n", anim->global_colours );
	printf( "  global_colour_table = %p\n", anim->global_colour_table );
	printf( "  local_colour_table = %p\n", anim->local_colour_table );

	for( i = 0; i < anim->frame_holders; i++ ) {
		printf( "%d ", i );
		print_frame( &anim->frames[i] );
	}
}
#endif /*VERBOSE*/

static int
vips_foreign_load_gifns_set_header( VipsForeignLoadGifns *gif, 
	VipsImage *image )
{
	VIPS_DEBUG_MSG( "vips_foreign_load_gifns_set_header:\n" );

	vips_image_init_fields( image,
		gif->anim->width, gif->anim->height * gif->n, 4,
		VIPS_FORMAT_UCHAR, VIPS_CODING_NONE,
		VIPS_INTERPRETATION_sRGB, 1.0, 1.0 );
	vips_image_pipelinev( image, VIPS_DEMAND_STYLE_FATSTRIP, NULL );

	if( vips_object_argument_isset( VIPS_OBJECT( gif ), "n" ) )
		vips_image_set_int( image,
			VIPS_META_PAGE_HEIGHT, gif->anim->height );
	vips_image_set_int( image, VIPS_META_N_PAGES, 
		gif->frame_count_displayable );
	vips_image_set_int( image, "gif-loop", gif->anim->loop_count );

	vips_image_set_array_int( image, "delay", gif->delay, gif->n );

	/* The deprecated gif-delay field is in centiseconds.
	 */
	vips_image_set_int( image, "gif-delay", gif->gif_delay ); 

	return( 0 );
}

static int
vips_foreign_load_gifns_open( VipsForeignLoadGifns *gif )
{
	VipsObjectClass *class = VIPS_OBJECT_GET_CLASS( gif );
	VipsForeignLoad *load = (VipsForeignLoad *) gif;

	gif_result result;
	int i;

	VIPS_DEBUG_MSG( "vips_foreign_load_gifns_open: %p\n", gif );

	result = gif_initialise( gif->anim, gif->size, gif->data );
	VIPS_DEBUG_MSG( "gif_initialise() = %d\n", result );
#ifdef VERBOSE
	print_animation( gif->anim );
#endif /*VERBOSE*/
	if( result != GIF_OK && 
		result != GIF_WORKING &&
		result != GIF_INSUFFICIENT_FRAME_DATA ) {
		vips_foreign_load_gifns_error( gif, result ); 
		return( -1 );
	}
	else if( result == GIF_INSUFFICIENT_FRAME_DATA &&
		load->fail ) {
		vips_error( class->nickname, "%s", _( "truncated GIF" ) );
		return( -1 );
	}

	/* Many GIFs have dead frames at the end. Remove these from our count.
	 */
	for( i = gif->anim->frame_count - 1; 
		i >= 0 && !gif->anim->frames[i].display; i-- ) 
		;
	gif->frame_count_displayable = i + 1;
#ifdef VERBOSE
	if( gif->frame_count_displayable != gif->anim->frame_count )
		printf( "vips_foreign_load_gifns_open: "
			"removed %d undisplayable frames\n", 
			gif->anim->frame_count - gif->frame_count_displayable );
#endif /*VERBOSE*/

	if( !gif->frame_count_displayable ) {
		vips_error( class->nickname, "%s", _( "no frames in GIF" ) );
		return( -1 );
	}

	if( gif->n == -1 )
		gif->n = gif->frame_count_displayable - gif->page;

	if( gif->page < 0 ||
		gif->n <= 0 ||
		gif->page + gif->n > gif->frame_count_displayable ) {
		vips_error( class->nickname, "%s", _( "bad page number" ) );
		return( -1 );
	}

	/* In ms, frame_delay in cs.
	 */
	VIPS_FREE( gif->delay );
	if( !(gif->delay = VIPS_ARRAY( NULL, gif->n, int )) )
		return( -1 );
	for( i = 0; i < gif->n; i++ )
		gif->delay[i] = 
			10 * gif->anim->frames[gif->page + i].frame_delay;

	gif->gif_delay = gif->anim->frames[0].frame_delay;

	return( 0 );
}

static void
vips_foreign_load_gifns_close( VipsForeignLoadGifns *gif )
{
	/* Don't call gif_finalise(): that just frees memory attached to the
	 * gif struct, and gif_initialise() hates being called again after 
	 * that.
	 */
}

/* Scan the GIF as quickly as we can and extract transparency, bands, pages,
 * etc.
 *
 * Don't flag any errors unless we have to: we want to work for corrupt or
 * malformed GIFs.
 *
 * Close as soon as we can to free up the fd.
 */
static int
vips_foreign_load_gifns_header( VipsForeignLoad *load )
{
	VipsForeignLoadGifnsClass *class = 
		VIPS_FOREIGN_LOAD_GIF_GET_CLASS( load );
	VipsForeignLoadGifns *gif = (VipsForeignLoadGifns *) load;

	VIPS_DEBUG_MSG( "vips_foreign_load_gifns_header:\n" );

	if( class->open( gif ) ) {
		class->close( gif );
		return( -1 );
	}
	class->close( gif );

	vips_foreign_load_gifns_set_header( gif, load->out );

	return( 0 );
}

static void
vips_foreign_load_gifns_minimise( VipsObject *object, 
	VipsForeignLoadGifns *gif )
{
	VipsForeignLoadGifnsClass *class = 
		VIPS_FOREIGN_LOAD_GIF_GET_CLASS( gif );

	VIPS_DEBUG_MSG( "vips_foreign_load_gifns_minimise:\n" );

	class->close( gif );
}

static int
vips_foreign_load_gifns_generate( VipsRegion *or,
	void *seq, void *a, void *b, gboolean *stop )
{
        VipsRect *r = &or->valid;
	VipsForeignLoadGifns *gif = (VipsForeignLoadGifns *) a;

	int y;

#ifdef VERBOSE
	VIPS_DEBUG_MSG( "vips_foreign_load_gifns_generate: "
		"top = %d, height = %d\n", r->top, r->height );
#endif /*VERBOSE*/

	for( y = 0; y < r->height; y++ ) {
		/* The page for this output line, and the line number in page.
		 */
		int page = (r->top + y) / gif->anim->height + gif->page;
		int line = (r->top + y) % gif->anim->height;

		gif_result result;
		VipsPel *p, *q;

		g_assert( line >= 0 && line < gif->anim->height );
		g_assert( page >= 0 && page < gif->frame_count_displayable );

		if( gif->anim->decoded_frame != page ) {
			result = gif_decode_frame( gif->anim, page ); 
			VIPS_DEBUG_MSG( "  gif_decode_frame(%d) = %d\n", 
				page, result );
			if( result != GIF_OK ) {
				vips_foreign_load_gifns_error( gif, result ); 
				return( -1 );
			}
#ifdef VERBOSE
			print_animation( gif->anim );
#endif /*VERBOSE*/
		}

		p = gif->anim->frame_image + 
			line * gif->anim->width * sizeof( int );
		q = VIPS_REGION_ADDR( or, 0, r->top + y );
		memcpy( q, p, VIPS_REGION_SIZEOF_LINE( or ) );
	}

	return( 0 );
}

static int
vips_foreign_load_gifns_load( VipsForeignLoad *load )
{
	VipsForeignLoadGifns *gif = (VipsForeignLoadGifns *) load;
	VipsForeignLoadGifnsClass *class = 
		VIPS_FOREIGN_LOAD_GIF_GET_CLASS( gif );
	VipsImage **t = (VipsImage **)
		vips_object_local_array( VIPS_OBJECT( load ), 4 );

	VIPS_DEBUG_MSG( "vips_foreign_load_gifns_load:\n" );

	if( class->open( gif ) )
		return( -1 );

	/* Make the output pipeline.
	 */
	t[0] = vips_image_new();
	if( vips_foreign_load_gifns_set_header( gif, t[0] ) )
		return( -1 );

	/* Close immediately at end of read.
	 */
	g_signal_connect( t[0], "minimise", 
		G_CALLBACK( vips_foreign_load_gifns_minimise ), gif ); 

	/* Strips 8 pixels high to avoid too many tiny regions.
	 */
	if( vips_image_generate( t[0],
		NULL, vips_foreign_load_gifns_generate, NULL, gif, NULL ) ||
		vips_sequential( t[0], &t[1],
			"tile_height", VIPS__FATSTRIP_HEIGHT,
			NULL ) ||
		vips_image_write( t[1], load->real ) )
		return( -1 );

	return( 0 );
}

static void
vips_foreign_load_gifns_class_init( VipsForeignLoadGifnsClass *class )
{
	GObjectClass *gobject_class = G_OBJECT_CLASS( class );
	VipsObjectClass *object_class = (VipsObjectClass *) class;
	VipsForeignClass *foreign_class = (VipsForeignClass *) class;
	VipsForeignLoadClass *load_class = (VipsForeignLoadClass *) class;

	gobject_class->dispose = vips_foreign_load_gifns_dispose;
	gobject_class->set_property = vips_object_set_property;
	gobject_class->get_property = vips_object_get_property;

	object_class->nickname = "gifnsload_base";
	object_class->description = _( "load GIF with libnsgif" );

	/* High priority, so that we handle vipsheader etc.
	 */
	foreign_class->priority = 50;

	load_class->get_flags_filename = 
		vips_foreign_load_gifns_get_flags_filename;
	load_class->get_flags = vips_foreign_load_gifns_get_flags;
	load_class->header = vips_foreign_load_gifns_header;
	load_class->load = vips_foreign_load_gifns_load;

	class->open = vips_foreign_load_gifns_open;
	class->close = vips_foreign_load_gifns_close;

	VIPS_ARG_INT( class, "page", 10,
		_( "Page" ),
		_( "Load this page from the file" ),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET( VipsForeignLoadGifns, page ),
		0, 100000, 0 );

	VIPS_ARG_INT( class, "n", 6,
		_( "n" ),
		_( "Load this many pages" ),
		VIPS_ARGUMENT_OPTIONAL_INPUT,
		G_STRUCT_OFFSET( VipsForeignLoadGifns, n ),
		-1, 100000, 1 );

}

static void *
vips_foreign_load_gifns_bitmap_create( int width, int height )
{
        /* ensure a stupidly large bitmap is not created 
	 */

        return calloc( width * height, 4 );
}

static void 
vips_foreign_load_gifns_bitmap_set_opaque( void *bitmap, bool opaque )
{
        (void) opaque;  /* unused */
        (void) bitmap;  /* unused */
        g_assert( bitmap );
}

static bool 
vips_foreign_load_gifns_bitmap_test_opaque( void *bitmap )
{
        (void) bitmap;  /* unused */
        g_assert( bitmap );

        return( false );
}

static unsigned char *
vips_foreign_load_gifns_bitmap_get_buffer( void *bitmap )
{
        g_assert( bitmap );

        return( bitmap );
}

static void 
vips_foreign_load_gifns_bitmap_destroy( void *bitmap )
{
        g_assert( bitmap );
        free( bitmap );
}

static void 
vips_foreign_load_gifns_bitmap_modified( void *bitmap )
{
        (void) bitmap;  /* unused */
        g_assert( bitmap );

        return;
}

static gif_bitmap_callback_vt vips_foreign_load_gifns_bitmap_callbacks = {
	vips_foreign_load_gifns_bitmap_create,
	vips_foreign_load_gifns_bitmap_destroy,
	vips_foreign_load_gifns_bitmap_get_buffer,
	vips_foreign_load_gifns_bitmap_set_opaque,
	vips_foreign_load_gifns_bitmap_test_opaque,
	vips_foreign_load_gifns_bitmap_modified
};

static void
vips_foreign_load_gifns_init( VipsForeignLoadGifns *gif )
{
	gif->anim = g_new0( gif_animation, 1 );
	gif_create( gif->anim, &vips_foreign_load_gifns_bitmap_callbacks );
	gif->n = 1;
}

typedef struct _VipsForeignLoadGifnsFile {
	VipsForeignLoadGifns parent_object;

	/* Filename for load.
	 */
	char *filename; 

	/* mmap here.
	 */
	int fd;
	void *base;
	gint64 length;

} VipsForeignLoadGifnsFile;

typedef VipsForeignLoadGifnsClass VipsForeignLoadGifnsFileClass;

G_DEFINE_TYPE( VipsForeignLoadGifnsFile, vips_foreign_load_gifns_file, 
	vips_foreign_load_gifns_get_type() );

static const char *vips_foreign_gifns_suffs[] = {
	".gif",
	NULL
};

static int
vips_foreign_load_gifns_file_header( VipsForeignLoad *load )
{
	VipsForeignLoadGifnsFile *file = (VipsForeignLoadGifnsFile *) load;

	VIPS_DEBUG_MSG( "vips_foreign_load_gifns_file_header:\n" );

	VIPS_SETSTR( load->out->filename, file->filename );

	return( VIPS_FOREIGN_LOAD_CLASS(
		vips_foreign_load_gifns_file_parent_class )->header( load ) );
}

static int
vips_foreign_load_gifns_file_open( VipsForeignLoadGifns *gif )
{
	VipsObjectClass *class = VIPS_OBJECT_GET_CLASS( gif );
	VipsForeignLoadGifnsFile *file = (VipsForeignLoadGifnsFile *) gif;

	VIPS_DEBUG_MSG( "vips_foreign_load_gifns_file_open:\n" );

	if( file->fd == -1 ) {
		struct stat st;
		mode_t m;

		g_assert( gif->filename );
		g_assert( !gif->data );
		g_assert( !gif->size );

		file->fd = vips__open_read( file->filename );
		if( file->fd == -1 ) {
			vips_error_system( errno, class->nickname, 
				_( "unable to open file \"%s\" for reading" ), 
				file->filename );
			return( -1 );
		}
		if( (file->length = vips_file_length( file->fd )) == -1 )
			return( -1 );

		if( fstat( file->fd, &st ) == -1 ) {
			vips_error( class->nickname, 
				"%s", _( "unable to get file status" ) );
			return( -1 );
		}
		m = (mode_t) st.st_mode;
		if( !S_ISREG( m ) ) {
			vips_error( class->nickname, 
				"%s", _( "not a regular file" ) ); 
			return( -1 ); 
		}

		if( !(file->base = vips__mmap( file->fd, 0, file->length, 0 )) )
			return( -1 );

		gif->data = file->base;
		gif->size = file->length;
	}

	return( VIPS_FOREIGN_LOAD_GIF_CLASS(
		vips_foreign_load_gifns_file_parent_class )->open( gif ) );
}

static void
vips_foreign_load_gifns_file_close( VipsForeignLoadGifns *gif )
{
	VipsForeignLoadGifnsFile *file = (VipsForeignLoadGifnsFile *) gif;

	VIPS_DEBUG_MSG( "vips_foreign_load_gifns_file_close:\n" );

	if( file->fd >= 0 ) {
		(void) g_close( file->fd, NULL );
		file->fd  = -1;
	}

	if( file->base ) {
		vips__munmap( file->base, file->length );
		file->base = NULL;
		file->length = 0;
	}

	VIPS_FOREIGN_LOAD_GIF_CLASS(
		vips_foreign_load_gifns_file_parent_class )->close( gif );
}

static void
vips_foreign_load_gifns_file_class_init( 
	VipsForeignLoadGifnsFileClass *class )
{
	GObjectClass *gobject_class = G_OBJECT_CLASS( class );
	VipsObjectClass *object_class = (VipsObjectClass *) class;
	VipsForeignClass *foreign_class = (VipsForeignClass *) class;
	VipsForeignLoadClass *load_class = (VipsForeignLoadClass *) class;

	gobject_class->set_property = vips_object_set_property;
	gobject_class->get_property = vips_object_get_property;

	object_class->nickname = "gifnsload";
	object_class->description = _( "load GIF with libnsgif" );

	foreign_class->suffs = vips_foreign_gifns_suffs;

	load_class->is_a = vips_foreign_load_gifns_is_a;
	load_class->header = vips_foreign_load_gifns_file_header;

	class->open = vips_foreign_load_gifns_file_open;
	class->close = vips_foreign_load_gifns_file_close;

	VIPS_ARG_STRING( class, "filename", 1, 
		_( "Filename" ),
		_( "Filename to load from" ),
		VIPS_ARGUMENT_REQUIRED_INPUT, 
		G_STRUCT_OFFSET( VipsForeignLoadGifnsFile, filename ),
		NULL );

}

static void
vips_foreign_load_gifns_file_init( VipsForeignLoadGifnsFile *file )
{
	file->fd = -1;
}

typedef struct _VipsForeignLoadGifnsBuffer {
	VipsForeignLoadGifns parent_object;

	/* Load from a buffer.
	 */
	VipsArea *buf;

	/* Current read point, bytes left in buffer.
	 */
	VipsPel *p;
	size_t bytes_to_go;

} VipsForeignLoadGifnsBuffer;

typedef VipsForeignLoadGifnsClass VipsForeignLoadGifnsBufferClass;

G_DEFINE_TYPE( VipsForeignLoadGifnsBuffer, vips_foreign_load_gifns_buffer, 
	vips_foreign_load_gifns_get_type() );

static int
vips_foreign_load_gifns_buffer_header( VipsForeignLoad *load )
{
	VipsForeignLoadGifns *gif = (VipsForeignLoadGifns *) load;
	VipsForeignLoadGifnsBuffer *buffer = 
		(VipsForeignLoadGifnsBuffer *) load;

	VIPS_DEBUG_MSG( "vips_foreign_load_gifns_buffer_header:\n" );

	gif->data = buffer->buf->data;
	gif->size = buffer->buf->length;

	return( VIPS_FOREIGN_LOAD_CLASS(
		vips_foreign_load_gifns_buffer_parent_class )->header( load ) );
}

static void
vips_foreign_load_gifns_buffer_class_init( 
	VipsForeignLoadGifnsBufferClass *class )
{
	GObjectClass *gobject_class = G_OBJECT_CLASS( class );
	VipsObjectClass *object_class = (VipsObjectClass *) class;
	VipsForeignLoadClass *load_class = (VipsForeignLoadClass *) class;

	gobject_class->set_property = vips_object_set_property;
	gobject_class->get_property = vips_object_get_property;

	object_class->nickname = "gifnsload_buffer";
	object_class->description = _( "load GIF with libnsgif" );

	load_class->is_a_buffer = vips_foreign_load_gifns_is_a_buffer;
	load_class->header = vips_foreign_load_gifns_buffer_header;

	VIPS_ARG_BOXED( class, "buffer", 1, 
		_( "Buffer" ),
		_( "Buffer to load from" ),
		VIPS_ARGUMENT_REQUIRED_INPUT, 
		G_STRUCT_OFFSET( VipsForeignLoadGifnsBuffer, buf ),
		VIPS_TYPE_BLOB );

}

static void
vips_foreign_load_gifns_buffer_init( VipsForeignLoadGifnsBuffer *buffer )
{
}

#endif /*HAVE_LIBNSGIF*/
