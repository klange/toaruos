#ifndef KERNEL_MOD_RTL_H
#define KERNEL_MOD_RTL_H

extern uint8_t* rtl_get_mac();
extern rtl_send_packet(uint8_t* payload, size_t payload_size);
extern void* rtl_dequeue();
extern void rtl_enqueue(void* buffer);

#endif
