/* One flat .text: hook.S first (it pads itself to 0x100), then the C code. */
SECTIONS
{
    . = 0;
    .text : {
        *hook.o(.text)
        *stub.o(.text .text.* .rdata .rdata$* .data .bss)
    }
    /DISCARD/ : { *(.pdata) *(.xdata) *(.comment) *(.note*) }
}
