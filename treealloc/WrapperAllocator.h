#ifndef   OS_RES_WRAPPER_ALLOCATOR_HEADER
#define   OS_RES_WRAPPER_ALLOCATOR_HEADER

// this allocator is just a thin wrapper around a block allocator, using it
// directly

#include <inttypes.h>
#include <string.h> // memcpy
#include "kassert.h"

namespace os {
namespace res {

template<typename BlockAllocator, uintptr_t ALIGNMENT = 2 * sizeof(uintptr_t)>
class WrapperAllocator
{
	private:
	struct alignas(ALIGNMENT) MemHeader
	{
		#ifdef cf_debug_kernel
			// this should be the first member of this class
			uintptr_t canary;
		#endif

		uintptr_t start;
		uintptr_t blocks;

		#ifdef cf_debug_kernel
			private:
			uintptr_t calcCanary()
			{
				uintptr_t sum = 0;
				sum ^= start;
				sum <<= sizeof(sum) * 4;
				sum ^= blocks;
				sum ^= ~((uintptr_t)(0xBADC0DED));
				return sum;
			}

			public:
			bool applyCanary()
			{
				canary = calcCanary();
				return true;
			}

			bool checkCanary()
			{
				uintptr_t expectedCanary = calcCanary();
				return expectedCanary == canary;
			}
		#endif
	};

	static uintptr_t alignUp(uintptr_t numToRound, uintptr_t multiple)
	{
		uintptr_t mask = multiple - 1;
	    return (numToRound + mask) & ~mask;
	}

	public:
	uintptr_t overhead() const
	{
		return sizeof(MemHeader);
	}

	void* alloc(uintptr_t size)
	{
		kassert(size != 0);

		const uintptr_t blockBits = BlockAllocator::getBlockBits();
		const uintptr_t blockSize = ((uintptr_t)1) << blockBits;
		const uintptr_t nBlocks = alignUp(size + sizeof(MemHeader), blockSize) >> blockBits;
		const uintptr_t rawMem = (uintptr_t)BlockAllocator::alloc(nBlocks);

		if(rawMem == 0) {
			return 0;
		}

		MemHeader *header = (MemHeader*)rawMem;
		header->start = rawMem;
		header->blocks = nBlocks;

		kassert(header->applyCanary());

		return (void*)(header + 1);
	}

	void* writeAlignedHeader(uintptr_t alignment, void *rawMem, uintptr_t size)
	{
		uintptr_t chunk = (uintptr_t)rawMem;
		uintptr_t nBlocks = size >> BlockAllocator::getBlockBits();

		// this will be the address to return
		const uintptr_t alignedChunk = alignUp(chunk + sizeof(MemHeader), alignment);

		// write the header
		MemHeader *header = (MemHeader*)(alignedChunk - sizeof(MemHeader));

		header->start = chunk;
		header->blocks = nBlocks;

		kassert(header->applyCanary());

		return (void*)alignedChunk;
	}

	void* allocAligned(uintptr_t alignment, uintptr_t size)
	{
		kassert(size != 0);
		// power of two
		kassert((alignment != 0) && !(alignment & (alignment - 1)));

		// just do a normal alloc when the alignment is small
		if(alignment <= sizeof(MemHeader)) {
			return alloc(size);
		}

		const uintptr_t blockBits = BlockAllocator::getBlockBits();
		const uintptr_t blockSize = ((uintptr_t)1) << blockBits;

		const uintptr_t nBlocks = alignUp(size +
			sizeof(MemHeader) + (alignment - 1), blockSize) >> blockBits;

		const uintptr_t chunk = (uintptr_t)BlockAllocator::alloc(nBlocks);
		if(chunk == 0) {
			return 0;
		}

		return writeAlignedHeader(alignment, (void*)chunk, nBlocks * blockSize);
	}

	void* realloc(void *ptr, uintptr_t size)
	{
		if(ptr == 0) {
			return this->alloc(size);
		}

		if(size == 0) {
			this->free(ptr);
			return 0;
		}

		MemHeader *header = ((MemHeader*)ptr) - 1;
		kassert(header->checkCanary());

		const uintptr_t blockBits = BlockAllocator::getBlockBits();
		const uintptr_t blockSize = ((uintptr_t)1) << blockBits;

		// end of this memory chunk
		const uintptr_t end = header->start + header->blocks * blockSize;

		// the old available size
		const uintptr_t oldSize = end - ((uintptr_t)ptr);

		if(size > oldSize) {
			// new size is larger than the available space
			// grow the region in place, if possible
			const uintptr_t additionalBytes = size - oldSize;
			const uintptr_t additionalBlocks = alignUp(additionalBytes, blockSize) >> blockBits;
			if(BlockAllocator::grow((void*)(header->start), header->blocks, header->blocks + additionalBlocks)) {
				// success, we could grow the region in-place
				header->blocks += additionalBlocks;
				kassert(header->applyCanary());
				return ptr;
			}

			// else alloc, copy, free
			// this is the worst case
			void *mem = this->alloc(size);
			if(mem == 0) {
				return 0;
			}
			memcpy(mem, ptr, oldSize);
			this->free(ptr);

			return mem;
		}
		// if(size <= oldSize)
		// new size is smaller, check if we can give some memory back to
		// the block allocator
		const uintptr_t unneededBytes = oldSize - size;
		if(unneededBytes > blockSize) {
			// if at least one full block is free, give it/them back
			const uintptr_t unneededBlocks = unneededBytes >> blockBits;
			const uintptr_t newEnd = end - unneededBlocks * blockSize;
			BlockAllocator::free((void*)newEnd, unneededBlocks);

			header->blocks -= unneededBlocks;
			kassert(header->applyCanary());
		}
		return ptr;
	}

	void free(void *ptr)
	{
		kassert(ptr != nullptr);

		MemHeader *header = ((MemHeader*)ptr) - 1;
		kassert(header->checkCanary());
		BlockAllocator::free((void*)(header->start), header->blocks);
	}

	uintptr_t getUserSize(void *ptr)
	{
		kassert(ptr != nullptr);

		MemHeader *header = ((MemHeader*)ptr) - 1;
		kassert(header->checkCanary());

		uintptr_t userStart = (uintptr_t)ptr;
		uintptr_t blockStart = header->start;
		uintptr_t overheadBytes = userStart - blockStart;

		uintptr_t totalSize = header->blocks << BlockAllocator::getBlockBits();
		uintptr_t userSize = totalSize - overheadBytes;
		return userSize;
	}
};


} // namespace res
} // namespace os

#endif /* OS_RES_WRAPPER_ALLOCATOR_HEADER */
