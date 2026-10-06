SECTIONS
{
    /* vita-elf-create appends SCE metadata to the executable segment. */
    . = ALIGN(0x1000) + 0x1000;
}
INSERT BEFORE .init_array;
