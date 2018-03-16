.section .symbols

.extern alloc_frame
.type alloc_frame, @function

.extern args_parse
.type args_parse, @function

.extern args_present
.type args_present, @function

.extern args_value
.type args_value, @function

.extern assert_failed
.type assert_failed, @function

.extern atoi
.type atoi, @function

.extern bitset_clear
.type bitset_clear, @function

.extern bitset_ffub
.type bitset_ffub, @function

.extern bitset_free
.type bitset_free, @function

.extern bitset_init
.type bitset_init, @function

.extern bitset_set
.type bitset_set, @function

.extern bitset_test
.type bitset_test, @function

.extern boot_arg
.type boot_arg, @function

.extern boot_arg_extra
.type boot_arg_extra, @function

.extern boot_time
.type boot_time, @function

.extern bss
.type bss, @function

.extern calloc
.type calloc, @function

.extern canonicalize_path
.type canonicalize_path, @function

.extern chmod_fs
.type chmod_fs, @function

.extern chown_fs
.type chown_fs, @function

.extern cleanup_process
.type cleanup_process, @function

.extern clear_frame
.type clear_frame, @function

.extern clone
.type clone, @function

.extern clone_directory
.type clone_directory, @function

.extern clone_fs
.type clone_fs, @function

.extern clone_table
.type clone_table, @function

.extern close_fs
.type close_fs, @function

.extern close_pipe
.type close_pipe, @function

.extern close_pty_master
.type close_pty_master, @function

.extern close_pty_slave
.type close_pty_slave, @function

.extern cmdline
.type cmdline, @function

.extern cmos_dump
.type cmos_dump, @function

.extern code
.type code, @function

.extern copy_multiboot
.type copy_multiboot, @function

.extern copy_page_physical
.type copy_page_physical, @function

.extern create_file_fs
.type create_file_fs, @function

.extern create_kernel_tasklet
.type create_kernel_tasklet, @function

.extern current_directory
.type current_directory, @function

.extern current_process
.type current_process, @function

.extern data
.type data, @function

.extern debug_file
.type debug_file, @function

.extern debug_hook
.type debug_hook, @function

.extern debug_level
.type debug_level, @function

.extern _debug_print
.type _debug_print, @function

.extern debug_print_directory
.type debug_print_directory, @function

.extern debug_print_process_tree
.type debug_print_process_tree, @function

.extern debug_print_process_tree_node
.type debug_print_process_tree_node, @function

.extern debug_print_vfs_tree
.type debug_print_vfs_tree, @function

.extern debug_print_vfs_tree_node
.type debug_print_vfs_tree_node, @function

.extern debug_video_crash
.type debug_video_crash, @function

.extern default_name
.type default_name, @function

.extern delete_process
.type delete_process, @function

.extern disable_fpu
.type disable_fpu, @function

.extern dma_frame
.type dma_frame, @function

.extern dump_multiboot
.type dump_multiboot, @function

.extern enable_fpu
.type enable_fpu, @function

.extern end
.type end, @function

.extern enter_signal_handler
.type enter_signal_handler, @function

.extern enter_user_jmp
.type enter_user_jmp, @function

.extern enter_userspace
.type enter_userspace, @function

.extern exec
.type exec, @function

.extern exec_elf
.type exec_elf, @function

.extern exec_shebang
.type exec_shebang, @function

.extern fault_handler
.type fault_handler, @function

.extern finddir_fs
.type finddir_fs, @function

.extern first_frame
.type first_frame, @function

.extern first_n_frames
.type first_n_frames, @function

.extern fix_signal_stacks
.type fix_signal_stacks, @function

.extern fmts
.type fmts, @function

.extern fork
.type fork, @function

.extern fprintf
.type fprintf, @function

.extern fpu_install
.type fpu_install, @function

.extern fpu_thread
.type fpu_thread, @function

.extern frames
.type frames, @function

.extern free
.type free, @function

.extern free_frame
.type free_frame, @function

.extern frozen_stack
.type frozen_stack, @function

.extern fs_root
.type fs_root, @function

.extern fs_tree
.type fs_tree, @function

.extern fs_types
.type fs_types, @function

.extern gdt_flush
.type gdt_flush, @function

.extern gdt_install
.type gdt_install, @function

.extern gdt_set_gate
.type gdt_set_gate, @function

.extern get_date
.type get_date, @function

.extern get_mount_point
.type get_mount_point, @function

.extern get_next_pid
.type get_next_pid, @function

.extern get_page
.type get_page, @function

.extern getpid
.type getpid, @function

.extern get_time
.type get_time, @function

.extern gettimeofday
.type gettimeofday, @function

.extern halt_and_catch_fire
.type halt_and_catch_fire, @function

.extern handle_signal
.type handle_signal, @function

.extern hashmap_create
.type hashmap_create, @function

.extern hashmap_create_int
.type hashmap_create_int, @function

.extern hashmap_free
.type hashmap_free, @function

.extern hashmap_get
.type hashmap_get, @function

.extern hashmap_has
.type hashmap_has, @function

.extern hashmap_int_comp
.type hashmap_int_comp, @function

.extern hashmap_int_dupe
.type hashmap_int_dupe, @function

.extern hashmap_int_hash
.type hashmap_int_hash, @function

.extern hashmap_keys
.type hashmap_keys, @function

.extern hashmap_remove
.type hashmap_remove, @function

.extern hashmap_set
.type hashmap_set, @function

.extern hashmap_string_comp
.type hashmap_string_comp, @function

.extern hashmap_string_dupe
.type hashmap_string_dupe, @function

.extern hashmap_string_hash
.type hashmap_string_hash, @function

.extern hashmap_values
.type hashmap_values, @function

.extern has_permission
.type has_permission, @function

.extern heap_end
.type heap_end, @function

.extern heap_install
.type heap_install, @function

.extern idt_install
.type idt_install, @function

.extern idt_load
.type idt_load, @function

.extern idt_set_gate
.type idt_set_gate, @function

.extern init_fpu
.type init_fpu, @function

.extern initial_esp
.type initial_esp, @function

.extern initialize_process_tree
.type initialize_process_tree, @function

.extern inportb
.type inportb, @function

.extern inportl
.type inportl, @function

.extern inports
.type inports, @function

.extern inportsm
.type inportsm, @function

.extern int_disable
.type int_disable, @function

.extern int_enable
.type int_enable, @function

.extern int_resume
.type int_resume, @function

.extern invalidate_page_tables
.type invalidate_page_tables, @function

.extern invalidate_tables_at
.type invalidate_tables_at, @function

.extern invalid_op
.type invalid_op, @function

.extern ioctl_fs
.type ioctl_fs, @function

.extern ioctl_pty_master
.type ioctl_pty_master, @function

.extern ioctl_pty_slave
.type ioctl_pty_slave, @function

.extern _irq0
.type _irq0, @function

.extern _irq1
.type _irq1, @function

.extern _irq10
.type _irq10, @function

.extern _irq11
.type _irq11, @function

.extern _irq12
.type _irq12, @function

.extern _irq13
.type _irq13, @function

.extern _irq14
.type _irq14, @function

.extern _irq15
.type _irq15, @function

.extern _irq2
.type _irq2, @function

.extern _irq3
.type _irq3, @function

.extern _irq4
.type _irq4, @function

.extern _irq5
.type _irq5, @function

.extern _irq6
.type _irq6, @function

.extern _irq7
.type _irq7, @function

.extern _irq8
.type _irq8, @function

.extern _irq9
.type _irq9, @function

.extern irq_ack
.type irq_ack, @function

.extern irq_handler
.type irq_handler, @function

.extern irq_install
.type irq_install, @function

.extern irq_install_handler
.type irq_install_handler, @function

.extern irq_uninstall_handler
.type irq_uninstall_handler, @function

.extern isdeadly
.type isdeadly, @function

.extern _isr0
.type _isr0, @function

.extern _isr1
.type _isr1, @function

.extern _isr10
.type _isr10, @function

.extern _isr11
.type _isr11, @function

.extern _isr12
.type _isr12, @function

.extern _isr127
.type _isr127, @function

.extern _isr13
.type _isr13, @function

.extern _isr14
.type _isr14, @function

.extern _isr15
.type _isr15, @function

.extern _isr16
.type _isr16, @function

.extern _isr17
.type _isr17, @function

.extern _isr18
.type _isr18, @function

.extern _isr19
.type _isr19, @function

.extern _isr2
.type _isr2, @function

.extern _isr20
.type _isr20, @function

.extern _isr21
.type _isr21, @function

.extern _isr22
.type _isr22, @function

.extern _isr23
.type _isr23, @function

.extern _isr24
.type _isr24, @function

.extern _isr25
.type _isr25, @function

.extern _isr26
.type _isr26, @function

.extern _isr27
.type _isr27, @function

.extern _isr28
.type _isr28, @function

.extern _isr29
.type _isr29, @function

.extern _isr3
.type _isr3, @function

.extern _isr30
.type _isr30, @function

.extern _isr31
.type _isr31, @function

.extern _isr4
.type _isr4, @function

.extern _isr5
.type _isr5, @function

.extern _isr6
.type _isr6, @function

.extern _isr7
.type _isr7, @function

.extern _isr8
.type _isr8, @function

.extern _isr9
.type _isr9, @function

.extern isrs_install
.type isrs_install, @function

.extern isrs_install_handler
.type isrs_install_handler, @function

.extern isrs_uninstall_handler
.type isrs_uninstall_handler, @function

.extern is_update_in_progress
.type is_update_in_progress, @function

.extern is_valid_process
.type is_valid_process, @function

.extern __kernel_arch
.type __kernel_arch, @function

.extern kernel_args_map
.type kernel_args_map, @function

.extern __kernel_build_date
.type __kernel_build_date, @function

.extern __kernel_build_time
.type __kernel_build_time, @function

.extern __kernel_compiler_version
.type __kernel_compiler_version, @function

.extern kernel_directory
.type kernel_directory, @function

.extern kernel_heap_alloc_point
.type kernel_heap_alloc_point, @function

.extern kernel_idle_task
.type kernel_idle_task, @function

.extern __kernel_name
.type __kernel_name, @function

.extern kernel_symbols_end
.type kernel_symbols_end, @function

.extern kernel_symbols_start
.type kernel_symbols_start, @function

.extern __kernel_version_codename
.type __kernel_version_codename, @function

.extern __kernel_version_format
.type __kernel_version_format, @function

.extern __kernel_version_lower
.type __kernel_version_lower, @function

.extern __kernel_version_major
.type __kernel_version_major, @function

.extern __kernel_version_minor
.type __kernel_version_minor, @function

.extern __kernel_version_suffix
.type __kernel_version_suffix, @function

.extern kexit
.type kexit, @function

.extern kmain
.type kmain, @function

.extern kmalloc
.type kmalloc, @function

.extern kmalloc_p
.type kmalloc_p, @function

.extern kmalloc_real
.type kmalloc_real, @function

.extern kmalloc_startat
.type kmalloc_startat, @function

.extern kopen
.type kopen, @function

.extern kopen_recur
.type kopen_recur, @function

.extern krand
.type krand, @function

.extern kvmalloc
.type kvmalloc, @function

.extern kvmalloc_p
.type kvmalloc_p, @function

.extern lfind
.type lfind, @function

.extern list_append
.type list_append, @function

.extern list_append_after
.type list_append_after, @function

.extern list_append_before
.type list_append_before, @function

.extern list_copy
.type list_copy, @function

.extern list_create
.type list_create, @function

.extern list_delete
.type list_delete, @function

.extern list_dequeue
.type list_dequeue, @function

.extern list_destroy
.type list_destroy, @function

.extern list_find
.type list_find, @function

.extern list_free
.type list_free, @function

.extern list_index_of
.type list_index_of, @function

.extern list_insert
.type list_insert, @function

.extern list_insert_after
.type list_insert_after, @function

.extern list_insert_before
.type list_insert_before, @function

.extern list_merge
.type list_merge, @function

.extern list_pop
.type list_pop, @function

.extern list_remove
.type list_remove, @function

.extern make_pipe
.type make_pipe, @function

.extern make_process_ready
.type make_process_ready, @function

.extern make_unix_pipe
.type make_unix_pipe, @function

.extern malloc
.type malloc, @function

.extern map_to_physical
.type map_to_physical, @function

.extern map_vfs_directory
.type map_vfs_directory, @function

.extern mboot_ptr
.type mboot_ptr, @function

.extern memchr
.type memchr, @function

.extern memcmp
.type memcmp, @function

.extern memcpy
.type memcpy, @function

.extern memmove
.type memmove, @function

.extern memory_total
.type memory_total, @function

.extern memory_use
.type memory_use, @function

.extern memrchr
.type memrchr, @function

.extern memset
.type memset, @function

.extern memsetw
.type memsetw, @function

.extern mkdir_fs
.type mkdir_fs, @function

.extern module_load
.type module_load, @function

.extern module_load_direct
.type module_load_direct, @function

.extern module_quickcheck
.type module_quickcheck, @function

.extern modules_get_list
.type modules_get_list, @function

.extern modules_get_symbols
.type modules_get_symbols, @function

.extern modules_install
.type modules_install, @function

.extern module_unload
.type module_unload, @function

.extern next_pid
.type next_pid, @function

.extern next_ready_process
.type next_ready_process, @function

.extern nframes
.type nframes, @function

.extern now
.type now, @function

.extern num_syscalls
.type num_syscalls, @function

.extern open_fs
.type open_fs, @function

.extern open_pipe
.type open_pipe, @function

.extern open_pty_master
.type open_pty_master, @function

.extern open_pty_slave
.type open_pty_slave, @function

.extern outportb
.type outportb, @function

.extern outportl
.type outportl, @function

.extern outports
.type outports, @function

.extern outportsm
.type outportsm, @function

.extern page_fault
.type page_fault, @function

.extern paging_finalize
.type paging_finalize, @function

.extern paging_install
.type paging_install, @function

.extern paging_mark_system
.type paging_mark_system, @function

.extern PciClassCodeTable
.type PciClassCodeTable, @function

.extern PciCommandFlags
.type PciCommandFlags, @function

.extern pci_device_lookup
.type pci_device_lookup, @function

.extern PciDevSelFlags
.type PciDevSelFlags, @function

.extern PciDevTable
.type PciDevTable, @function

.extern pci_find_type
.type pci_find_type, @function

.extern pci_read_field
.type pci_read_field, @function

.extern pci_scan
.type pci_scan, @function

.extern pci_scan_bus
.type pci_scan_bus, @function

.extern pci_scan_func
.type pci_scan_func, @function

.extern pci_scan_hit
.type pci_scan_hit, @function

.extern pci_scan_slot
.type pci_scan_slot, @function

.extern PciStatusFlags
.type PciStatusFlags, @function

.extern pci_vendor_lookup
.type pci_vendor_lookup, @function

.extern PciVenTable
.type PciVenTable, @function

.extern pci_write_field
.type pci_write_field, @function

.extern phys
.type phys, @function

.extern pipe_size
.type pipe_size, @function

.extern pipe_unsize
.type pipe_unsize, @function

.extern placement_pointer
.type placement_pointer, @function

.extern probable_function_name
.type probable_function_name, @function

.extern process_alert_node
.type process_alert_node, @function

.extern process_append_fd
.type process_append_fd, @function

.extern process_available
.type process_available, @function

.extern process_awaken_from_fswait
.type process_awaken_from_fswait, @function

.extern process_compare
.type process_compare, @function

.extern process_disown
.type process_disown, @function

.extern process_from_pid
.type process_from_pid, @function

.extern process_get_parent
.type process_get_parent, @function

.extern process_is_ready
.type process_is_ready, @function

.extern process_list
.type process_list, @function

.extern process_move_fd
.type process_move_fd, @function

.extern process_queue
.type process_queue, @function

.extern process_tree
.type process_tree, @function

.extern process_wait
.type process_wait, @function

.extern process_wait_nodes
.type process_wait_nodes, @function

.extern process_wake
.type process_wake, @function

.extern pty_available_input
.type pty_available_input, @function

.extern pty_available_output
.type pty_available_output, @function

.extern pty_create
.type pty_create, @function

.extern pty_install
.type pty_install, @function

.extern pty_ioctl
.type pty_ioctl, @function

.extern pty_list
.type pty_list, @function

.extern pty_master_create
.type pty_master_create, @function

.extern pty_new
.type pty_new, @function

.extern pty_slave_create
.type pty_slave_create, @function

.extern ramdisk
.type ramdisk, @function

.extern ramdisk_mount
.type ramdisk_mount, @function

.extern read_cmos
.type read_cmos, @function

.extern readdir_fs
.type readdir_fs, @function

.extern read_eip
.type read_eip, @function

.extern read_fs
.type read_fs, @function

.extern readlink_fs
.type readlink_fs, @function

.extern read_pipe
.type read_pipe, @function

.extern read_pty_master
.type read_pty_master, @function

.extern read_pty_slave
.type read_pty_slave, @function

.extern realloc
.type realloc, @function

.extern reap_process
.type reap_process, @function

.extern relative_time
.type relative_time, @function

.extern release_directory
.type release_directory, @function

.extern release_directory_for_exec
.type release_directory_for_exec, @function

.extern restore_fpu
.type restore_fpu, @function

.extern rets_from_sig
.type rets_from_sig, @function

.extern return_from_signal_handler
.type return_from_signal_handler, @function

.extern return_to_userspace
.type return_to_userspace, @function

.extern rfind
.type rfind, @function

.extern ring_buffer_available
.type ring_buffer_available, @function

.extern ring_buffer_create
.type ring_buffer_create, @function

.extern ring_buffer_destroy
.type ring_buffer_destroy, @function

.extern ring_buffer_interrupt
.type ring_buffer_interrupt, @function

.extern ring_buffer_read
.type ring_buffer_read, @function

.extern ring_buffer_select_wait
.type ring_buffer_select_wait, @function

.extern ring_buffer_size
.type ring_buffer_size, @function

.extern ring_buffer_unread
.type ring_buffer_unread, @function

.extern ring_buffer_write
.type ring_buffer_write, @function

.extern save_fpu
.type save_fpu, @function

.extern saves
.type saves, @function

.extern sbrk
.type sbrk, @function

.extern secs_of_month
.type secs_of_month, @function

.extern secs_of_years
.type secs_of_years, @function

.extern selectcheck_fs
.type selectcheck_fs, @function

.extern selectwait_fs
.type selectwait_fs, @function

.extern send_signal
.type send_signal, @function

.extern set_fpu_cw
.type set_fpu_cw, @function

.extern set_frame
.type set_frame, @function

.extern set_kernel_stack
.type set_kernel_stack, @function

.extern set_process_environment
.type set_process_environment, @function

.extern shm_install
.type shm_install, @function

.extern shm_obtain
.type shm_obtain, @function

.extern shm_release
.type shm_release, @function

.extern shm_release_all
.type shm_release_all, @function

.extern shm_tree
.type shm_tree, @function

.extern sleep_on
.type sleep_on, @function

.extern sleep_queue
.type sleep_queue, @function

.extern sleep_until
.type sleep_until, @function

.extern spawn_init
.type spawn_init, @function

.extern spawn_kidle
.type spawn_kidle, @function

.extern spawn_process
.type spawn_process, @function

.extern spin_init
.type spin_init, @function

.extern spin_lock
.type spin_lock, @function

.extern spin_unlock
.type spin_unlock, @function

.extern spin_wait
.type spin_wait, @function

.extern sprintf
.type sprintf, @function

.extern start
.type start, @function

.extern startswith
.type startswith, @function

.extern stpcpy
.type stpcpy, @function

.extern strchr
.type strchr, @function

.extern strchrnul
.type strchrnul, @function

.extern strcmp
.type strcmp, @function

.extern strcpy
.type strcpy, @function

.extern strcspn
.type strcspn, @function

.extern strdup
.type strdup, @function

.extern strlen
.type strlen, @function

.extern strpbrk
.type strpbrk, @function

.extern strrchr
.type strrchr, @function

.extern strspn
.type strspn, @function

.extern strstr
.type strstr, @function

.extern strtok_r
.type strtok_r, @function

.extern switch_fpu
.type switch_fpu, @function

.extern switch_next
.type switch_next, @function

.extern switch_page_directory
.type switch_page_directory, @function

.extern switch_task
.type switch_task, @function

.extern symbol_find
.type symbol_find, @function

.extern symlink_fs
.type symlink_fs, @function

.extern syscall_handler
.type syscall_handler, @function

.extern syscalls_install
.type syscalls_install, @function

.extern system
.type system, @function

.extern task_exit
.type task_exit, @function

.extern tasking_install
.type tasking_install, @function

.extern test_frame
.type test_frame, @function

.extern _timer_drift
.type _timer_drift, @function

.extern timer_drift
.type timer_drift, @function

.extern timer_handler
.type timer_handler, @function

.extern timer_install
.type timer_install, @function

.extern timer_phase
.type timer_phase, @function

.extern timer_subticks
.type timer_subticks, @function

.extern timer_ticks
.type timer_ticks, @function

.extern tokenize
.type tokenize, @function

.extern trace_pid
.type trace_pid, @function

.extern tree_break_off
.type tree_break_off, @function

.extern tree_count_children
.type tree_count_children, @function

.extern tree_create
.type tree_create, @function

.extern tree_destroy
.type tree_destroy, @function

.extern tree_find
.type tree_find, @function

.extern tree_find_parent
.type tree_find_parent, @function

.extern tree_free
.type tree_free, @function

.extern tree_node_create
.type tree_node_create, @function

.extern tree_node_destroy
.type tree_node_destroy, @function

.extern tree_node_find
.type tree_node_find, @function

.extern tree_node_find_parent
.type tree_node_find_parent, @function

.extern tree_node_free
.type tree_node_free, @function

.extern tree_node_insert_child
.type tree_node_insert_child, @function

.extern tree_node_insert_child_node
.type tree_node_insert_child_node, @function

.extern tree_node_parent_remove
.type tree_node_parent_remove, @function

.extern tree_node_remove
.type tree_node_remove, @function

.extern tree_remove
.type tree_remove, @function

.extern tree_remove_reparent_root
.type tree_remove_reparent_root, @function

.extern tree_set_root
.type tree_set_root, @function

.extern tss_flush
.type tss_flush, @function

.extern ubsan_debug
.type ubsan_debug, @function

.extern __ubsan_handle_add_overflow
.type __ubsan_handle_add_overflow, @function

.extern __ubsan_handle_builtin_unreachable
.type __ubsan_handle_builtin_unreachable, @function

.extern __ubsan_handle_divrem_overflow
.type __ubsan_handle_divrem_overflow, @function

.extern __ubsan_handle_mul_overflow
.type __ubsan_handle_mul_overflow, @function

.extern __ubsan_handle_negate_overflow
.type __ubsan_handle_negate_overflow, @function

.extern __ubsan_handle_out_of_bounds
.type __ubsan_handle_out_of_bounds, @function

.extern __ubsan_handle_shift_out_of_bounds
.type __ubsan_handle_shift_out_of_bounds, @function

.extern __ubsan_handle_sub_overflow
.type __ubsan_handle_sub_overflow, @function

.extern __ubsan_handle_type_mismatch
.type __ubsan_handle_type_mismatch, @function

.extern __ubsan_handle_vla_bound_not_positive
.type __ubsan_handle_vla_bound_not_positive, @function

.extern unlink_fs
.type unlink_fs, @function

.extern unswitch_fpu
.type unswitch_fpu, @function

.extern validate
.type validate, @function

.extern valloc
.type valloc, @function

.extern vasprintf
.type vasprintf, @function

.extern vfs_install
.type vfs_install, @function

.extern vfs_lock
.type vfs_lock, @function

.extern vfs_mount
.type vfs_mount, @function

.extern vfs_mount_type
.type vfs_mount_type, @function

.extern vfs_register
.type vfs_register, @function

.extern waitpid
.type waitpid, @function

.extern wakeup_queue
.type wakeup_queue, @function

.extern wakeup_queue_interrupted
.type wakeup_queue_interrupted, @function

.extern wakeup_sleepers
.type wakeup_sleepers, @function

.extern write_fs
.type write_fs, @function

.extern write_pipe
.type write_pipe, @function

.extern write_pty_master
.type write_pty_master, @function

.extern write_pty_slave
.type write_pty_slave, @function

.global kernel_symbols_start
kernel_symbols_start:

.long alloc_frame
.asciz "alloc_frame"

.long args_parse
.asciz "args_parse"

.long args_present
.asciz "args_present"

.long args_value
.asciz "args_value"

.long assert_failed
.asciz "assert_failed"

.long atoi
.asciz "atoi"

.long bitset_clear
.asciz "bitset_clear"

.long bitset_ffub
.asciz "bitset_ffub"

.long bitset_free
.asciz "bitset_free"

.long bitset_init
.asciz "bitset_init"

.long bitset_set
.asciz "bitset_set"

.long bitset_test
.asciz "bitset_test"

.long boot_arg
.asciz "boot_arg"

.long boot_arg_extra
.asciz "boot_arg_extra"

.long boot_time
.asciz "boot_time"

.long bss
.asciz "bss"

.long calloc
.asciz "calloc"

.long canonicalize_path
.asciz "canonicalize_path"

.long chmod_fs
.asciz "chmod_fs"

.long chown_fs
.asciz "chown_fs"

.long cleanup_process
.asciz "cleanup_process"

.long clear_frame
.asciz "clear_frame"

.long clone
.asciz "clone"

.long clone_directory
.asciz "clone_directory"

.long clone_fs
.asciz "clone_fs"

.long clone_table
.asciz "clone_table"

.long close_fs
.asciz "close_fs"

.long close_pipe
.asciz "close_pipe"

.long close_pty_master
.asciz "close_pty_master"

.long close_pty_slave
.asciz "close_pty_slave"

.long cmdline
.asciz "cmdline"

.long cmos_dump
.asciz "cmos_dump"

.long code
.asciz "code"

.long copy_multiboot
.asciz "copy_multiboot"

.long copy_page_physical
.asciz "copy_page_physical"

.long create_file_fs
.asciz "create_file_fs"

.long create_kernel_tasklet
.asciz "create_kernel_tasklet"

.long current_directory
.asciz "current_directory"

.long current_process
.asciz "current_process"

.long data
.asciz "data"

.long debug_file
.asciz "debug_file"

.long debug_hook
.asciz "debug_hook"

.long debug_level
.asciz "debug_level"

.long _debug_print
.asciz "_debug_print"

.long debug_print_directory
.asciz "debug_print_directory"

.long debug_print_process_tree
.asciz "debug_print_process_tree"

.long debug_print_process_tree_node
.asciz "debug_print_process_tree_node"

.long debug_print_vfs_tree
.asciz "debug_print_vfs_tree"

.long debug_print_vfs_tree_node
.asciz "debug_print_vfs_tree_node"

.long debug_video_crash
.asciz "debug_video_crash"

.long default_name
.asciz "default_name"

.long delete_process
.asciz "delete_process"

.long disable_fpu
.asciz "disable_fpu"

.long dma_frame
.asciz "dma_frame"

.long dump_multiboot
.asciz "dump_multiboot"

.long enable_fpu
.asciz "enable_fpu"

.long end
.asciz "end"

.long enter_signal_handler
.asciz "enter_signal_handler"

.long enter_user_jmp
.asciz "enter_user_jmp"

.long enter_userspace
.asciz "enter_userspace"

.long exec
.asciz "exec"

.long exec_elf
.asciz "exec_elf"

.long exec_shebang
.asciz "exec_shebang"

.long fault_handler
.asciz "fault_handler"

.long finddir_fs
.asciz "finddir_fs"

.long first_frame
.asciz "first_frame"

.long first_n_frames
.asciz "first_n_frames"

.long fix_signal_stacks
.asciz "fix_signal_stacks"

.long fmts
.asciz "fmts"

.long fork
.asciz "fork"

.long fprintf
.asciz "fprintf"

.long fpu_install
.asciz "fpu_install"

.long fpu_thread
.asciz "fpu_thread"

.long frames
.asciz "frames"

.long free
.asciz "free"

.long free_frame
.asciz "free_frame"

.long frozen_stack
.asciz "frozen_stack"

.long fs_root
.asciz "fs_root"

.long fs_tree
.asciz "fs_tree"

.long fs_types
.asciz "fs_types"

.long gdt_flush
.asciz "gdt_flush"

.long gdt_install
.asciz "gdt_install"

.long gdt_set_gate
.asciz "gdt_set_gate"

.long get_date
.asciz "get_date"

.long get_mount_point
.asciz "get_mount_point"

.long get_next_pid
.asciz "get_next_pid"

.long get_page
.asciz "get_page"

.long getpid
.asciz "getpid"

.long get_time
.asciz "get_time"

.long gettimeofday
.asciz "gettimeofday"

.long halt_and_catch_fire
.asciz "halt_and_catch_fire"

.long handle_signal
.asciz "handle_signal"

.long hashmap_create
.asciz "hashmap_create"

.long hashmap_create_int
.asciz "hashmap_create_int"

.long hashmap_free
.asciz "hashmap_free"

.long hashmap_get
.asciz "hashmap_get"

.long hashmap_has
.asciz "hashmap_has"

.long hashmap_int_comp
.asciz "hashmap_int_comp"

.long hashmap_int_dupe
.asciz "hashmap_int_dupe"

.long hashmap_int_hash
.asciz "hashmap_int_hash"

.long hashmap_keys
.asciz "hashmap_keys"

.long hashmap_remove
.asciz "hashmap_remove"

.long hashmap_set
.asciz "hashmap_set"

.long hashmap_string_comp
.asciz "hashmap_string_comp"

.long hashmap_string_dupe
.asciz "hashmap_string_dupe"

.long hashmap_string_hash
.asciz "hashmap_string_hash"

.long hashmap_values
.asciz "hashmap_values"

.long has_permission
.asciz "has_permission"

.long heap_end
.asciz "heap_end"

.long heap_install
.asciz "heap_install"

.long idt_install
.asciz "idt_install"

.long idt_load
.asciz "idt_load"

.long idt_set_gate
.asciz "idt_set_gate"

.long init_fpu
.asciz "init_fpu"

.long initial_esp
.asciz "initial_esp"

.long initialize_process_tree
.asciz "initialize_process_tree"

.long inportb
.asciz "inportb"

.long inportl
.asciz "inportl"

.long inports
.asciz "inports"

.long inportsm
.asciz "inportsm"

.long int_disable
.asciz "int_disable"

.long int_enable
.asciz "int_enable"

.long int_resume
.asciz "int_resume"

.long invalidate_page_tables
.asciz "invalidate_page_tables"

.long invalidate_tables_at
.asciz "invalidate_tables_at"

.long invalid_op
.asciz "invalid_op"

.long ioctl_fs
.asciz "ioctl_fs"

.long ioctl_pty_master
.asciz "ioctl_pty_master"

.long ioctl_pty_slave
.asciz "ioctl_pty_slave"

.long _irq0
.asciz "_irq0"

.long _irq1
.asciz "_irq1"

.long _irq10
.asciz "_irq10"

.long _irq11
.asciz "_irq11"

.long _irq12
.asciz "_irq12"

.long _irq13
.asciz "_irq13"

.long _irq14
.asciz "_irq14"

.long _irq15
.asciz "_irq15"

.long _irq2
.asciz "_irq2"

.long _irq3
.asciz "_irq3"

.long _irq4
.asciz "_irq4"

.long _irq5
.asciz "_irq5"

.long _irq6
.asciz "_irq6"

.long _irq7
.asciz "_irq7"

.long _irq8
.asciz "_irq8"

.long _irq9
.asciz "_irq9"

.long irq_ack
.asciz "irq_ack"

.long irq_handler
.asciz "irq_handler"

.long irq_install
.asciz "irq_install"

.long irq_install_handler
.asciz "irq_install_handler"

.long irq_uninstall_handler
.asciz "irq_uninstall_handler"

.long isdeadly
.asciz "isdeadly"

.long _isr0
.asciz "_isr0"

.long _isr1
.asciz "_isr1"

.long _isr10
.asciz "_isr10"

.long _isr11
.asciz "_isr11"

.long _isr12
.asciz "_isr12"

.long _isr127
.asciz "_isr127"

.long _isr13
.asciz "_isr13"

.long _isr14
.asciz "_isr14"

.long _isr15
.asciz "_isr15"

.long _isr16
.asciz "_isr16"

.long _isr17
.asciz "_isr17"

.long _isr18
.asciz "_isr18"

.long _isr19
.asciz "_isr19"

.long _isr2
.asciz "_isr2"

.long _isr20
.asciz "_isr20"

.long _isr21
.asciz "_isr21"

.long _isr22
.asciz "_isr22"

.long _isr23
.asciz "_isr23"

.long _isr24
.asciz "_isr24"

.long _isr25
.asciz "_isr25"

.long _isr26
.asciz "_isr26"

.long _isr27
.asciz "_isr27"

.long _isr28
.asciz "_isr28"

.long _isr29
.asciz "_isr29"

.long _isr3
.asciz "_isr3"

.long _isr30
.asciz "_isr30"

.long _isr31
.asciz "_isr31"

.long _isr4
.asciz "_isr4"

.long _isr5
.asciz "_isr5"

.long _isr6
.asciz "_isr6"

.long _isr7
.asciz "_isr7"

.long _isr8
.asciz "_isr8"

.long _isr9
.asciz "_isr9"

.long isrs_install
.asciz "isrs_install"

.long isrs_install_handler
.asciz "isrs_install_handler"

.long isrs_uninstall_handler
.asciz "isrs_uninstall_handler"

.long is_update_in_progress
.asciz "is_update_in_progress"

.long is_valid_process
.asciz "is_valid_process"

.long __kernel_arch
.asciz "__kernel_arch"

.long kernel_args_map
.asciz "kernel_args_map"

.long __kernel_build_date
.asciz "__kernel_build_date"

.long __kernel_build_time
.asciz "__kernel_build_time"

.long __kernel_compiler_version
.asciz "__kernel_compiler_version"

.long kernel_directory
.asciz "kernel_directory"

.long kernel_heap_alloc_point
.asciz "kernel_heap_alloc_point"

.long kernel_idle_task
.asciz "kernel_idle_task"

.long __kernel_name
.asciz "__kernel_name"

.long kernel_symbols_end
.asciz "kernel_symbols_end"

.long kernel_symbols_start
.asciz "kernel_symbols_start"

.long __kernel_version_codename
.asciz "__kernel_version_codename"

.long __kernel_version_format
.asciz "__kernel_version_format"

.long __kernel_version_lower
.asciz "__kernel_version_lower"

.long __kernel_version_major
.asciz "__kernel_version_major"

.long __kernel_version_minor
.asciz "__kernel_version_minor"

.long __kernel_version_suffix
.asciz "__kernel_version_suffix"

.long kexit
.asciz "kexit"

.long kmain
.asciz "kmain"

.long kmalloc
.asciz "kmalloc"

.long kmalloc_p
.asciz "kmalloc_p"

.long kmalloc_real
.asciz "kmalloc_real"

.long kmalloc_startat
.asciz "kmalloc_startat"

.long kopen
.asciz "kopen"

.long kopen_recur
.asciz "kopen_recur"

.long krand
.asciz "krand"

.long kvmalloc
.asciz "kvmalloc"

.long kvmalloc_p
.asciz "kvmalloc_p"

.long lfind
.asciz "lfind"

.long list_append
.asciz "list_append"

.long list_append_after
.asciz "list_append_after"

.long list_append_before
.asciz "list_append_before"

.long list_copy
.asciz "list_copy"

.long list_create
.asciz "list_create"

.long list_delete
.asciz "list_delete"

.long list_dequeue
.asciz "list_dequeue"

.long list_destroy
.asciz "list_destroy"

.long list_find
.asciz "list_find"

.long list_free
.asciz "list_free"

.long list_index_of
.asciz "list_index_of"

.long list_insert
.asciz "list_insert"

.long list_insert_after
.asciz "list_insert_after"

.long list_insert_before
.asciz "list_insert_before"

.long list_merge
.asciz "list_merge"

.long list_pop
.asciz "list_pop"

.long list_remove
.asciz "list_remove"

.long make_pipe
.asciz "make_pipe"

.long make_process_ready
.asciz "make_process_ready"

.long make_unix_pipe
.asciz "make_unix_pipe"

.long malloc
.asciz "malloc"

.long map_to_physical
.asciz "map_to_physical"

.long map_vfs_directory
.asciz "map_vfs_directory"

.long mboot_ptr
.asciz "mboot_ptr"

.long memchr
.asciz "memchr"

.long memcmp
.asciz "memcmp"

.long memcpy
.asciz "memcpy"

.long memmove
.asciz "memmove"

.long memory_total
.asciz "memory_total"

.long memory_use
.asciz "memory_use"

.long memrchr
.asciz "memrchr"

.long memset
.asciz "memset"

.long memsetw
.asciz "memsetw"

.long mkdir_fs
.asciz "mkdir_fs"

.long module_load
.asciz "module_load"

.long module_load_direct
.asciz "module_load_direct"

.long module_quickcheck
.asciz "module_quickcheck"

.long modules_get_list
.asciz "modules_get_list"

.long modules_get_symbols
.asciz "modules_get_symbols"

.long modules_install
.asciz "modules_install"

.long module_unload
.asciz "module_unload"

.long next_pid
.asciz "next_pid"

.long next_ready_process
.asciz "next_ready_process"

.long nframes
.asciz "nframes"

.long now
.asciz "now"

.long num_syscalls
.asciz "num_syscalls"

.long open_fs
.asciz "open_fs"

.long open_pipe
.asciz "open_pipe"

.long open_pty_master
.asciz "open_pty_master"

.long open_pty_slave
.asciz "open_pty_slave"

.long outportb
.asciz "outportb"

.long outportl
.asciz "outportl"

.long outports
.asciz "outports"

.long outportsm
.asciz "outportsm"

.long page_fault
.asciz "page_fault"

.long paging_finalize
.asciz "paging_finalize"

.long paging_install
.asciz "paging_install"

.long paging_mark_system
.asciz "paging_mark_system"

.long PciClassCodeTable
.asciz "PciClassCodeTable"

.long PciCommandFlags
.asciz "PciCommandFlags"

.long pci_device_lookup
.asciz "pci_device_lookup"

.long PciDevSelFlags
.asciz "PciDevSelFlags"

.long PciDevTable
.asciz "PciDevTable"

.long pci_find_type
.asciz "pci_find_type"

.long pci_read_field
.asciz "pci_read_field"

.long pci_scan
.asciz "pci_scan"

.long pci_scan_bus
.asciz "pci_scan_bus"

.long pci_scan_func
.asciz "pci_scan_func"

.long pci_scan_hit
.asciz "pci_scan_hit"

.long pci_scan_slot
.asciz "pci_scan_slot"

.long PciStatusFlags
.asciz "PciStatusFlags"

.long pci_vendor_lookup
.asciz "pci_vendor_lookup"

.long PciVenTable
.asciz "PciVenTable"

.long pci_write_field
.asciz "pci_write_field"

.long phys
.asciz "phys"

.long pipe_size
.asciz "pipe_size"

.long pipe_unsize
.asciz "pipe_unsize"

.long placement_pointer
.asciz "placement_pointer"

.long probable_function_name
.asciz "probable_function_name"

.long process_alert_node
.asciz "process_alert_node"

.long process_append_fd
.asciz "process_append_fd"

.long process_available
.asciz "process_available"

.long process_awaken_from_fswait
.asciz "process_awaken_from_fswait"

.long process_compare
.asciz "process_compare"

.long process_disown
.asciz "process_disown"

.long process_from_pid
.asciz "process_from_pid"

.long process_get_parent
.asciz "process_get_parent"

.long process_is_ready
.asciz "process_is_ready"

.long process_list
.asciz "process_list"

.long process_move_fd
.asciz "process_move_fd"

.long process_queue
.asciz "process_queue"

.long process_tree
.asciz "process_tree"

.long process_wait
.asciz "process_wait"

.long process_wait_nodes
.asciz "process_wait_nodes"

.long process_wake
.asciz "process_wake"

.long pty_available_input
.asciz "pty_available_input"

.long pty_available_output
.asciz "pty_available_output"

.long pty_create
.asciz "pty_create"

.long pty_install
.asciz "pty_install"

.long pty_ioctl
.asciz "pty_ioctl"

.long pty_list
.asciz "pty_list"

.long pty_master_create
.asciz "pty_master_create"

.long pty_new
.asciz "pty_new"

.long pty_slave_create
.asciz "pty_slave_create"

.long ramdisk
.asciz "ramdisk"

.long ramdisk_mount
.asciz "ramdisk_mount"

.long read_cmos
.asciz "read_cmos"

.long readdir_fs
.asciz "readdir_fs"

.long read_eip
.asciz "read_eip"

.long read_fs
.asciz "read_fs"

.long readlink_fs
.asciz "readlink_fs"

.long read_pipe
.asciz "read_pipe"

.long read_pty_master
.asciz "read_pty_master"

.long read_pty_slave
.asciz "read_pty_slave"

.long realloc
.asciz "realloc"

.long reap_process
.asciz "reap_process"

.long relative_time
.asciz "relative_time"

.long release_directory
.asciz "release_directory"

.long release_directory_for_exec
.asciz "release_directory_for_exec"

.long restore_fpu
.asciz "restore_fpu"

.long rets_from_sig
.asciz "rets_from_sig"

.long return_from_signal_handler
.asciz "return_from_signal_handler"

.long return_to_userspace
.asciz "return_to_userspace"

.long rfind
.asciz "rfind"

.long ring_buffer_available
.asciz "ring_buffer_available"

.long ring_buffer_create
.asciz "ring_buffer_create"

.long ring_buffer_destroy
.asciz "ring_buffer_destroy"

.long ring_buffer_interrupt
.asciz "ring_buffer_interrupt"

.long ring_buffer_read
.asciz "ring_buffer_read"

.long ring_buffer_select_wait
.asciz "ring_buffer_select_wait"

.long ring_buffer_size
.asciz "ring_buffer_size"

.long ring_buffer_unread
.asciz "ring_buffer_unread"

.long ring_buffer_write
.asciz "ring_buffer_write"

.long save_fpu
.asciz "save_fpu"

.long saves
.asciz "saves"

.long sbrk
.asciz "sbrk"

.long secs_of_month
.asciz "secs_of_month"

.long secs_of_years
.asciz "secs_of_years"

.long selectcheck_fs
.asciz "selectcheck_fs"

.long selectwait_fs
.asciz "selectwait_fs"

.long send_signal
.asciz "send_signal"

.long set_fpu_cw
.asciz "set_fpu_cw"

.long set_frame
.asciz "set_frame"

.long set_kernel_stack
.asciz "set_kernel_stack"

.long set_process_environment
.asciz "set_process_environment"

.long shm_install
.asciz "shm_install"

.long shm_obtain
.asciz "shm_obtain"

.long shm_release
.asciz "shm_release"

.long shm_release_all
.asciz "shm_release_all"

.long shm_tree
.asciz "shm_tree"

.long sleep_on
.asciz "sleep_on"

.long sleep_queue
.asciz "sleep_queue"

.long sleep_until
.asciz "sleep_until"

.long spawn_init
.asciz "spawn_init"

.long spawn_kidle
.asciz "spawn_kidle"

.long spawn_process
.asciz "spawn_process"

.long spin_init
.asciz "spin_init"

.long spin_lock
.asciz "spin_lock"

.long spin_unlock
.asciz "spin_unlock"

.long spin_wait
.asciz "spin_wait"

.long sprintf
.asciz "sprintf"

.long start
.asciz "start"

.long startswith
.asciz "startswith"

.long stpcpy
.asciz "stpcpy"

.long strchr
.asciz "strchr"

.long strchrnul
.asciz "strchrnul"

.long strcmp
.asciz "strcmp"

.long strcpy
.asciz "strcpy"

.long strcspn
.asciz "strcspn"

.long strdup
.asciz "strdup"

.long strlen
.asciz "strlen"

.long strpbrk
.asciz "strpbrk"

.long strrchr
.asciz "strrchr"

.long strspn
.asciz "strspn"

.long strstr
.asciz "strstr"

.long strtok_r
.asciz "strtok_r"

.long switch_fpu
.asciz "switch_fpu"

.long switch_next
.asciz "switch_next"

.long switch_page_directory
.asciz "switch_page_directory"

.long switch_task
.asciz "switch_task"

.long symbol_find
.asciz "symbol_find"

.long symlink_fs
.asciz "symlink_fs"

.long syscall_handler
.asciz "syscall_handler"

.long syscalls_install
.asciz "syscalls_install"

.long system
.asciz "system"

.long task_exit
.asciz "task_exit"

.long tasking_install
.asciz "tasking_install"

.long test_frame
.asciz "test_frame"

.long _timer_drift
.asciz "_timer_drift"

.long timer_drift
.asciz "timer_drift"

.long timer_handler
.asciz "timer_handler"

.long timer_install
.asciz "timer_install"

.long timer_phase
.asciz "timer_phase"

.long timer_subticks
.asciz "timer_subticks"

.long timer_ticks
.asciz "timer_ticks"

.long tokenize
.asciz "tokenize"

.long trace_pid
.asciz "trace_pid"

.long tree_break_off
.asciz "tree_break_off"

.long tree_count_children
.asciz "tree_count_children"

.long tree_create
.asciz "tree_create"

.long tree_destroy
.asciz "tree_destroy"

.long tree_find
.asciz "tree_find"

.long tree_find_parent
.asciz "tree_find_parent"

.long tree_free
.asciz "tree_free"

.long tree_node_create
.asciz "tree_node_create"

.long tree_node_destroy
.asciz "tree_node_destroy"

.long tree_node_find
.asciz "tree_node_find"

.long tree_node_find_parent
.asciz "tree_node_find_parent"

.long tree_node_free
.asciz "tree_node_free"

.long tree_node_insert_child
.asciz "tree_node_insert_child"

.long tree_node_insert_child_node
.asciz "tree_node_insert_child_node"

.long tree_node_parent_remove
.asciz "tree_node_parent_remove"

.long tree_node_remove
.asciz "tree_node_remove"

.long tree_remove
.asciz "tree_remove"

.long tree_remove_reparent_root
.asciz "tree_remove_reparent_root"

.long tree_set_root
.asciz "tree_set_root"

.long tss_flush
.asciz "tss_flush"

.long ubsan_debug
.asciz "ubsan_debug"

.long __ubsan_handle_add_overflow
.asciz "__ubsan_handle_add_overflow"

.long __ubsan_handle_builtin_unreachable
.asciz "__ubsan_handle_builtin_unreachable"

.long __ubsan_handle_divrem_overflow
.asciz "__ubsan_handle_divrem_overflow"

.long __ubsan_handle_mul_overflow
.asciz "__ubsan_handle_mul_overflow"

.long __ubsan_handle_negate_overflow
.asciz "__ubsan_handle_negate_overflow"

.long __ubsan_handle_out_of_bounds
.asciz "__ubsan_handle_out_of_bounds"

.long __ubsan_handle_shift_out_of_bounds
.asciz "__ubsan_handle_shift_out_of_bounds"

.long __ubsan_handle_sub_overflow
.asciz "__ubsan_handle_sub_overflow"

.long __ubsan_handle_type_mismatch
.asciz "__ubsan_handle_type_mismatch"

.long __ubsan_handle_vla_bound_not_positive
.asciz "__ubsan_handle_vla_bound_not_positive"

.long unlink_fs
.asciz "unlink_fs"

.long unswitch_fpu
.asciz "unswitch_fpu"

.long validate
.asciz "validate"

.long valloc
.asciz "valloc"

.long vasprintf
.asciz "vasprintf"

.long vfs_install
.asciz "vfs_install"

.long vfs_lock
.asciz "vfs_lock"

.long vfs_mount
.asciz "vfs_mount"

.long vfs_mount_type
.asciz "vfs_mount_type"

.long vfs_register
.asciz "vfs_register"

.long waitpid
.asciz "waitpid"

.long wakeup_queue
.asciz "wakeup_queue"

.long wakeup_queue_interrupted
.asciz "wakeup_queue_interrupted"

.long wakeup_sleepers
.asciz "wakeup_sleepers"

.long write_fs
.asciz "write_fs"

.long write_pipe
.asciz "write_pipe"

.long write_pty_master
.asciz "write_pty_master"

.long write_pty_slave
.asciz "write_pty_slave"

.global kernel_symbols_end
kernel_symbols_end:
