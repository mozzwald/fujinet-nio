/* Additional linker assertions: firmware LOAD image, not just SRAM text. */
ASSERT(LENGTH(FLASH) == 0x1000000, "ROM storage requires 16MiB flash")
ASSERT(__flash_binary_end <= 0x10100000, "Firmware exceeds reserved 1MiB region")
__rom_slot0 = 0x10fc0000;
__rom_slot1 = 0x10fd1000;
__rom_metadata0 = 0x10fe2000;
__rom_metadata1 = 0x10fe3000;
__rom_storage_end = 0x10fe4000;
ASSERT(__rom_slot0 >= __flash_binary_end, "ROM storage overlaps firmware")
ASSERT(__rom_slot1 == __rom_slot0 + 0x11000, "ROM slot overlap")
ASSERT(__rom_metadata0 == __rom_slot1 + 0x11000, "ROM metadata overlap")
ASSERT(__rom_storage_end <= ORIGIN(FLASH) + LENGTH(FLASH), "ROM storage outside flash")
