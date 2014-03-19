#include <system.h>
#include <module.h>
#include <fs.h>
#include <mod/shell.h>

extern fs_node_t * ext2_fs_mount(char * device, char * mount_path);

DEFINE_SHELL_FUNCTION(mount, "Mount an ext2 filesystem") {

	if (argc < 3) {
		fs_printf(tty, "Usage: %s device mount_path", argv[0]);
		return 1;
	}

	ext2_fs_mount(argv[1], argv[2]);

	return 0;
}

static int init(void) {
	BIND_SHELL_FUNCTION(mount);
	return 0;
}

static int fini(void) {
	return 0;
}

MODULE_DEF(ext2mount, init, fini);
MODULE_DEPENDS(debugshell);
MODULE_DEPENDS(ext2);
