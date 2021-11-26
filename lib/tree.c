/**
 * @brief General-purpose tree implementation
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2018 K. Lange
 */

#ifdef _KERNEL_
#	include <kernel/system.h>
#else
#	include <stddef.h>
#	include <stdlib.h>
#endif

#include <toaru/tree.h>

tree_t * tree_create(void) {
	/* Create a new tree */
	tree_t * out = malloc(sizeof(tree_t));
	out->nodes  = 0;
	out->root   = NULL;
	return out;
}

void tree_set_root(tree_t * tree, void * value) {
	/* Set the root node for a new tree. */
	tree_node_t * root = tree_node_create(value);
	tree->root = root;
	tree->nodes = 1;
}

void tree_node_destroy(tree_node_t * node) {
	/* Free the contents of a node and its children, but not the nodes themselves */
	foreach(child, node->children) {
		tree_node_destroy((tree_node_t *)child->value);
	}
	free(node->value);
}

void tree_destroy(tree_t * tree) {
	/* Free the contents of a tree, but not the nodes */
	if (tree->root) {
		tree_node_destroy(tree->root);
	}
}

void tree_node_free(tree_node_t * node) {
	/* Free a node and its children, but not their contents */
	if (!node) return;
	foreach(child, node->children) {
		tree_node_free(child->value);
	}
	free(node);
}

void tree_free(tree_t * tree) {
	/* Free all of the nodes in a tree, but not their contents */
	tree_node_free(tree->root);
}

tree_node_t * tree_node_create(void * value) {
	/* Create a new tree node pointing to the given value */
	tree_node_t * out = malloc(sizeof(tree_node_t));
	out->value = value;
	out->children = list_create();
	out->parent = NULL;
	return out;
}

void tree_node_insert_child_node(tree_t * tree, tree_node_t * parent, tree_node_t * node) {
	/* Insert a node as a child of parent */
	list_insert(parent->children, node);
	node->parent = parent;
	tree->nodes++;
}

tree_node_t * tree_node_insert_child(tree_t * tree, tree_node_t * parent, void * value) {
	/* Insert a (fresh) node as a child of parent */
	tree_node_t * out = tree_node_create(value);
	tree_node_insert_child_node(tree, parent, out);
	return out;
}

tree_node_t * tree_node_find_parent(tree_node_t * haystack, tree_node_t * needle) {
	/* Recursive node part of tree_find_parent */
	tree_node_t * found = NULL;
	foreach(child, haystack->children) {
		if (child->value == needle) {
			return haystack;
		}
		found = tree_node_find_parent((tree_node_t *)child->value, needle);
		if (found) {
			break;
		}
	}
	return found;
}

tree_node_t * tree_find_parent(tree_t * tree, tree_node_t * node) {
	/* Return the parent of a node, inefficiently. */
	if (!tree->root) return NULL;
	return tree_node_find_parent(tree->root, node);
}

size_t tree_count_children(tree_node_t * node) {
	/* return the number of children this node has */
	if (!node) return 0;
	if (!node->children) return 0;
	size_t out = node->children->length;
	foreach(child, node->children) {
		out += tree_count_children((tree_node_t *)child->value);
	}
	return out;
}

void tree_node_parent_remove(tree_t * tree, tree_node_t * parent, tree_node_t * node) {
	/* remove a node when we know its parent; update node counts for the tree */
	tree->nodes -= tree_count_children(node) + 1;
	list_delete(parent->children, list_find(parent->children, node));
	tree_node_free(node);
}

void tree_node_remove(tree_t * tree, tree_node_t * node) {
	/* remove an entire branch given its root */
	tree_node_t * parent = node->parent;
	if (!parent) {
		if (node == tree->root) {
			tree->nodes = 0;
			tree->root  = NULL;
			tree_node_free(node);
		}
	}
	tree_node_parent_remove(tree, parent, node);
}

void tree_remove(tree_t * tree, tree_node_t * node) {
	/* Remove this node and move its children into its parent's list of children */
	tree_node_t * parent = node->parent;
	/* This is something we just can't do. We don't know how to merge our
	 * children into our "parent" because then we'd have more than one root node.
	 * A good way to think about this is actually what this tree struct
	 * primarily exists for: processes. Trying to remove the root is equivalent
	 * to trying to kill init! Which is bad. We immediately fault on such
	 * a case anyway ("Tried to kill init, shutting down!").
	 */
	if (!parent) return;
	tree->nodes--;
	list_delete(parent->children, list_find(parent->children, node));
	foreach(child, node->children) {
		/* Reassign the parents */
		((tree_node_t *)child->value)->parent = parent;
	}
	list_merge(parent->children, node->children);
	free(node);
}

void tree_remove_reparent_root(tree_t * tree, tree_node_t * node) {
	/* Remove this node and move its children into the root children */
	tree_node_t * parent = node->parent;
	if (!parent) return;
	tree->nodes--;
	list_delete(parent->children, list_find(parent->children, node));
	foreach(child, node->children) {
		/* Reassign the parents */
		((tree_node_t *)child->value)->parent = tree->root;
	}
	list_merge(tree->root->children, node->children);
	free(node);
}

void tree_break_off(tree_t * tree, tree_node_t * node) {
	tree_node_t * parent = node->parent;
	if (!parent) return;
	list_delete(parent->children, list_find(parent->children, node));
}

tree_node_t * tree_node_find(tree_node_t * node, void * search, tree_comparator_t comparator) {
	if (comparator(node->value,search)) {
		return node;
	}
	tree_node_t * found;
	foreach(child, node->children) {
		found = tree_node_find((tree_node_t *)child->value, search, comparator);
		if (found) return found;
	}
	return NULL;
}

tree_node_t * tree_find(tree_t * tree, void * value, tree_comparator_t comparator) {
	return tree_node_find(tree->root, value, comparator);
}
