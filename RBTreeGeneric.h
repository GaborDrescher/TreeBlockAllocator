#ifndef   LIB_ADT_RB_TREE_GENERIC_HEADER
#define   LIB_ADT_RB_TREE_GENERIC_HEADER

#include <stdint.h>

namespace lib {
namespace adt {

template<typename T, typename NodeType, NodeType T::*nodeMember, typename K, typename Comp>
class RBTreeGeneric
{
	protected:
	T *root;

	enum {
		RED = 0,
		BLACK = 1
	};

	static T* getParent(uintptr_t pc)
	{
		return (T*)(pc & (~1UL));
	}

	static T* getParent(T *r)
	{
		return getParent((r->*nodeMember).parentColor);
	}

	static bool isBlack(uintptr_t pc)
	{
		return (pc & 1) == 1;
	}

	static bool isBlack(T *rb)
	{
		return isBlack((rb->*nodeMember).parentColor);
	}

	static bool isRed(T *rb)
	{
		return !isBlack(rb);
	}

	static void setParent(T *rb, T *p)
	{
		(rb->*nodeMember).parentColor = ((rb->*nodeMember).parentColor & 1) | (uintptr_t)p;
	}

	static void setParent(T *rb, T *p, int color)
	{
		(rb->*nodeMember).parentColor = (uintptr_t)p | color;
	}

	void changeChild(T *old, T *new_node, T *parent)
	{
		if(parent) {
			if((parent->*nodeMember).left == old) {
				(parent->*nodeMember).left = new_node;
			}
			else {
				(parent->*nodeMember).right = new_node;
			}
		}
		else {
			root = new_node;
		}
	}

	static void setBlack(T *rb)
	{
		(rb->*nodeMember).parentColor |= BLACK;
	}

	static T *redParent(T *red)
	{
		return (T *)(red->*nodeMember).parentColor;
	}

	void rotateSetParents(T *old, T *new_node, int color)
	{
		T *parent = getParent(old);
		(new_node->*nodeMember).parentColor = (old->*nodeMember).parentColor;
		setParent(old, new_node, color);
		changeChild(old, new_node, parent);
	}

	T* eraseAugmented(T *node)
	{
		T *child = (node->*nodeMember).right;
		T *tmp = (node->*nodeMember).left;
		T *parent, *rebalance;
		uintptr_t pc;

		if (!tmp) {
			pc = (node->*nodeMember).parentColor;
			parent = getParent(pc);
			changeChild(node, child, parent);
			if (child) {
				(child->*nodeMember).parentColor = pc;
				rebalance = 0;
			} else
				rebalance = isBlack(pc) ? parent : 0;
			tmp = parent;
		} else if (!child) {
			(tmp->*nodeMember).parentColor = pc = (node->*nodeMember).parentColor;
			parent = getParent(pc);
			changeChild(node, tmp, parent);
			rebalance = 0;
			tmp = parent;
		} else {
			T *successor = child, *child2;

			tmp = (child->*nodeMember).left;
			if (!tmp) {
				parent = successor;
				child2 = (successor->*nodeMember).right;
				(node->*nodeMember).copy(&(successor->*nodeMember), nodeMember); // augment_copy(node, successor);
			} else {
				do {
					parent = successor;
					successor = tmp;
					tmp = (tmp->*nodeMember).left;
				} while (tmp);
				child2 = (successor->*nodeMember).right;
				(parent->*nodeMember).left = child2;
				(successor->*nodeMember).right = child;
				setParent(child, successor);
				(node->*nodeMember).copy(&(successor->*nodeMember), nodeMember); // augment_copy(node, successor);
				(parent->*nodeMember).propagate(&(successor->*nodeMember), nodeMember); // augment_propagate(parent, successor);
			}

			tmp = (node->*nodeMember).left;
			(successor->*nodeMember).left = tmp;
			setParent(tmp, successor);

			pc = (node->*nodeMember).parentColor;
			tmp = getParent(pc);
			changeChild(node, successor, tmp);

			if (child2) {
				(successor->*nodeMember).parentColor = pc;
				setParent(child2, parent, BLACK);
				rebalance = 0;
			} else {
				uintptr_t pc2 = (successor->*nodeMember).parentColor;
				(successor->*nodeMember).parentColor = pc;
				rebalance = isBlack(pc2) ? parent : 0;
			}
			tmp = successor;
		}
		(tmp->*nodeMember).propagate(0, nodeMember); // augment_propagate(tmp, 0);
		return rebalance;
	}

	static void linkNode(T *node, T *parent, T **link)
	{
		(node->*nodeMember).parentColor = (uintptr_t)parent;
		(node->*nodeMember).left = (node->*nodeMember).right = 0;
		*link = node;
	}

	void insertInternal(T *node)
	{
		T *parent = redParent(node), *gparent, *tmp;

		for(;;) {
			if (!parent) {
				setParent(node, 0, BLACK);
				break;
			} else if (isBlack(parent))
				break;

			gparent = redParent(parent);

			tmp = (gparent->*nodeMember).right;
			if (parent != tmp) {
				if (tmp && isRed(tmp)) {
					setParent(tmp, gparent, BLACK);
					setParent(parent, gparent, BLACK);
					node = gparent;
					parent = getParent(node);
					setParent(node, parent, RED);
					continue;
				}

				tmp = (parent->*nodeMember).right;
				if (node == tmp) {
					tmp = (node->*nodeMember).left;
					(parent->*nodeMember).right = tmp;
					(node->*nodeMember).left = parent;
					if (tmp)
						setParent(tmp, parent, BLACK);
					setParent(parent, node, RED);
					(parent->*nodeMember).rotate(&(node->*nodeMember), nodeMember); // augment_rotate(parent, node);
					parent = node;
					tmp = (node->*nodeMember).right;
				}

				(gparent->*nodeMember).left = tmp;
				(parent->*nodeMember).right = gparent;
				if (tmp)
					setParent(tmp, gparent, BLACK);
				rotateSetParents(gparent, parent, RED);
				(gparent->*nodeMember).rotate(&(parent->*nodeMember), nodeMember); // augment_rotate(gparent, parent);
				break;
			} else {
				tmp = (gparent->*nodeMember).left;
				if (tmp && isRed(tmp)) {
					setParent(tmp, gparent, BLACK);
					setParent(parent, gparent, BLACK);
					node = gparent;
					parent = getParent(node);
					setParent(node, parent, RED);
					continue;
				}

				tmp = (parent->*nodeMember).left;
				if (node == tmp) {
					tmp = (node->*nodeMember).right;
					(parent->*nodeMember).left = tmp;
					(node->*nodeMember).right = parent;
					if (tmp)
						setParent(tmp, parent, BLACK);
					setParent(parent, node, RED);
					(parent->*nodeMember).rotate(&(node->*nodeMember), nodeMember); // augment_rotate(parent, node);
					parent = node;
					tmp = (node->*nodeMember).left;
				}

				(gparent->*nodeMember).right = tmp;
				(parent->*nodeMember).left = gparent;
				if (tmp)
					setParent(tmp, gparent, BLACK);
				rotateSetParents(gparent, parent, RED);
				(gparent->*nodeMember).rotate(&(parent->*nodeMember), nodeMember); // augment_rotate(gparent, parent);
				break;
			}
		}
	}

	static int blackHeight(T *r)
	{
		if(r == 0) {
			return 1;
		}

		int leftBlackHeight = blackHeight((r->*nodeMember).left);
		if(leftBlackHeight == 0) {
			return leftBlackHeight;
		}

		int rightBlackHeight = blackHeight((r->*nodeMember).right);
		if(rightBlackHeight == 0) {
			return rightBlackHeight;
		}

		if(leftBlackHeight != rightBlackHeight) {
			return 0;
		}
		else {
			return leftBlackHeight + (isBlack(r) ? 1 : 0);
		}
	}

	// no red node has red children
	static bool checkRedProperty(T *r)
	{
		if(r == 0) {
			return true;
		}

		if(!checkRedProperty((r->*nodeMember).left)) {
			return false;
		}

		if(!checkRedProperty((r->*nodeMember).right)) {
			return false;
		}

		if(isRed(r) && (((r->*nodeMember).left && isRed((r->*nodeMember).left)) || ((r->*nodeMember).right && isRed((r->*nodeMember).right)))) {
			return false;
		}

		return true;
	}

	// max height is twince as high as min height in the worst case
	static bool isBalanced(T *r, int &maxh, int &minh)
	{
		// Base case
		if(r == 0) {
			maxh = minh = 0;
			return true;
		}

		int lmxh, lmnh;
		int rmxh, rmnh;

		if(!isBalanced((r->*nodeMember).left, lmxh, lmnh)) {
			return false;
		}

		if(!isBalanced((r->*nodeMember).right, rmxh, rmnh)) {
			return false;
		}

		maxh = (lmxh > rmxh ? lmxh : rmxh) + 1;
		minh = (lmnh < rmnh ? lmnh : rmnh) + 1;

		if(maxh <= (2 * minh)) {
			return true;
		}

		return false;
	}

	public:
	static int cmp(K k, T *n)
	{
		return Comp::cmp(k, n);
	}

	static int cmp(T *a, T *b)
	{
		return Comp::cmp(a, b);
	}

	void init()
	{
		root = 0;
	}

	RBTreeGeneric()
	{
		init();
	}

	RBTreeGeneric(const char *NO_INIT)
	{
		(void)NO_INIT;
	}

	T* getRoot() const
	{
		return root;
	}

	T* search(K key) const
	{
		T *node = root;
		while(node != 0) {
			const int result = cmp(key, node);
			if(result < 0) {
				node = (node->*nodeMember).left;
			}
			else if(result > 0) {
				node = (node->*nodeMember).right;
			}
			else {
				return node;
			}
		}
		return 0;
	}

	T* ceil(K key) const
	{
		T *ceilNode = 0;
		T *node = root;
		while(node != 0) {
			const int result = cmp(key, node);
			if(result < 0) {
				ceilNode = node;
				node = (node->*nodeMember).left;
			}
			else if(result > 0) {
				node = (node->*nodeMember).right;
			}
			else {
				return node;
			}
		}
		return ceilNode;
	}

	T* floor(K key) const
	{
		T *floorNode = 0;
		T *node = root;
		while(node != 0) {
			const int result = cmp(key, node);
			if(result < 0) {
				node = (node->*nodeMember).left;
			}
			else if(result > 0) {
				floorNode = node;
				node = (node->*nodeMember).right;
			}
			else {
				return node;
			}
		}
		return floorNode;
	}

	T *first() const
	{
		T *n = root;
		if(!n) {
			return 0;
		}
		while((n->*nodeMember).left) {
			n = (n->*nodeMember).left;
		}
		return n;
	}

	T *last() const
	{
		T  *n = root;
		if(!n) {
			return 0;
		}
		while((n->*nodeMember).right) {
			n = (n->*nodeMember).right;
		}
		return n;
	}

	T* min() const
	{
		return first();
	}

	T* max() const
	{
		return last();
	}

	T* insert(T *item)
	{
		T **newNode = &root;
		T *parent = 0;

		// figure out where to put item
		// update size afterwards
		while(*newNode) {
			int result = cmp(item, *newNode);

			parent = *newNode;
			if(result < 0) {
				newNode = &(((*newNode)->*nodeMember).left);
			}
			else if(result > 0) {
				newNode = &(((*newNode)->*nodeMember).right);
			}
			else {
				// this item was already in the tree, do not update the size
				return *newNode;
			}
		}

		// first link the node, so it has a parent
		linkNode(item, parent, newNode);

		// now propagate the size going up the tree
		(item->*nodeMember).propagate(0, nodeMember);

		// rebalance the tree
		insertInternal(item);

		return item;
	}

	void remove(T *node)
	{
		T *parent = eraseAugmented(node);
		if (parent) {
			T *node = 0, *sibling, *tmp1, *tmp2;

			for(;;) {
				sibling = (parent->*nodeMember).right;
				if (node != sibling) {
					if (isRed(sibling)) {
						tmp1 = (sibling->*nodeMember).left;
						(parent->*nodeMember).right = tmp1;
						(sibling->*nodeMember).left = parent;
						setParent(tmp1, parent, BLACK);
						rotateSetParents(parent, sibling, RED);
						(parent->*nodeMember).rotate(&(sibling->*nodeMember), nodeMember); // augment_rotate(parent, sibling);
						sibling = tmp1;
					}
					tmp1 = (sibling->*nodeMember).right;
					if (!tmp1 || isBlack(tmp1)) {
						tmp2 = (sibling->*nodeMember).left;
						if (!tmp2 || isBlack(tmp2)) {
							setParent(sibling, parent, RED);
							if (isRed(parent))
								setBlack(parent);
							else {
								node = parent;
								parent = getParent(node);
								if (parent)
									continue;
							}
							break;
						}
						tmp1 = (tmp2->*nodeMember).right;
						(sibling->*nodeMember).left = tmp1;
						(tmp2->*nodeMember).right = sibling;
						(parent->*nodeMember).right = tmp2;
						if (tmp1)
							setParent(tmp1, sibling, BLACK);
						(sibling->*nodeMember).rotate(&(tmp2->*nodeMember), nodeMember); // augment_rotate(sibling, tmp2);
						tmp1 = sibling;
						sibling = tmp2;
					}
					tmp2 = (sibling->*nodeMember).left;
					(parent->*nodeMember).right = tmp2;
					(sibling->*nodeMember).left = parent;
					setParent(tmp1, sibling, BLACK);
					if (tmp2)
						setParent(tmp2, parent);
					rotateSetParents(parent, sibling, BLACK);
					(parent->*nodeMember).rotate(&(sibling->*nodeMember), nodeMember); // augment_rotate(parent, sibling);
					break;
				} else {
					sibling = (parent->*nodeMember).left;
					if (isRed(sibling)) {
						tmp1 = (sibling->*nodeMember).right;
						(parent->*nodeMember).left = tmp1;
						(sibling->*nodeMember).right = parent;
						setParent(tmp1, parent, BLACK);
						rotateSetParents(parent, sibling, RED);
						(parent->*nodeMember).rotate(&(sibling->*nodeMember), nodeMember); // augment_rotate(parent, sibling);
						sibling = tmp1;
					}
					tmp1 = (sibling->*nodeMember).left;
					if (!tmp1 || isBlack(tmp1)) {
						tmp2 = (sibling->*nodeMember).right;
						if (!tmp2 || isBlack(tmp2)) {
							setParent(sibling, parent, RED);
							if (isRed(parent))
								setBlack(parent);
							else {
								node = parent;
								parent = getParent(node);
								if (parent)
									continue;
							}
							break;
						}
						tmp1 = (tmp2->*nodeMember).left;
						(sibling->*nodeMember).right = tmp1;
						(tmp2->*nodeMember).left = sibling;
						(parent->*nodeMember).left = tmp2;
						if (tmp1)
							setParent(tmp1, sibling, BLACK);
						(sibling->*nodeMember).rotate(&(tmp2->*nodeMember), nodeMember); // augment_rotate(sibling, tmp2);
						tmp1 = sibling;
						sibling = tmp2;
					}
					tmp2 = (sibling->*nodeMember).right;
					(parent->*nodeMember).left = tmp2;
					(sibling->*nodeMember).right = parent;
					setParent(tmp1, sibling, BLACK);
					if (tmp2)
						setParent(tmp2, parent);
					rotateSetParents(parent, sibling, BLACK);
					(parent->*nodeMember).rotate(&(sibling->*nodeMember), nodeMember); // augment_rotate(parent, sibling);
					break;
				}
			}
		}
	}

	void replace(T *victim, T *newNode)
	{
		T *parent = getParent(victim);

		changeChild(victim, newNode, parent);
		if((victim->*nodeMember).left) {
			setParent((victim->*nodeMember).left, newNode);
		}
		if((victim->*nodeMember).right) {
			setParent((victim->*nodeMember).right, newNode);
		}

		newNode->*nodeMember = victim->*nodeMember;
	}

	static T *next(T *node)
	{
		T *parent;
		if((node->*nodeMember).right) {
			node = (node->*nodeMember).right; 
			while((node->*nodeMember).left) {
				node=(node->*nodeMember).left;
			}
			return (T *)node;
		}

		while((parent = getParent(node)) && node == (parent->*nodeMember).right) {
			node = parent;
		}
		return parent;
	}

	static T *prev(T *node)
	{
		T *parent;
		if((node->*nodeMember).left) {
			node = (node->*nodeMember).left; 
			while((node->*nodeMember).right) {
				node=(node->*nodeMember).right;
			}
			return (T *)node;
		}

		while((parent = getParent(node)) && node == (parent->*nodeMember).left) {
			node = parent;
		}
		return parent;
	}

	bool isEmpty() const
	{
		return root == 0;
	}

	bool check() const
	{
		if(blackHeight(root) == 0) {
			return false;
		}

		if(!checkRedProperty(root)) {
			return false;
		}

		int d1, d2;
		if(!isBalanced(root, d1, d2)) {
			return false;
		}

		return true;
	}
};

} // namespace adt
} // namespace lib

#endif /* LIB_ADT_RB_TREE_GENERIC_HEADER */
