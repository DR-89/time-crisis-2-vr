/* Only the emulated chip boundary is stubbed in host/UI fixtures. */
#include <stdint.h>
uint8_t tc2_ram[0x1000000];
uint32_t rr_frame;
uint8_t *tc2_vram_ptr(uint32_t address){(void)address;static uint8_t ram[0x4000];return ram;}
uint64_t tc2_cycles_now(void){return 0;}
void tc2_irq_line(uint32_t bit,int on){(void)bit;(void)on;}
