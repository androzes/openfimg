/*
 * fimg/system.c
 *
 * SAMSUNG S3C6410 FIMG-3DSE SYSTEM-DEVICE INTERFACE
 *
 * Copyrights:	2010 by Tomasz Figa <tomasz.figa@gmail.com>
 */

#include "fimg_private.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifndef WIN32
#include <sys/ioctl.h>
#include <sys/mman.h>
#else
#define ioctl(args...)	(0)
#define mmap(args...)	((void *)0)
#define MAP_FAILED	0
#define munmap(args...)
#endif
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <cutils/log.h>

int fimgFileDesc = -1;
int fimgMemFileDesc = -1;
volatile void *fimgBase = NULL;
unsigned int refCount = 0;

#define FIMG_SFR_BASE 0x72000000
#define FIMG_SFR_SIZE 0x80000

/*****************************************************************************/
#define G3D_IOCTL_MAGIC		'S'
#define S3C_3D_MEM_ALLOC	_IOWR(G3D_IOCTL_MAGIC, 310, struct s3c_3d_mem_alloc)
#define S3C_3D_MEM_FREE		_IOWR(G3D_IOCTL_MAGIC, 311, struct s3c_3d_mem_alloc)

struct s3c_3d_mem_alloc {
        int             size;
        unsigned int    vir_addr;
        unsigned int    phy_addr;
};
/*****************************************************************************/

/*****************************************************************************
 * FUNCTION:	fimgDeviceOpen
 * SYNOPSIS:	This function opens the 3D device and initializes global variables
 * RETURNS:	 0, on success
 *		-errno, on error
 *****************************************************************************/
int fimgDeviceOpen(void)
{
	int fd, memfd;

	fd = open("/dev/s3c-g3d", O_RDWR, 0);
	if(fd < 0) {
		LOGE("Couldn't open /dev/s3c-g3d (%s).", strerror(errno));
		return -errno;
	}

	memfd = open("/dev/mem", O_RDWR | O_SYNC, 0);
	if(memfd < 0) {
		LOGE("Couldn't open /dev/mem (%s).", strerror(errno));
		close(fd);
		return -errno;
	}

	fimgBase = mmap(NULL, FIMG_SFR_SIZE, PROT_WRITE | PROT_READ, MAP_SHARED, memfd, FIMG_SFR_BASE);
	if(fimgBase == MAP_FAILED) {
		LOGE("Couldn't mmap FIMG registers (%s).", strerror(errno));
		close(fd);
		close(memfd);
		return -errno;
	}

	LOGD("fimg3D: Opened /dev/s3c-g3d (%d) and /dev/mem (%d).", fd, memfd);
	fimgFileDesc = fd;
	fimgMemFileDesc = memfd;
	return 0;
}

/*****************************************************************************
 * FUNCTION:	fimgDeviceClose
 * SYNOPSIS:	This function closes the 3D device
 *****************************************************************************/
void fimgDeviceClose(void)
{
	if(fimgFileDesc < 0) {
		LOGW("fimg3D warning: Trying to close already closed device.");
		return;
	}

	munmap((void *)fimgBase, FIMG_SFR_SIZE);
	close(fimgFileDesc);
	close(fimgMemFileDesc);

	LOGD("fimg3D: Closed /dev/s3c-g3d (%d) and /dev/mem (%d).", fimgFileDesc, fimgMemFileDesc);

	fimgFileDesc = -1;
}

/*****************************************************************************
 * FUNCTION:	fimgAllocMemory
 * SYNOPSIS:	This function allocates a block of 3D memory
 * PARAMETERS:	[in]  size - requested block size
 * 		[out] paddr - physical address
 * RETURNS:	virtual address of allocated block
 *****************************************************************************/
void *fimgAllocMemory(unsigned long *size, unsigned long *paddr)
{
	struct s3c_3d_mem_alloc mem;

	mem.size = *size;
	
	ioctl(fimgFileDesc, S3C_3D_MEM_ALLOC, &mem);

	LOGD("Allocated %d bytes of memory. (0x%08x @ 0x%08x)", mem.size, mem.vir_addr, mem.phy_addr);

	*paddr = mem.phy_addr;
	*size = mem.size;
	return (void *)mem.vir_addr;
}

/*****************************************************************************
 * FUNCTION:	fimgFreeMemory
 * SYNOPSIS:	This function frees allocated 3D memory block
 * PARAMETERS:	[in] vaddr - virtual address
 *		[in] paddr - physical address
 *		[in] size - size
 *****************************************************************************/
void fimgFreeMemory(void *vaddr, unsigned long paddr, unsigned long size)
{
	struct s3c_3d_mem_alloc mem;

	mem.vir_addr = (unsigned int)vaddr;
	mem.phy_addr = paddr;
	mem.size = size;

	LOGD("Freed %d bytes of memory. (0x%08x @ 0x%08x)", mem.size, mem.vir_addr, mem.phy_addr);

	ioctl(fimgFileDesc, S3C_3D_MEM_FREE, &mem);
}

fimgContext *fimgCreateContext(void)
{
	fimgContext *ctx = malloc(sizeof(*ctx));
	int i;

	if(!ctx)
		return NULL;

	memset(ctx, 0, sizeof(fimgContext));

	fimgCreateGlobalContext(ctx);
	fimgCreateHostContext(ctx);
	fimgCreatePrimitiveContext(ctx);
	fimgCreateRasterizerContext(ctx);
	fimgCreateFragmentContext(ctx);

	ctx->numAttribs = 0;

	if(!refCount++) {
		if(fimgDeviceOpen()) {
			free(ctx);
			refCount--;
			return NULL;
		}
	}

	return ctx;
}

void fimgDestroyContext(fimgContext *ctx)
{
	free(ctx);

	if(!--refCount)
		fimgDeviceClose();
}

void fimgRestoreContext(fimgContext *ctx)
{
	fimgRestoreGlobalState(ctx);
	fimgRestoreHostState(ctx);
	fimgRestorePrimitiveState(ctx);
	fimgRestoreRasterizerState(ctx);
	fimgRestoreFragmentState(ctx);
}

/* TODO: Implement rest of kernel driver functions */
