# New VFS Design Documentation

## For reference: virtual functions in the old VFS

```c
typedef uint32_t (*read_type_t) (struct fs_node *, uint32_t, uint32_t, uint8_t *);
typedef uint32_t (*write_type_t) (struct fs_node *, uint32_t, uint32_t, uint8_t *);
typedef void (*open_type_t) (struct fs_node *, unsigned int flags);
typedef void (*close_type_t) (struct fs_node *);
typedef struct dirent *(*readdir_type_t) (struct fs_node *, uint32_t);
typedef struct fs_node *(*finddir_type_t) (struct fs_node *, char *name);
typedef void (*create_type_t) (struct fs_node *, char *name, uint16_t permission);
typedef void (*unlink_type_t) (struct fs_node *, char *name);
typedef void (*mkdir_type_t) (struct fs_node *, char *name, uint16_t permission);
typedef int (*ioctl_type_t) (struct fs_node *, int request, void * argp);
typedef int (*get_size_type_t) (struct fs_node *);
typedef int (*chmod_type_t) (struct fs_node *, int mode);
```

## Design Goals of the New VFS

1. Large file support from the beginning
   All 64-bit offsets and sizes.
2. Cached virtual file objects
   We currently have "objects that happen to exist from a malloc" and "objects we created from an open() call"...
3. Reference counting so we can actually close files
   Specifically, so we can close everything on exit.
4. Unix pipes
   The focus of this rewrite is supporting closing of pipes...
5. Permissions
   The current VFS can... uh... store them, but doesn't actually respect them.
6. Support for symbolic links
   Yeah.

