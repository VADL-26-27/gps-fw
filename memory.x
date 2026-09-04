/* memory.x — linker script for STM32F446RE */
MEMORY
{
    FLASH : ORIGIN = 0x08000000, LENGTH = 512K
    /* values found from pg. 65 of rm0390 */
    RAM   : ORIGIN = 0x20000000, LENGTH = 128K
    /* values found from pg. 60 of rm0390 */
}
