#pragma once

#include <stdint.h>
#include <stddef.h>
#include <kernel/vfs.h>

extern fs_node_t * ramdisk_mount(uintptr_t, size_t);
