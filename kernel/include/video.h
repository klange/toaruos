#pragma once

#define IO_VID_WIDTH  0x5001
#define IO_VID_HEIGHT 0x5002
#define IO_VID_DEPTH  0x5003
#define IO_VID_ADDR   0x5004
#define IO_VID_SIGNAL 0x5005

#ifdef _KERNEL_
extern void lfb_set_resolution(uint16_t x, uint16_t y);
extern uint16_t lfb_resolution_x;
extern uint16_t lfb_resolution_y;
extern uint16_t lfb_resolution_b;
extern uint8_t * lfb_vid_memory;
#endif

