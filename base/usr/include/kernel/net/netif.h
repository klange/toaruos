#pragma once

#include <kernel/vfs.h>

int net_add_interface(const char * name, fs_node_t * deviceNode);
