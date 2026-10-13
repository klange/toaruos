#pragma once

/**
 * Embedded tree entry.
 */

struct etree {
	struct etree * et_parent;
	struct etree * et_children;
	struct etree * et_sibleft;
	struct etree * et_sibright;
};

static inline void __etree_insert_child(struct etree * parent, struct etree * elem) {
	elem->et_parent = parent;
	if (!parent->et_children) {
		parent->et_children = elem->et_sibleft = elem->et_sibright = elem;
	} else {
		elem->et_sibright = parent->et_children;
		elem->et_sibleft = parent->et_children->et_sibleft;
		parent->et_children->et_sibleft->et_sibright = elem;
		parent->et_children->et_sibleft = elem;
	}
}

#define etree_insert_child(parent, child, field) \
	__etree_insert_child(&parent->field, &child->field)

static inline void * __etree_first_child(struct etree * parent, size_t field_offset) {
	struct etree * child = parent->et_children;
	if (!child) return NULL;
	return (void*)((char*)child - field_offset);
}

static inline void * __etree_next_sibling(struct etree * parent, struct etree * child, size_t field_offset) {
	struct etree * next = child->et_sibright;
	if (next == parent->et_children) return NULL;
	return (void*)((char *)next - field_offset);
}

#define etree_children_foreach(itername, node, field) \
	for (typeof(node) itername = __etree_first_child(&(node -> field), offsetof(typeof(*node),field)); \
		itername; itername = __etree_next_sibling(&(node -> field), &itername->field, offsetof(typeof(*node),field)))

static inline void * __etree_find(struct etree * node, size_t field_offset, void * needle, int (*comparator)(void *, void *)) {
	void * val = (void*)((char *)node - field_offset);
	if (comparator(val, needle)) return val;
	struct etree * n = node->et_children;
	while (n) {
		void * maybe = __etree_find(n, field_offset, needle, comparator);
		if (maybe) return maybe;
		n = n->et_sibright;
		if (n == node->et_children) break;
	}
	return NULL;
}

#define etree_find(parent, field, needle, comp) \
	__etree_find(&(parent -> field), offsetof(typeof(*parent), field), needle, comp)

static inline void * __etree_detach_reparent(struct etree * node, struct etree *nparent) {
	if (node->et_sibright == node) {
		struct etree * parent = node->et_parent;
		if (parent) parent->et_children = NULL;
	} else {
		struct etree * parent = node->et_parent;
		if (parent) {
			if (parent->et_children == node) parent->et_children = node->et_sibright;
			node->et_sibright->et_sibleft = node->et_sibleft;
			node->et_sibleft->et_sibright = node->et_sibright;
			node->et_sibright = node->et_sibleft = node;
		}
	}
	node->et_parent = NULL;

	if (node->et_children) {
		struct etree * n = node->et_children;
		if (n == n->et_sibright) {
			__etree_insert_child(nparent, n);
		} else {
			while (n) {
				n->et_parent = nparent;
				n = n->et_sibright;
				if (n == node->et_children) break;
			}
			if (!nparent->et_children) {
				nparent->et_children = n;
			} else {
				struct etree * mtail = n->et_sibleft;
				struct etree * ptail = nparent->et_children->et_sibleft;
				ptail->et_sibright = n;
				n->et_sibleft = ptail;
				mtail->et_sibright = nparent->et_children;
				nparent->et_children->et_sibleft = mtail;
			}
		}
	}
}

#define etree_detach_reparent(node, nparent, field) \
	__etree_detach_reparent(&(node -> field), &(nparent -> field))

static inline void * __etree_parent(struct etree * node, size_t field_offset) {
	if (!node->et_parent) return NULL;
	return (void*)((char *)node->et_parent - field_offset);
}

#define etree_parent(node, field) \
	__etree_parent(&(node -> field), offsetof(typeof(*node), field))
