/*
 * Server-side file mapping management
 *
 * Copyright (C) 1999 Alexandre Julliard
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "config.h"
#include "winnt.h"
#include "winbase.h"

#include "handle.h"
#include "thread.h"
#include "request.h"

struct mapping
{
    struct object  obj;             /* object header */
    int            size_high;       /* mapping size */
    int            size_low;        /* mapping size */
    int            protect;         /* protection flags */
    struct file   *file;            /* file mapped */
    int            header_size;     /* size of headers (for PE image mapping) */
    void          *base;            /* default base addr (for PE image mapping) */
    struct file   *shared_file;     /* temp file for shared PE mapping */
    int            shared_size;     /* shared mapping total size */
};

static void mapping_dump( struct object *obj, int verbose );
static void mapping_destroy( struct object *obj );

static const struct object_ops mapping_ops =
{
    sizeof(struct mapping),      /* size */
    mapping_dump,                /* dump */
    no_add_queue,                /* add_queue */
    NULL,                        /* remove_queue */
    NULL,                        /* signaled */
    NULL,                        /* satisfied */
    NULL,                        /* get_poll_events */
    NULL,                        /* poll_event */
    no_read_fd,                  /* get_read_fd */
    no_write_fd,                 /* get_write_fd */
    no_flush,                    /* flush */
    no_get_file_info,            /* get_file_info */
    mapping_destroy              /* destroy */
};

#ifdef __i386__

/* These are always the same on an i386, and it will be faster this way */
# define page_mask  0xfff
# define page_shift 12
# define init_page_size() /* nothing */

#else  /* __i386__ */

static int page_shift, page_mask;

static void init_page_size(void)
{
    int page_size;
# ifdef HAVE_GETPAGESIZE
    page_size = getpagesize();
# else
#  ifdef __svr4__
    page_size = sysconf(_SC_PAGESIZE);
#  else
#   error Cannot get the page size on this platform
#  endif
# endif
    page_mask = page_size - 1;
    /* Make sure we have a power of 2 */
    assert( !(page_size & page_mask) );
    page_shift = 0;
    while ((1 << page_shift) != page_size) page_shift++;
}
#endif  /* __i386__ */

#define ROUND_ADDR(addr) \
   ((int)(addr) & ~page_mask)

#define ROUND_SIZE(addr,size) \
   (((int)(size) + ((int)(addr) & page_mask) + page_mask) & ~page_mask)


/* allocate and fill the temp file for a shared PE image mapping */
static int build_shared_mapping( struct mapping *mapping, int fd,
                                 IMAGE_SECTION_HEADER *sec, int nb_sec )
{
    int i, max_size, total_size, pos;
    char *buffer = NULL; 
    int shared_fd = -1;

    /* compute the total size of the shared mapping */

    total_size = max_size = 0;
    for (i = 0; i < nb_sec; i++)
    {
        if ((sec[i].Characteristics & IMAGE_SCN_MEM_SHARED) &&
            (sec[i].Characteristics & IMAGE_SCN_MEM_WRITE))
        {
            int size = ROUND_SIZE( 0, sec[i].Misc.VirtualSize );
            if (size > max_size) max_size = size;
            total_size += size;
        }
    }
    if (!(mapping->shared_size = total_size)) return 1;  /* nothing to do */

    /* create a temp file for the mapping */

    if (!(mapping->shared_file = create_temp_file( GENERIC_READ|GENERIC_WRITE ))) goto error;
    if (!grow_file( mapping->shared_file, 0, total_size )) goto error;
    if ((shared_fd = file_get_mmap_fd( mapping->shared_file )) == -1) goto error;

    if (!(buffer = malloc( max_size ))) goto error;

    /* copy the shared sections data into the temp file */

    for (i = pos = 0; i < nb_sec; i++)
    {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_SHARED)) continue;
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
        if (lseek( shared_fd, pos, SEEK_SET ) != pos) goto error;
        pos += ROUND_SIZE( 0, sec[i].Misc.VirtualSize );
    	if (sec->Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) continue;
        if (!sec->PointerToRawData || !sec->SizeOfRawData) continue;
        if (lseek( fd, sec[i].PointerToRawData, SEEK_SET ) != sec[i].PointerToRawData) goto error;
        if (read( fd, buffer, sec[i].SizeOfRawData ) != sec[i].SizeOfRawData) goto error;
        if (write( shared_fd, buffer, sec[i].SizeOfRawData ) != sec[i].SizeOfRawData) goto error;
    }
    close( shared_fd );
    free( buffer );
    return 1;

 error:
    if (shared_fd != -1) close( shared_fd );
    if (buffer) free( buffer );
    return 0;
}

/* retrieve the mapping parameters for an executable (PE) image */
static int get_image_params( struct mapping *mapping )
{
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS nt;
    IMAGE_SECTION_HEADER *sec = NULL;
    int fd, filepos, size;

    /* load the headers */

    if ((fd = file_get_mmap_fd( mapping->file )) == -1) return 0;
    filepos = lseek( fd, 0, SEEK_SET );
    if (read( fd, &dos, sizeof(dos) ) != sizeof(dos)) goto error;
    if (dos.e_magic != IMAGE_DOS_SIGNATURE) goto error;
    if (lseek( fd, dos.e_lfanew, SEEK_SET ) == -1) goto error;
    if (read( fd, &nt, sizeof(nt) ) != sizeof(nt)) goto error;
    if (nt.Signature != IMAGE_NT_SIGNATURE) goto error;

    /* load the section headers */

    size = sizeof(*sec) * nt.FileHeader.NumberOfSections;
    if (!(sec = malloc( size ))) goto error;
    if (read( fd, sec, size ) != size) goto error;

    if (!build_shared_mapping( mapping, fd, sec, nt.FileHeader.NumberOfSections )) goto error;

    mapping->size_low    = ROUND_SIZE( 0, nt.OptionalHeader.SizeOfImage );
    mapping->size_high   = 0;
    mapping->base        = (void *)nt.OptionalHeader.ImageBase;
    mapping->header_size = ROUND_SIZE( mapping->base, nt.OptionalHeader.SizeOfHeaders );
    mapping->protect     = VPROT_IMAGE;

    /* sanity check */
    if (mapping->header_size > mapping->size_low) goto error;

    lseek( fd, filepos, SEEK_SET );
    close( fd );
    free( sec );
    return 1;

 error:
    lseek( fd, filepos, SEEK_SET );
    close( fd );
    if (sec) free( sec );
    set_error( STATUS_INVALID_FILE_FOR_SECTION );
    return 0;
}


static struct object *create_mapping( int size_high, int size_low, int protect,
                                      int handle, const WCHAR *name, size_t len )
{
    struct mapping *mapping;
    int access = 0;

    if (!page_mask) init_page_size();

    if (!(mapping = create_named_object( &mapping_ops, name, len )))
        return NULL;
    if (get_error() == STATUS_OBJECT_NAME_COLLISION)
        return &mapping->obj;  /* Nothing else to do */

    mapping->header_size = 0;
    mapping->base        = NULL;
    mapping->shared_file = NULL;
    mapping->shared_size = 0;

    if (protect & VPROT_READ) access |= GENERIC_READ;
    if (protect & VPROT_WRITE) access |= GENERIC_WRITE;

    if (handle != -1)
    {
        if (!(mapping->file = get_file_obj( current->process, handle, access ))) goto error;
        if (protect & VPROT_IMAGE)
        {
            if (!get_image_params( mapping )) goto error;
            return &mapping->obj;
        }
        if (!size_high && !size_low)
        {
            struct get_file_info_request req;
            struct object *obj = (struct object *)mapping->file;
            obj->ops->get_file_info( obj, &req );
            size_high = req.size_high;
            size_low  = ROUND_SIZE( 0, req.size_low );
        }
        else if (!grow_file( mapping->file, size_high, size_low )) goto error;
    }
    else  /* Anonymous mapping (no associated file) */
    {
        if ((!size_high && !size_low) || (protect & VPROT_IMAGE))
        {
            set_error( STATUS_INVALID_PARAMETER );
            mapping->file = NULL;
            goto error;
        }
        if (!(mapping->file = create_temp_file( access ))) goto error;
        if (!grow_file( mapping->file, size_high, size_low )) goto error;
    }
    mapping->size_high = size_high;
    mapping->size_low  = ROUND_SIZE( 0, size_low );
    mapping->protect   = protect;
    return &mapping->obj;

 error:
    release_object( mapping );
    return NULL;
}

static void mapping_dump( struct object *obj, int verbose )
{
    struct mapping *mapping = (struct mapping *)obj;
    assert( obj->ops == &mapping_ops );
    fprintf( stderr, "Mapping size=%08x%08x prot=%08x file=%p header_size=%08x base=%p "
             "shared_file=%p shared_size=%08x ",
             mapping->size_high, mapping->size_low, mapping->protect, mapping->file,
             mapping->header_size, mapping->base, mapping->shared_file, mapping->shared_size );
    dump_object_name( &mapping->obj );
    fputc( '\n', stderr );
}

static void mapping_destroy( struct object *obj )
{
    struct mapping *mapping = (struct mapping *)obj;
    assert( obj->ops == &mapping_ops );
    if (mapping->file) release_object( mapping->file );
    if (mapping->shared_file) release_object( mapping->shared_file );
}

int get_page_size(void)
{
    if (!page_mask) init_page_size();
    return page_mask + 1;
}

/* create a file mapping */
DECL_HANDLER(create_mapping)
{
    struct object *obj;

    req->handle = -1;
    if ((obj = create_mapping( req->size_high, req->size_low,
                               req->protect, req->file_handle,
                               get_req_data(req), get_req_data_size(req) )))
    {
        int access = FILE_MAP_ALL_ACCESS;
        if (!(req->protect & VPROT_WRITE)) access &= ~FILE_MAP_WRITE;
        req->handle = alloc_handle( current->process, obj, access, req->inherit );
        release_object( obj );
    }
}

/* open a handle to a mapping */
DECL_HANDLER(open_mapping)
{
    req->handle = open_object( get_req_data(req), get_req_data_size(req),
                               &mapping_ops, req->access, req->inherit );
}

/* get a mapping information */
DECL_HANDLER(get_mapping_info)
{
    struct mapping *mapping;

    if ((mapping = (struct mapping *)get_handle_obj( current->process, req->handle,
                                                     0, &mapping_ops )))
    {
        req->size_high   = mapping->size_high;
        req->size_low    = mapping->size_low;
        req->protect     = mapping->protect;
        req->header_size = mapping->header_size;
        req->base        = mapping->base;
        req->shared_file = -1;
        req->shared_size = mapping->shared_size;
        if (mapping->shared_file)
            req->shared_file = alloc_handle( current->process, mapping->shared_file,
                                             GENERIC_READ|GENERIC_WRITE, 0 );
        if (mapping->file) set_reply_fd( current, file_get_mmap_fd( mapping->file ) );
        release_object( mapping );
    }
}

