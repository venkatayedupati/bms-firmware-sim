/*
 * Linker script for QEMU's mps2-an385 machine (Cortex-M3). Memory map and
 * section layout follow FreeRTOS's own official demo for this exact
 * machine (Demo/CORTEX_MPS2_QEMU_IAR_GCC/build/gcc/mps2_m3.ld in the
 * FreeRTOS/FreeRTOS repo) rather than a guessed layout.
 */

MEMORY
{
    FLASH (xr) : ORIGIN = 0x00000000, LENGTH = 4M
    RAM   (rw) : ORIGIN = 0x20000000, LENGTH = 4M
}

ENTRY(Reset_Handler)

_Min_Stack_Size = 0x1000; /* used by main() before the scheduler starts, then reused as the interrupt stack */

SECTIONS
{
    .isr_vector :
    {
        __vector_table = .;
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        *(.text*)
        *(.rodata*)
        _etext = .;
    } > FLASH

    .data :
    {
        . = ALIGN(8);
        _sdata = .;
        *(.data*)
        _edata = .;
    } > RAM AT > FLASH
    _sidata = LOADADDR(.data);

    /* configTOTAL_HEAP_SIZE's ucHeap[] array (portable/MemMang/heap_4.c)
       lives here too, as an ordinary static array -- FreeRTOS's heap isn't
       a separate linker-defined region when using heap_4.c, unlike some
       heap_5.c multi-region setups. */
    .bss :
    {
        . = ALIGN(8);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        _ebss = .;
    } > RAM

    /* newlib-nano's _sbrk (libnosys.a) references `end` even though this
       project never calls a libc allocator -- FreeRTOS's own heap_4.c
       manages configTOTAL_HEAP_SIZE itself, not through sbrk. _sbrk is
       still linked in (transitively referenced by nano.specs' C runtime
       init) even though nothing ever calls it, so `end` must resolve or
       the link fails; it isn't used at runtime. */
    PROVIDE(end = _ebss);
    PROVIDE(_end = _ebss);

    __StackTop = ORIGIN(RAM) + LENGTH(RAM);
    __StackLimit = __StackTop - _Min_Stack_Size;
    PROVIDE(_estack = __StackTop);

    ASSERT(__StackLimit >= _ebss, "region RAM overflowed: data + bss leave no room for the stack")
}
