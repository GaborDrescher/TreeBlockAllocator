// This class implements a red-black tree data-structure.
// The following operations run in O(log(N)) worst-case time:
// search, ceil, floor, min, max, insert, remove, prev and next
// Nodes with the same key are not allowed, insert will return the old item in this case.
// To remove a node based on its key, use: obj = search(key); remove(obj);
//
// It is recommended to encapsulate this class if it is OK for you to use
// dynamic memory allocation. Nevertheless, here is a direct-usage example that maps
// unsigned integers to const char*
//
//    struct UIntNode
//    {
//        lib::adt::RBNode<UIntNode> mynode;
//        unsigned int key;
//        const char *data;
//    };
//
//    struct UintComp
//    {
//        static int cmp(unsigned int k, UIntNode *other)
//        {
//            if(k < other->key) return -1;
//            if(k > other->key) return  1;
//            return 0;
//        }
//
//        static int cmp(UIntNode *a, UIntNode *b)
//        {
//            if(a->key < b->key) return -1;
//            if(a->key > b->key) return  1;
//            return 0;
//        }
//    };
//
//    // comparator type------------------------------------------------|
//    // (can be the same as your object type)                          |
//    //                                                                |
//    // key type------------------------------------------|            |
//    //                                                   |            |
//    // pointer to a RBNode                               |            |
//    // member in your                                    |            |
//    // object type-------------------|                   |            |
//    //                               |                   |            |
//    // type of your objects-|        |                   |            |
//    //                      |        |                   |            |
//    //                      |        |                   |            |
//    lib::adt::RBTree<UIntNode, &UIntNode::mynode, unsigned int, UintComp> tree;
//
//    UIntNode a;
//    a.key = 42;
//    a.data = "the answer to life the universe and everything";
//
//    tree.insert(&a);
//

#ifndef   LIB_ADT_RB_TREE_HEADER
#define   LIB_ADT_RB_TREE_HEADER

#include <stdint.h>
#include "RBTreeGeneric.h"

namespace lib {
namespace adt {

template<typename T>
class RBNode
{
	public:
	T *right;
	T *left;
	uintptr_t parentColor;

	void propagate(RBNode<T> *stop, RBNode<T> T::*nodeMember)
	{
		(void)stop;
		(void)nodeMember;
		// nothing to do
	}

	void copy(RBNode<T> *newNode, RBNode<T> T::*nodeMember)
	{
		(void)newNode;
		(void)nodeMember;
		// nothing to do
	}

	void rotate(RBNode<T> *newNode, RBNode<T> T::*nodeMember)
	{
		(void)newNode;
		(void)nodeMember;
		// nothing to do
	}
};

template<typename T, RBNode<T> T::*nodeMember, typename K, typename Comp>
class RBTree :
	public RBTreeGeneric<T, RBNode<T>, nodeMember, K, Comp>
{
	private:
	typedef RBTreeGeneric<T, RBNode<T>, nodeMember, K, Comp> Super;

	public:
	RBTree() : Super()
	{
	}

	RBTree(const char *NO_INIT) : Super(NO_INIT)
	{
	}
};

} // namespace adt
} // namespace lib

#endif /* LIB_ADT_RB_TREE_HEADER */
