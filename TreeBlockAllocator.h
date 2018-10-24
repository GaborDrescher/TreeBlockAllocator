#ifndef   OS_RES_TREE_BLOCK_ALLOCATOR
#define   OS_RES_TREE_BLOCK_ALLOCATOR

#include <inttypes.h>
#include "RBTree.h"

namespace os {
namespace res {

struct EmbeddedFreeBlock
{
	#ifdef cf_debug_kernel
		// this should be the first member of this class
		uintptr_t canary;
	#endif

	struct LinkNode
	{
		EmbeddedFreeBlock *prev;
		EmbeddedFreeBlock *next;
		uintptr_t parent;
	};

	lib::adt::RBNode<EmbeddedFreeBlock> addrNode;

	union {
		lib::adt::RBNode<EmbeddedFreeBlock> sizeNode;
		LinkNode linkNode;
	};

	EmbeddedFreeBlock *headNext;
	uintptr_t size;

	uintptr_t getStartAddress()
	{
		return (uintptr_t)this;
	}

	static EmbeddedFreeBlock* create(uintptr_t start, uintptr_t blockSize)
	{
		EmbeddedFreeBlock *newBlock = (EmbeddedFreeBlock*)start;
		newBlock->size = blockSize;
		newBlock->headNext = nullptr;
		return newBlock;
	}

	static void destroy(EmbeddedFreeBlock *block)
	{
		// block should not be nullptr
		(void)block;
	}

	static EmbeddedFreeBlock* recycle(EmbeddedFreeBlock *block, uintptr_t start, uintptr_t blockSize)
	{
		(void)block;
		return create(start, blockSize);
	}

	#ifdef cf_debug_kernel
		private:
		uintptr_t calcCanary()
		{
			uintptr_t sum = 0;
			sum ^= (uintptr_t)this;
			sum <<= sizeof(sum) * 4;
			sum ^= size;
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

#ifdef cf_debug_kernel
	static_assert(sizeof(EmbeddedFreeBlock) == (sizeof(void*) * 9),
		"Size of EmbeddedFreeBlock is not sizeof(void*) * 9");
#else
	static_assert(sizeof(EmbeddedFreeBlock) == (sizeof(void*) * 8),
		"Size of EmbeddedFreeBlock is not sizeof(void*) * 8");
#endif

template<uintptr_t BLOCK_BITS, typename FreeBlock, typename Locker>
class TreeBlockAllocatorGeneric
{
	private:
	static_assert(BLOCK_BITS < (sizeof(uintptr_t) * 8), "");
	static_assert((((uintptr_t)1) << BLOCK_BITS) >= sizeof(FreeBlock), "");
	static_assert((void*)nullptr == (void*)0, "");

	template<bool COMPARE_SIZE>
	struct Comparator
	{
		static int cmp(uintptr_t val, FreeBlock *block)
		{
			uintptr_t blockVal;
			if(COMPARE_SIZE) {
				blockVal = block->size;
			}
			else {
				blockVal = block->getStartAddress();
			}

			if(val < blockVal) return -1;
			if(val > blockVal) return 1;
			return 0;
		}

		static int cmp(FreeBlock *a, FreeBlock *b)
		{
			if(COMPARE_SIZE) {
				return cmp(a->size, b);
			}
			else {
				return cmp(a->getStartAddress(), b);
			}
		}
	};

	Locker locker;
	lib::adt::RBTree<FreeBlock, &FreeBlock::addrNode, uintptr_t, Comparator<false> > addrTree;
	lib::adt::RBTree<FreeBlock, &FreeBlock::sizeNode, uintptr_t, Comparator<true> > sizeTree;

	// number of free blocks
	uintptr_t freeBlocks;

	// number of free continuous chunks
	uintptr_t contChunks;

	public:
	uintptr_t getBlockSize() const
	{
		return ((uintptr_t)1) << BLOCK_BITS;
	}

	private:
	static uintptr_t alignUp(uintptr_t numToRound, uintptr_t multiple) 
	{
		uintptr_t mask = multiple - 1;
	    return (numToRound + mask) & ~mask;
	}

	void linkBlock(FreeBlock *oldBlock, FreeBlock *newBlock)
	{
		FreeBlock *ring = oldBlock->headNext;
		newBlock->headNext = oldBlock;
		sizeTree.replace(oldBlock, newBlock);

		if(ring == nullptr) {
			oldBlock->headNext = (FreeBlock*)(((uintptr_t)newBlock) | 1);
			oldBlock->linkNode.prev = oldBlock;
			oldBlock->linkNode.next = oldBlock;
		}
		else {
			oldBlock->headNext = (FreeBlock*)(((uintptr_t)newBlock) | 1);
			ring->headNext = (FreeBlock*)((uintptr_t)1);

			oldBlock->linkNode.prev = ring;
			oldBlock->linkNode.next = ring->linkNode.next;

			ring->linkNode.next->linkNode.prev = oldBlock;
			ring->linkNode.next = oldBlock;
		}
	}

	void unLinkBlock(FreeBlock *oldBlock)
	{
		const bool inRing = (((uintptr_t)(oldBlock->headNext)) & 1) == 1;
		FreeBlock *head = (FreeBlock*)(((uintptr_t)(oldBlock->headNext)) & (~1ul));
		if(inRing) {
			if(head == nullptr) {
				oldBlock->linkNode.next->linkNode.prev = oldBlock->linkNode.prev;
				oldBlock->linkNode.prev->linkNode.next = oldBlock->linkNode.next;
			}
			else {
				FreeBlock *prev = oldBlock->linkNode.prev;
				if(prev == oldBlock) {
					head->headNext = nullptr;
				}
				else {
					prev->headNext = oldBlock->headNext;
					head->headNext = prev;
					oldBlock->linkNode.next->linkNode.prev = oldBlock->linkNode.prev;
					oldBlock->linkNode.prev->linkNode.next = oldBlock->linkNode.next;
				}
			}
		}
		else {
			FreeBlock *ring = oldBlock->headNext;
			FreeBlock *prev = ring->linkNode.prev;
			if(prev == ring) {
				ring->headNext = nullptr;
			}
			else {
				prev->headNext = (FreeBlock*)(((uintptr_t)ring) | 1);
				ring->headNext = prev;
				ring->linkNode.next->linkNode.prev = ring->linkNode.prev;
				ring->linkNode.prev->linkNode.next = ring->linkNode.next;
			}
			sizeTree.replace(oldBlock, ring);
		}
	}

	void removeFromSizeTree(FreeBlock *block)
	{
		if(block->headNext != nullptr) {
			unLinkBlock(block);
			block->headNext = nullptr;
		}
		else {
			sizeTree.remove(block);
		}
	}

	void remove(FreeBlock *block)
	{
		addrTree.remove(block);
		removeFromSizeTree(block);
	}

	void addToSizeTree(FreeBlock *block)
	{
		FreeBlock *oldBlock = sizeTree.insert(block);
		if(oldBlock != block) {
			linkBlock(oldBlock, block);
		}
	}

	void add(FreeBlock *block)
	{
		addrTree.insert(block);
		addToSizeTree(block);
	}

	void* doAlignmentSplit(FreeBlock *outBlock, uintptr_t alignment, uintptr_t allocSize)
	{
		// start of this free block
		uintptr_t startAddr = outBlock->getStartAddress();

		// size of this block in bytes
		uintptr_t blockSize = outBlock->size;

		// end of this free block, not inclusive
		uintptr_t blockEnd = startAddr + blockSize;

		// alignedChunk contains the aligned address
		uintptr_t alignedChunk = alignUp(startAddr, alignment);

		// now we may have 3 continuous blocks:
		// 1. before the aligned chunk
		// 2. the chunk itself
		// 3. and after the chunk
		// the leading chunk starts at 'startAddr'
		uintptr_t leadingSize = alignedChunk - startAddr;
		uintptr_t leadingBlocks = leadingSize >> BLOCK_BITS;

		uintptr_t trailingBlocksStart = alignedChunk + allocSize;
		uintptr_t trailingSize = blockEnd - trailingBlocksStart;
		uintptr_t trailingBlocks = trailingSize >> BLOCK_BITS;
		
		// if there are some leading blocks then the start address does not
		// change, so do not remove the old free block from the address tree
		if(leadingBlocks != 0) {
			// remove this block just from the size tree, start address will
			// not change
			removeFromSizeTree(outBlock);

			// set the new size of this block
			outBlock->size = leadingSize;

			// add this block back to the size tree
			addToSizeTree(outBlock);

			kassert(outBlock->applyCanary());

			// there are some trailing blocks and we kept the leading block
			// -> create a new free block and add it
			if(trailingBlocks != 0) {
				FreeBlock *newBlock = FreeBlock::create(trailingBlocksStart, trailingSize);
				add(newBlock);
				kassert(newBlock->applyCanary());
				contChunks += 1;
			}
		}
		else {
			// there are no leading blocks
			if(trailingBlocks != 0) {
				// see alloc() for the strategy that is used here
				removeFromSizeTree(outBlock);

				FreeBlock *newBlock = FreeBlock::create(trailingBlocksStart, trailingSize);
				addrTree.replace(outBlock, newBlock);

				addToSizeTree(newBlock);
				kassert(newBlock->applyCanary());
			}
			else {
				// there are also no trailing blocks, the block is removed
				// completely
				remove(outBlock);
				contChunks -= 1;
			}
			FreeBlock::destroy(outBlock);
		}

		return (void*)alignedChunk;
	}

	public:
	void init()
	{
		addrTree.init();
		sizeTree.init();
		locker.init();
		freeBlocks = 0;
		contChunks = 0;
	}

	TreeBlockAllocatorGeneric() : freeBlocks(0), contChunks(0)
	{
	}

	TreeBlockAllocatorGeneric(const char *NO_INIT) : addrTree(NO_INIT),
													sizeTree(NO_INIT)
	{
	}

	void* alloc(uintptr_t blocks)
	{
		if(blocks == 0) {
			return nullptr;
		}

		void *out = nullptr;
		// the size to allocate
		const uintptr_t size = blocks << BLOCK_BITS;

		// get the lock
		typename Locker::Item item;
		locker.lock(&item);

		// check the red-black trees
		kassert(check());

		// look for a FreeBlock >= 'size' in the size tree
		FreeBlock *outBlock = sizeTree.ceil(size);
		if(outBlock != nullptr) {
			// we found a free block large enough, decrease the number of free
			// blocks, this is just for statistics
			kassert(freeBlocks >= blocks);
			freeBlocks -= blocks;

			// start of this free block
			const uintptr_t startAddr = outBlock->getStartAddress();

			// size of this block in bytes
			const uintptr_t blockSize = outBlock->size;

			// end of this free block, not inclusive
			const uintptr_t blockEnd = startAddr + blockSize;

			const uintptr_t trailingBlocksStart = startAddr + size;
			const uintptr_t trailingSize = blockEnd - trailingBlocksStart;
			const uintptr_t trailingBlocks = trailingSize >> BLOCK_BITS;

			if(trailingBlocks == 0) {
				// a continuous block is completely removed
				// remove the old block from both trees
				remove(outBlock);
				contChunks -= 1;
			}
			else {
				// there are trailing blocks, we can add the trailing block to
				// the size tree and replace the node in the address tree
				// without removing and readding it into the same position in
				// the addrTree

				removeFromSizeTree(outBlock);

				FreeBlock *newBlock = FreeBlock::create(trailingBlocksStart, trailingSize);
				addrTree.replace(outBlock, newBlock);

				addToSizeTree(newBlock);
				kassert(newBlock->applyCanary());
			}

			FreeBlock::destroy(outBlock);

			// check if the return value is roughly in the right range
			kassert(startAddr >= (1024 * 1024));
			kassert(startAddr < (~((uintptr_t)0xffff))); // max address - 64k

			out = (void*)startAddr;
		}

		locker.unlock(&item);
		return out;
	}

	void* allocAligned(uintptr_t alignment, uintptr_t blocks)
	{
		// if not power of two
		if((alignment == 0) || ((alignment & (alignment - 1)) != 0)) {
			return nullptr;
		}
		if(blocks == 0) {
			return nullptr;
		}

		if(alignment <= getBlockSize()) {
			return alloc(blocks);
		}

		// at this point alignment is a larger power of two than the block size

		// the size to allocate
		uintptr_t allocSize = blocks << BLOCK_BITS;

		// allocate (blocks + alignment - 1) blocks
		uintptr_t extraBlocks = (alignment >> BLOCK_BITS) - 1;
		
		// look for this size to allocate
		uintptr_t size = (blocks + extraBlocks) << BLOCK_BITS;

		// get the lock
		typename Locker::Item item;
		locker.lock(&item);

		// check the red-black trees
		kassert(check());

		// look for a FreeBlock >= 'size' in the size tree
		FreeBlock *outBlock = sizeTree.ceil(size);
		if(outBlock == nullptr) {
			// there is no such free block
			// try allocating exactly the desired size - maybe the resulting
			// chunk happens to have proper alignment
			outBlock = sizeTree.ceil(allocSize);

			if(outBlock == nullptr || ((outBlock->getStartAddress() % alignment) != 0)) {
				// also not successful
				locker.unlock(&item);
				return nullptr;
			}
		}

		// we found a free block large enough, decrease the number of free
		// blocks, this is just for statistics, do not remove 'extraBlocks'
		// since we will return them later
		kassert(freeBlocks >= blocks);
		freeBlocks -= blocks;

		void *out = doAlignmentSplit(outBlock, alignment, allocSize);

		// check if the return value is roughly in the right range
		kassert((uintptr_t)out >= (1024 * 1024));
		kassert((uintptr_t)out < (~((uintptr_t)0xffff))); // max address - 64k

		locker.unlock(&item);

		return out;
	}

	bool free(void *s, uintptr_t blocks)
	{
		uintptr_t start = (uintptr_t)s;

		kassert(start != 0);
		kassert(blocks != 0);

		uintptr_t size = blocks << BLOCK_BITS;
		const uintptr_t end = start + size;

		typename Locker::Item item;
		locker.lock(&item);

		// check the red-black trees
		kassert(check());

		#ifdef cf_debug_kernel
			// test if any of the blocks to be freed are already in the
			// allocator -> double free
			FreeBlock *debugBlock = addrTree.floor(end);
			if(debugBlock != nullptr) {
				// test for overlap
				kassert((debugBlock->getStartAddress() + debugBlock->size) <= start
					|| end <= debugBlock->getStartAddress());
			}
		#endif

		// check if the arguments are roughly in the right range
		kassert((freeBlocks + blocks) > freeBlocks);
		kassert(start >= (1024 * 1024));
		kassert(start < (~((uintptr_t)0xffff))); // max address - 64k

		freeBlocks += blocks;

		// try to merge an already existing free block at the end of the memory
		// to be freed, search for its successor
		FreeBlock *pred;
		FreeBlock *succ = addrTree.search(end);
		if(succ != nullptr) {
			// if there is a successor, grab its predecessor, this is an
			// optimisation as RBTree::prev() is (in average) faster than
			// RBTree::floor()
			pred = addrTree.prev(succ);
		}
		else {
			// if there is no successor, search for the predecessor 
			pred = addrTree.floor(start);
		}

		if(pred != nullptr) {
			const uintptr_t floorStart = pred->getStartAddress();
			const uintptr_t floorEndAddr = floorStart + pred->size;

			// if the end of the existing free block is not the same as the start
			// address of the block to be freed, then this is not the real
			// predecessor
			if(floorEndAddr != start) {
				pred = nullptr;
			}
		}

		if(pred != nullptr && succ != nullptr) {
			// append the current block and the successor to the predecessor,
			// start address does not change
			removeFromSizeTree(pred);
			remove(succ);

			pred->size += size + succ->size;
			addToSizeTree(pred);
			contChunks -= 1;

			kassert(pred->applyCanary());
		}
		else if(pred != nullptr) {
			// append the current block to the predecessor, start address does
			// not change
			removeFromSizeTree(pred);
			pred->size += size;
			addToSizeTree(pred);

			kassert(pred->applyCanary());
		}
		else if(succ != nullptr) {
			// replace successor in the addrTree without removing and re-adding
			removeFromSizeTree(succ);

			FreeBlock *newBlock = FreeBlock::create(start, size + succ->size);
			addrTree.replace(succ, newBlock);

			addToSizeTree(newBlock);
			kassert(newBlock->applyCanary());
		}
		else {
			// the blocks to be freed cannot be merged, create a new block
			FreeBlock *newBlock = FreeBlock::create(start, size);
			contChunks += 1;
			add(newBlock);
			kassert(newBlock->applyCanary());
		}

		locker.unlock(&item);
		return true;
	}

	bool grow(void *s, uintptr_t oldBlocks, uintptr_t newBlocks)
	{
		uintptr_t start = (uintptr_t)s;
		kassert(start != 0);
		kassert(oldBlocks != 0);
		kassert(newBlocks != 0);

		kassert(newBlocks > oldBlocks);

		bool resizeDone = false;
		const uintptr_t oldSize = oldBlocks << BLOCK_BITS;
		const uintptr_t addBlocks = newBlocks - oldBlocks;
		const uintptr_t additionalSpace = addBlocks << BLOCK_BITS;
		const uintptr_t end = start + oldSize;

		typename Locker::Item item;
		locker.lock(&item);

		// check the red-black trees
		kassert(check());

		// check if the arguments are roughly in the right range
		kassert(start >= (1024 * 1024));
		kassert(start < (~((uintptr_t)0xffff))); // max address - 64k

		FreeBlock *extBlock = addrTree.search(end);
		if(extBlock != nullptr) {
			if(extBlock->size >= additionalSpace) {
				const uintptr_t diff = extBlock->size - additionalSpace;
				if(diff > 0) {
					removeFromSizeTree(extBlock);

					FreeBlock *newBlock = FreeBlock::create(end + additionalSpace, diff);
					addrTree.replace(extBlock, newBlock);

					addToSizeTree(newBlock);
					kassert(newBlock->applyCanary());
				}
				else {
					// a continuous block is completely removed
					remove(extBlock);
					FreeBlock::destroy(extBlock);
					contChunks -= 1;
				}
				resizeDone = true;
				freeBlocks -= (newBlocks - oldBlocks);
			}
		}

		locker.unlock(&item);
		return resizeDone;
	}

	uintptr_t getFreeCount()
	{
		uintptr_t out;

		typename Locker::Item item;
		locker.lock(&item);
		out = freeBlocks;
		locker.unlock(&item);

		return out;
	}

	uintptr_t getContBlockCount()
	{
		uintptr_t out;

		typename Locker::Item item;
		locker.lock(&item);
		out = contChunks;
		locker.unlock(&item);

		return out;
	}

	void checkAllCanaries(FreeBlock *root)
	{
		if(root == nullptr) {
			return;
		}

		kassert(root->checkCanary());
		checkAllCanaries(root->addrNode.right);
		checkAllCanaries(root->addrNode.left);
	}

	// this function is for debugging and testcases
	bool check()
	{

// Debugging is enabled by default on leon, but this check slows the invasive
// hardware down too much - so we disable it for now.
#ifndef cf_hw_invasic

		// check canaries
		checkAllCanaries(addrTree.getRoot());

		// check trees
		bool retA = addrTree.check();
		if(!retA) {
			printk("addrTree check failed\n");
			return false;
		}

		bool retB = sizeTree.check();
		if(!retB) {
			printk("sizeTree check failed\n");
			return false;
		}

		// iterate over all elements this includes the linked lists
		// check if the size of all elements in a linked list is the same
		// check if 'freeBlocks' is the same as the number of free blocks
		uintptr_t count = 0;
		for(FreeBlock *block = sizeTree.min(); block != nullptr; block = sizeTree.next(block)) {
			count += block->size >> BLOCK_BITS;
			if(block->headNext != nullptr) {
				// linked list case
				// all freeblock here must have this size
				uintptr_t size = block->size;
				FreeBlock *ringElem = block->headNext;
				do {
					if(ringElem->size != size) {
						printk("element in ring has wrong size, expected %" PRIuPTR " got %" PRIuPTR "\n", size, ringElem->size);
						return false;
					}
					count += ringElem->size >> BLOCK_BITS;

					ringElem = ringElem->linkNode.next;
				}
				while(ringElem != block->headNext);
			}
		}

		if(count != freeBlocks) {
			printk("counted number of free blocks %" PRIuPTR " is not equal to 'freeBlocks' %" PRIuPTR "\n", count, freeBlocks);
			return false;
		}

		// check contChunks counter
		uintptr_t checkContChunks = 0;
		for(FreeBlock *block = addrTree.min(); block != nullptr; block = addrTree.next(block)) {
			checkContChunks += 1;
		}
		if(checkContChunks != contChunks) {
			printk("counted number of continuous free blocks %" PRIuPTR " is not equal to 'contChunks' %" PRIuPTR "\n", checkContChunks, contChunks);
			return false;
		}

#endif // cf_hw_invasic

		return true;
	}

	// this function is for debugging and testcases
	// count the number of elements in the trees
	void getTreeElems(uintptr_t *sizeAddrTree, uintptr_t *sizeSizeTree)
	{
		uintptr_t sizeElems = 0;
		for(FreeBlock *block = sizeTree.min(); block != nullptr; block = sizeTree.next(block)) {
			sizeElems += 1;
			if(block->headNext != nullptr) {
				// linked list case
				FreeBlock *ringElem = block->headNext;
				do {
					sizeElems += 1;
					ringElem = ringElem->linkNode.next;
				}
				while(ringElem != block->headNext);
			}
		}
		*sizeSizeTree = sizeElems;

		uintptr_t addrElems = 0;
		for(FreeBlock *block = addrTree.min(); block != nullptr; block = addrTree.next(block)) {
			// no linked list case here
			addrElems += 1;
		}

		*sizeAddrTree = addrElems;
	}

	template<typename Iterator>
	void iterate(Iterator &&iter)
	{
		typename Locker::Item item;
		locker.lock(&item);

		for(FreeBlock *block = addrTree.min(); block != nullptr; block = addrTree.next(block)) {
			if(!iter((void*)(block->getStartAddress()), block->size >> BLOCK_BITS)) {
				break;
			}
		}

		locker.unlock(&item);
	}

	template<typename Iterator>
	void iterateSizeReverse(Iterator &&iter)
	{
		typename Locker::Item item;
		locker.lock(&item);

		for(FreeBlock *block = sizeTree.max(); block != nullptr; block = sizeTree.prev(block)) {
			if(!iter((void*)(block->getStartAddress()), block->size >> BLOCK_BITS)) {
				break;
			}
		}

		locker.unlock(&item);
	}

	void* allocLargest(uintptr_t minAlign, uintptr_t *minBlocks)
	{
		void *out = nullptr;

		const uintptr_t minSize = *minBlocks << BLOCK_BITS;

		typename Locker::Item item;
		locker.lock(&item);

		// check the red-black trees
		kassert(check());

		FreeBlock *block = sizeTree.max();
		if(block != nullptr) {
			const uintptr_t start = block->getStartAddress();
			const uintptr_t blockSize = block->size;

			if(blockSize >= minSize) {
				const uintptr_t alignStart = alignUp(start, minAlign);
				const uintptr_t alignWaste = alignStart - start;
				if(blockSize > alignWaste) {
					const uintptr_t remainSize = (blockSize - alignWaste) & ~(minAlign - 1);
					if(remainSize >= minSize) {
						const uintptr_t remainBlocks = remainSize >> BLOCK_BITS;
						out = doAlignmentSplit(block, minAlign, remainSize);
						*minBlocks = remainBlocks;
						freeBlocks -= remainBlocks;
					}
				}
			}
		}

		locker.unlock(&item);
		return out;
	}

	void benchCleanup()
	{
		uintptr_t largestStart = 0;
		uintptr_t largestSize = 0;

		for(FreeBlock *block = addrTree.min(); block != nullptr; block = addrTree.next(block)) {
			if(block->size > largestSize) {
				largestSize = block->size;
				largestStart = block->getStartAddress();
			}
		}

		if(largestStart != 0) {
			this->addrTree.init();
			this->sizeTree.init();
			this->freeBlocks = 0;

			this->free((void*)largestStart, largestSize >> BLOCK_BITS);
		}
	}
};

template<uintptr_t BLOCK_BITS, typename Locker>
class TreeBlockAllocator :
	public TreeBlockAllocatorGeneric<BLOCK_BITS, EmbeddedFreeBlock, Locker>
{
	private:
	typedef TreeBlockAllocatorGeneric<BLOCK_BITS, EmbeddedFreeBlock, Locker> Super;

	public:
	TreeBlockAllocator() : Super()
	{
	}

	TreeBlockAllocator(const char *NO_INIT) : Super(NO_INIT)
	{
	}
};

class NoLocker
{
	public:
	typedef bool Item;

	void init()
	{
	}

	void lock(bool *state)
	{
		(void)state;
	}

	void unlock(bool *state)
	{
		(void)state;
	}
};

template<uintptr_t BLOCK_BITS>
class TreeBlockAllocatorNoLock :
	public TreeBlockAllocatorGeneric<BLOCK_BITS, EmbeddedFreeBlock, NoLocker>
{
	private:
	typedef TreeBlockAllocatorGeneric<BLOCK_BITS, EmbeddedFreeBlock, NoLocker> Super;

	public:
	TreeBlockAllocatorNoLock() : Super()
	{
	}

	TreeBlockAllocatorNoLock(const char *NO_INIT) : Super(NO_INIT)
	{
	}
};

} // namespace res
} // namespace os

#endif /* OS_RES_TREE_BLOCK_ALLOCATOR */
