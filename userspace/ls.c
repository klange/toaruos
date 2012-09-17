/*
 * ls
 *
 * Lists files in a directory, with nice color
 * output like any modern ls should have.
 */


#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <unistd.h>
#include <dirent.h>

#define MIN_COL_SPACING 2

#define EXE_COLOR		"1;32"
#define DIR_COLOR		"1;34"
#define REG_COLOR		"0"
#define MEDIA_COLOR		""
#define SYM_COLOR		""
#define BROKEN_COLOR	"1;"

#define DEFAULT_TERM_WIDTH 80
#define DEFAULT_TERM_HEIGHT 24


/* Shit that belongs as a separate data structure */

typedef struct node {
	struct node * next;
	struct node * prev;
	void * value;
} __attribute__((packed)) node_t;

typedef struct {
	node_t * head;
	node_t * tail;
	size_t length;
} __attribute__((packed)) list_t;

void list_destroy(list_t * list);
void list_free(list_t * list);
void list_append(list_t * list, node_t * item);
void list_insert(list_t * list, void * item);
list_t * list_create();
node_t * list_find(list_t * list, void * value);
void list_remove(list_t * list, size_t index);
void list_delete(list_t * list, node_t * node);
node_t * list_pop(list_t * list);
node_t * list_dequeue(list_t * list);
list_t * list_copy(list_t * original);
void list_merge(list_t * target, list_t * source);

#define foreach(i, list) for (node_t * i = list->head; i != NULL; i = i->next)

void list_destroy(list_t * list) {
	/* Free all of the contents of a list */
	node_t * n = list->head;
	while (n) {
		free(n->value);
		n = n->next;
	}
}

void list_free(list_t * list) {
	/* Free the actual structure of a list */
	node_t * n = list->head;
	while (n) {
		node_t * s = n->next;
		free(n);
		n = s;
	}
}

void list_append(list_t * list, node_t * node) {
	/* Insert a node onto the end of a list */
	if (!list->tail) {
		list->head = node;
	} else {
		list->tail->next = node;
		node->prev = list->tail;
	}
	list->tail = node;
	list->length++;
}

void list_insert(list_t * list, void * item) {
	/* Insert an item into a list */
	node_t * node = malloc(sizeof(node_t));
	node->value = item;
	node->next  = NULL;
	node->prev  = NULL;
	list_append(list, node);
}

list_t * list_create() {
	/* Create a fresh list */
	list_t * out = malloc(sizeof(list_t));
	out->head = NULL;
	out->tail = NULL;
	out->length = 0;
	return out;
}

node_t * list_find(list_t * list, void * value) {
	foreach(item, list) {
		if (item->value == value) {
			return item;
		}
	}
	return NULL;
}

void list_remove(list_t * list, size_t index) {
	/* remove index from the list */
	if (index > list->length) return;
	size_t i = 0;
	node_t * n = list->head;
	while (i < index) {
		n = n->next;
		i++;
	}
	list_delete(list, n);
}

void list_delete(list_t * list, node_t * node) {
	/* remove node from the list */
	if (node == list->head) {
		list->head = node->next;
	}
	if (node == list->tail) {
		list->tail = node->prev;
	}
	if (node->prev) {
		node->prev->next = node->next;
	}
	if (node->next) {
		node->next->prev = node->prev;
	}
	list->length--;
}

node_t * list_pop(list_t * list) {
	/* Remove and return the last value in the list
	 * If you don't need it, you still probably want to free it!
	 * Try free(list_pop(list)); !
	 * */
	if (!list->tail) return NULL;
	node_t * out = list->tail;
	list_delete(list, list->tail);
	return out;
}

node_t * list_dequeue(list_t * list) {
	if (!list->head) return NULL;
	node_t * out = list->head;
	list_delete(list, list->head);
	return out;
}

list_t * list_copy(list_t * original) {
	/* Create a new copy of original */
	list_t * out = list_create();
	node_t * node = original->head;
	while (node) {
		list_insert(out, node->value);
	}
	return out;
}

void list_merge(list_t * target, list_t * source) {
	/* Destructively merges source into target */
	if (target->tail) {
		target->tail->next = source->head;
	} else {
		target->head = source->head;
	}
	if (source->tail) {
		target->tail = source->tail;
	}
	target->length += source->length;
	free(source);
}


/* Shit that belongs in the clib */

int max (int a, int b) {
	if (a > b) {
		return a;
	} else {
		return b;
	}
}

/* Should be kept in sync with 'struct dirent' in kernel/include/fs.h */
// TODO: This thing is broken! Oh fuck!
/*
int readdir_r (DIR           *restrict  dirp,
               struct dirent *restrict  entry,
               struct dirent **restrict result) {

	if ((dirp == NULL) || (dirp->fd == -1)) {
		*result = NULL;
		return -1;
	}

	struct dirent * ent = malloc(sizeof(struct dirent));

	// Find the guy
	int index = 0;
	while (1) {
		int ret = syscall_readdir(dirp->fd, index++, ent);
		if (ret == -1) {
			*result = NULL;
			return -1;
		}
	}

	return 0;
}
*/

/* The program */

int entcmp (const void * c1, const void * c2) {
	struct dirent * d1 = *(struct dirent **)c1;
	struct dirent * d2 = *(struct dirent **)c2;
	return strcmp(d1->d_name, d2->d_name);
}

void print_entry (const char * filename, const char * srcpath, int colwidth) {
	/* Figure out full relpath */
	char * relpath = malloc(strlen(srcpath) + strlen(filename) + 2);
	sprintf(relpath, "%s/%s", srcpath, filename);

	/* Classify file */
	struct stat statbuf;
	stat(relpath, &statbuf);
	free(relpath);

	const char * ansi_color_str;
	if (S_ISDIR(statbuf.st_mode)) {
		// Directory
		ansi_color_str = DIR_COLOR;
	} else if (statbuf.st_mode & 0111) {
		// Executable
		ansi_color_str = EXE_COLOR;
	} else {
		// Something else
		ansi_color_str = REG_COLOR;
	}


	/* Print the file name */
	printf("\033[%sm%s\033[0m", ansi_color_str, filename);

	/* Pad the rest of the column */
	for (int rem = colwidth - strlen(filename); rem > 0; rem--) {
		printf(" ");
	}
}

int main (int argc, char * argv[]) {

	/* Parse arguments */
	char * p = ".";
	int explicit_path_set = 0;
	int show_hidden = 0;

#if 0
	if (argc > 1) {
		int index, c;
		while ((c = getopt(argc, argv, "a")) != -1) {
			switch (c) {
				case 'a':
					show_hidden = 1;
					break;
			}
		}

		if (optind < argc) {
			p = argv[optind];
		}
	}
#else
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-a") == 0) {
			show_hidden = 1;
		} else if (!explicit_path_set) {
			p = argv[i];
			explicit_path_set = 1;
		}
	}
#endif

	/* Open the directory */
	DIR * dirp = opendir(p);
	if (dirp == NULL) {
		printf("no such directory\n");
		return -1;
	}

	/* Read the entries in the directory */
	list_t * ents_list = list_create();

	struct dirent * ent = readdir(dirp);
	while (ent != NULL) {
		if (show_hidden || (ent->d_name[0] != '.')) {
			struct dirent * entcpy = malloc(sizeof(struct dirent));
			memcpy(entcpy, ent, sizeof(struct dirent));
			list_insert(ents_list, (void *)entcpy);
		}

		ent = readdir(dirp);
	}
	closedir(dirp);

	/* Now, copy those entries into an array (for sorting) */
	struct dirent ** ents_array = malloc(sizeof(struct dirent *) * ents_list->length);
	int index = 0;
	node_t * node;
	foreach(node, ents_list) {
		ents_array[index++] = (struct dirent *)node->value;
	}
	list_free(ents_list);
	int numents = index;

	qsort(ents_array, numents, sizeof(struct dirent *), entcmp);

	/* Determine the gridding dimensions */
	int ent_max_len = 0;
	for (int i = 0; i < numents; i++) {
		ent_max_len = max(ent_max_len, strlen(ents_array[i]->d_name));
	}

	int term_width = DEFAULT_TERM_WIDTH; // For now, we assume 128
	int term_height = DEFAULT_TERM_HEIGHT;

	printf("\033[1003z");
	fflush(stdout);
	scanf("%d,%d", &term_width, &term_height);
	term_width -= 1;

	int col_ext = ent_max_len + MIN_COL_SPACING;
	int cols = ((term_width - ent_max_len) / col_ext) + 1;
#if 0
	fprintf(stderr, "Printing %dx%d grid (max width: %d)\n", cols, (numents/cols), ent_max_len);
#endif

	/* Print the entries */

	// Print rows
	for (int i = 0; i < numents;) {

		// Print columns on this row
		print_entry(ents_array[i++]->d_name, p, ent_max_len);

		for (int j = 0; (i < numents) && (j < (cols-1)); j++) {
			printf("  ");
			print_entry(ents_array[i++]->d_name, p, ent_max_len);
		}

		printf("\n");
	}
	free(ents_array);

	return 0;
}

/*
 * vim: tabstop=4
 * vim: shiftwidth=4
 * vim: noexpandtab
 */
