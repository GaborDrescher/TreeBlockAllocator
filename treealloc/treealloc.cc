#ifndef _GNU_SOURCE
#	define _GNU_SOURCE
#endif

//#define MEASURE_TIME
//#define MORE_DEBUG

#include <unistd.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <stddef.h>
#include <inttypes.h>
#include <string.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef MEASURE_TIME
#	include <time.h>
#endif

#include "TreeBlockAllocator.h"
#include "WrapperAllocator.h"

extern "C" {
	void* malloc(size_t size);
	void* memalign(size_t alignment, size_t size);
	void* realloc(void *ptr, size_t size);
	void  free(void *ptr);
}

#define PAGE_SIZE (4096)
#ifdef cf_debug_kernel
#define ARCH_BLOCK_BITS ((sizeof(void*) == 8 ? 6 : (sizeof(void*) == 4 ? 5 : 4)) + 1)
#else
#define ARCH_BLOCK_BITS (sizeof(void*) == 8 ? 6 : (sizeof(void*) == 4 ? 5 : 4))
#endif

static const uintptr_t USER_BLOCK_SIZE = ((uintptr_t)1) << ARCH_BLOCK_BITS;
static const uintptr_t MIN_BLOCK_ALLOC = 1024*1024*2;

class FutexLock
{
	private:
	int32_t lockvar __attribute__((aligned(sizeof(int32_t))));

	//// atomic wrapper functions
	template<typename T>
	static void store(T *mem, T val)
	{
		__atomic_store_n(mem, val, __ATOMIC_SEQ_CST);
	}

	template<typename T>
	static bool cas(T *mem, T expected, T newval)
	{
		return __atomic_compare_exchange_n(mem, &expected, newval, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	}

	template<typename T>
	static T swap(T *mem, T val)
	{
		return __atomic_exchange_n(mem, val, __ATOMIC_SEQ_CST);
	}
	////

	void futex(int op, int val)
	{
		syscall(SYS_futex, &lockvar, op, val, NULL, NULL, 0);
	}

	public:
	void init()
	{
		store(&lockvar, 0);
	}

	void lock()
	{
		if(!cas(&lockvar, 0, 1)) {
			while(swap(&lockvar, 2) != 0) {
				futex(FUTEX_WAIT_PRIVATE, 2);
			}
		}
	}

	void unlock()
	{
		const uint32_t oldval = swap(&lockvar, 0);
		if(oldval == 2) {
			futex(FUTEX_WAKE_PRIVATE, 1);
		}
	}
};

#ifdef MEASURE_TIME
static uint64_t getNanos()
{
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC_RAW, &tp);
	return ((uint64_t)tp.tv_sec) * 1000000000 + ((uint64_t)tp.tv_nsec);
}
#endif

static void* mem_map(uintptr_t size)
{
	void *mem = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if(mem == MAP_FAILED) {
		return 0;
	}

	return mem;
}

static void mem_unmap(void *mem, uintptr_t size)
{
	int err = munmap(mem, size);
	(void)err;
	//if(err != 0) {
	//}
}

static os::res::TreeBlockAllocatorNoLock<ARCH_BLOCK_BITS> blockAllocator;
//static os::res::ListBlockAllocator<NoLocker, USER_BLOCK_SIZE> blockAllocator;

class UserSpaceWrapper
{
	public:
	static void* alloc(uintptr_t n)
	{
		return blockAllocator.alloc(n);
	}
	static void free(void *ptr, uintptr_t n)
	{
		blockAllocator.free(ptr, n);
	}
	static bool grow(void *ptr, uintptr_t a, uintptr_t b)
	{
		return blockAllocator.grow(ptr, a, b);
	}
	static uintptr_t getBlockBits()
	{
		return blockAllocator.getBlockBits();
	}
};

static os::res::WrapperAllocator<UserSpaceWrapper> fineAllocator;
static FutexLock lock;

class PrintIter
{
	public:
	uintptr_t blockBits;
	uintptr_t numFreeBlocks;

	void init(uintptr_t bits)
	{
		blockBits = bits;
		numFreeBlocks = 0;
	}

	bool operator()(void *s, uintptr_t blocks)
	{
		const uintptr_t start = (uintptr_t)s;
		const uintptr_t size = blocks << blockBits;
		const uintptr_t end = start + size;

		numFreeBlocks += 1;
		fprintf(stderr, "\t[0x%" PRIxPTR " - 0x%" PRIxPTR "] %" PRIuPTR "\n", start, end, size);

		return true;
	}
};

static uintptr_t alignUp(uintptr_t numToRound, uintptr_t multiple)
{
	uintptr_t mask = multiple - 1;
    return (numToRound + mask) & ~mask;
}

void *malloc(size_t size)
{
	if(size == 0) {
		return NULL;
	}

	lock.lock();

	#ifdef MORE_DEBUG
	fprintf(stderr, "malloc(%" PRIuPTR ") -> ", (uintptr_t)size);
	#endif

	#ifdef MEASURE_TIME
	uint64_t time = getNanos();
	#endif

	void *out = fineAllocator.alloc(size);
	if(out == 0) {
		uintptr_t overhead = fineAllocator.overhead();
		uintptr_t alignSize = alignUp(size + overhead, PAGE_SIZE);
		if(alignSize < MIN_BLOCK_ALLOC) {
			void *pages = mem_map(MIN_BLOCK_ALLOC);
			if(pages != 0) {
				blockAllocator.free(pages, MIN_BLOCK_ALLOC >> blockAllocator.getBlockBits());
			}
			out = fineAllocator.alloc(size);
		}
		else {
			void *pages = mem_map(alignSize);
			if(pages != 0) {
				out = fineAllocator.writeAlignedHeader(1, pages, alignSize);
			}
		}
	}

	#ifdef MORE_DEBUG
	fprintf(stderr, "0x%" PRIxPTR "\n", (uintptr_t)out);
	#endif

	#ifdef MEASURE_TIME
	time = getNanos() - time;
	fprintf(stderr, "malloc %" PRIu64 "\n", time);
	#endif

	lock.unlock();

	return out;
}

void* memalign(size_t alignment, size_t size)
{
	if(size == 0) {
		return NULL;
	}

	if(!((alignment != 0) && !(alignment & (alignment - 1)))) {
		return NULL;
	}

	lock.lock();

	#ifdef MORE_DEBUG
	fprintf(stderr, "memalign(%" PRIuPTR ", %" PRIuPTR ")\n", (uintptr_t)alignment, (uintptr_t)size);
	#endif

	#ifdef MEASURE_TIME
	uint64_t time = getNanos();
	#endif

	void *out = fineAllocator.allocAligned(alignment, size);
	if(out == 0) {
		uintptr_t overhead = fineAllocator.overhead();
		uintptr_t alignSize = alignUp(size + overhead + (alignment - 1), PAGE_SIZE);
		if(alignSize < MIN_BLOCK_ALLOC) {
			void *pages = mem_map(MIN_BLOCK_ALLOC);
			if(pages != 0) {
				blockAllocator.free(pages, MIN_BLOCK_ALLOC >> blockAllocator.getBlockBits());
			}
			out = fineAllocator.allocAligned(alignment, size);
		}
		else {
			void *pages = mem_map(alignSize);
			if(pages != 0) {
				out = fineAllocator.writeAlignedHeader(alignment, pages, alignSize);
			}
		}
	}

	#ifdef MEASURE_TIME
	time = getNanos() - time;
	fprintf(stderr, "memalign %" PRIu64 "\n", time);
	#endif

	lock.unlock();

	return out;
}

void free(void *mem)
{
	if(mem == NULL) {
		return;
	}

	lock.lock();

	#ifdef MORE_DEBUG
	PrintIter iter;
	iter.init(blockAllocator.getBlockBits());
	fprintf(stderr, "start free 0x%p\n", mem);
	iter.init(blockAllocator.getBlockBits());
	blockAllocator.iterate(iter);
	fprintf(stderr, "\tblocks: %" PRIuPTR "\n\n", iter.numFreeBlocks);
	#endif

	#ifdef MEASURE_TIME
	uint64_t time = getNanos();
	#endif

	fineAllocator.free(mem);
	for(;;) {
		uintptr_t blocks = MIN_BLOCK_ALLOC >> blockAllocator.getBlockBits();
		void *reclaim = blockAllocator.allocLargest(PAGE_SIZE, &blocks);
		if(reclaim == 0) {
			break;
		}

		mem_unmap(reclaim, blocks << blockAllocator.getBlockBits());
	}

	#ifdef MEASURE_TIME
	time = getNanos() - time;
	fprintf(stderr, "free %" PRIu64 "\n", time);
	#endif

	#ifdef MORE_DEBUG
	fprintf(stderr, "end free 0x%p\n", mem);
	iter.init(blockAllocator.getBlockBits());
	blockAllocator.iterate(iter);
	fprintf(stderr, "\tblocks: %" PRIuPTR "\n\n", iter.numFreeBlocks);
	#endif

	lock.unlock();
}

void* realloc(void *mem, size_t size)
{
	if(mem == NULL) {
		return malloc(size);
	}

	if(size == 0) {
		free(mem);
		return NULL;
	}

	lock.lock();

	#ifdef MORE_DEBUG
	PrintIter iter;
	iter.init(blockAllocator.getBlockBits());
	fprintf(stderr, "start realloc 0x%p\n", mem);
	iter.init(blockAllocator.getBlockBits());
	blockAllocator.iterate(iter);
	fprintf(stderr, "\tblocks: %" PRIuPTR "\n\n", iter.numFreeBlocks);
	#endif

	#ifdef MEASURE_TIME
	uint64_t time = getNanos();
	#endif

	void *out = fineAllocator.realloc(mem, size);
	if(out == 0) {
		uintptr_t overhead = fineAllocator.overhead();
		uintptr_t alignSize = alignUp(size + overhead, PAGE_SIZE);
		if(alignSize < MIN_BLOCK_ALLOC) {
			alignSize = MIN_BLOCK_ALLOC;
		}
		void *pages = mem_map(alignSize);
		if(pages != 0) {
			blockAllocator.free(pages, alignSize >> blockAllocator.getBlockBits());
		}
		out = fineAllocator.realloc(mem, size);
	}

	#ifdef MEASURE_TIME
	time = getNanos() - time;
	fprintf(stderr, "realloc %" PRIu64 "\n", time);
	#endif

	#ifdef MORE_DEBUG
	fprintf(stderr, "end realloc 0x%p\n", mem);
	iter.init(blockAllocator.getBlockBits());
	blockAllocator.iterate(iter);
	fprintf(stderr, "\tblocks: %" PRIuPTR "\n\n", iter.numFreeBlocks);
	#endif

	lock.unlock();

	return out;
}

extern void exit(int);

[[noreturn]] void panic(const char *format, ...)
{
	fprintf(stderr, "PANIC:\n");

    va_list args;
    va_start(args, format);
	vfprintf(stderr, format, args);
    va_end(args);

	exit(-1);
	for(;;);
}
