batchisp -device at32uc3a1512 -hardware usb -operation  erase f memory flash blankcheck loadbuffer ..\up4dar-2nd-bootloader\Debug\up4dar-2nd-bootloader.elf addrange 0x02000 0x03fff program verify loadbuffer Debug\up4dar-os.elf addrange 0x04000 0x3ffff program verify start reset 0