#include <stddef.h> /* NULL, size_t */
#include <string.h> /* memset */

#define PAGE_SIZE 4096

#ifdef __cplusplus
extern "C" {
#endif

extern void* malloc(size_t size);
extern void* memalign(size_t alignment, size_t size);

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
	void *out;

	if(memptr == NULL) {
		return 22;
	}

	if((alignment % sizeof(void*)) != 0) {
		return 22;
	}

	/* if not power of two */
	if(!((alignment != 0) && !(alignment & (alignment - 1)))) {
		return 22;
	}

	if(size == 0) {
		*memptr = NULL;
		return 0;
	}

	out = memalign(alignment, size);
	if(out == NULL) {
		return 12;
	}

	*memptr = out;
	return 0;
}

void* calloc(size_t nmemb, size_t size)
{
	void *out;
	size_t fullsize = nmemb * size;

	if((size != 0) && ((fullsize / size) != nmemb)) {
		return NULL;
	}

	out = malloc(fullsize);
	if(out == NULL) {
		return NULL;
	}

	memset(out, 0, fullsize);
	return out;
}

void* valloc(size_t size)
{
	return memalign(PAGE_SIZE, size);
}

void* pvalloc(size_t size)
{
	size_t ps, rem, allocsize;

	ps = PAGE_SIZE;
	rem = size % ps;
	allocsize = size;
	if(rem != 0) {
		allocsize = ps + (size - rem);
	}

	return memalign(ps, allocsize);
}

void* aligned_alloc(size_t alignment, size_t size)
{
	if(alignment > size) {
		return NULL;
	}

	if((size % alignment) != 0) {
		return NULL;
	}

	return memalign(alignment, size);
}

#ifdef __cplusplus
}
#endif
