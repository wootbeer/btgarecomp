SECTIONS
{
    HIDDEN(__romPos = 0);
    header_ROM_START = __romPos;
    header_VRAM = ADDR(.header);
    .header : AT(header_ROM_START) SUBALIGN(16)
    {
        FILL(0x00000000);
        header_DATA_START = .;
        build/asm/header.s.o(.data);
        header_DATA_END = .;
        header_DATA_SIZE = ABSOLUTE(header_DATA_END - header_DATA_START);
    }
    __romPos += SIZEOF(.header);
    header_ROM_END = __romPos;
    header_VRAM_END = .;

    ipl3_ROM_START = __romPos;
    ipl3_VRAM = ADDR(.ipl3);
    .ipl3 : AT(ipl3_ROM_START) SUBALIGN(16)
    {
        FILL(0x00000000);
        ipl3_DATA_START = .;
        build/assets/ipl3.bin.o(.data);
        ipl3_DATA_END = .;
        ipl3_DATA_SIZE = ABSOLUTE(ipl3_DATA_END - ipl3_DATA_START);
    }
    __romPos += SIZEOF(.ipl3);
    ipl3_ROM_END = __romPos;
    ipl3_VRAM_END = .;

    boot_ROM_START = __romPos;
    boot_VRAM = ADDR(.boot);
    .boot 0x80000400 : AT(boot_ROM_START) SUBALIGN(16)
    {
        FILL(0x00000000);
        boot_TEXT_START = .;
        build/src/boot_entry.c.o(.text);
        . = ALIGN(., 16);
        boot_TEXT_END = .;
        boot_TEXT_SIZE = ABSOLUTE(boot_TEXT_END - boot_TEXT_START);
        boot_DATA_START = .;
        build/src/boot_entry.c.o(.data);
        build/asm/data/boot_data.data.s.o(.data);
        . = ALIGN(., 16);
        boot_DATA_END = .;
        boot_DATA_SIZE = ABSOLUTE(boot_DATA_END - boot_DATA_START);
        boot_RODATA_START = .;
        build/src/boot_entry.c.o(.rodata);
        . = ALIGN(., 16);
        boot_RODATA_END = .;
        boot_RODATA_SIZE = ABSOLUTE(boot_RODATA_END - boot_RODATA_START);
    }
    boot_bss_VRAM = ADDR(.boot_bss);
    .boot_bss (NOLOAD) : SUBALIGN(16)
    {
        FILL(0x00000000);
        boot_BSS_START = .;
        build/src/boot_entry.c.o(.bss);
        . = ALIGN(., 16);
        boot_BSS_END = .;
        boot_BSS_SIZE = ABSOLUTE(boot_BSS_END - boot_BSS_START);
    }
    __romPos += SIZEOF(.boot);
    __romPos = ALIGN(__romPos, 16);
    . = ALIGN(., 16);
    boot_ROM_END = __romPos;
    boot_VRAM_END = .;

    unk_before_probe_ROM_START = __romPos;
    unk_before_probe_VRAM = ADDR(.unk_before_probe);
    .unk_before_probe 0x80002400 : AT(unk_before_probe_ROM_START) SUBALIGN(16)
    {
        FILL(0x00000000);
        unk_before_probe_DATA_START = .;
        build/assets/unk_before_probe.bin.o(.data);
        unk_before_probe_DATA_END = .;
        unk_before_probe_DATA_SIZE = ABSOLUTE(unk_before_probe_DATA_END - unk_before_probe_DATA_START);
    }
    __romPos += SIZEOF(.unk_before_probe);
    unk_before_probe_ROM_END = __romPos;
    unk_before_probe_VRAM_END = .;

    main_probe_ROM_START = __romPos;
    main_probe_VRAM = ADDR(.main_probe);
    .main_probe 0x8009ED9C : AT(main_probe_ROM_START) SUBALIGN(16)
    {
        FILL(0x00000000);
        main_probe_TEXT_START = .;
        build/src/main_entry.c.o(.text);
        . = ALIGN(., 16);
        main_probe_TEXT_END = .;
        main_probe_TEXT_SIZE = ABSOLUTE(main_probe_TEXT_END - main_probe_TEXT_START);
        main_probe_DATA_START = .;
        build/src/main_entry.c.o(.data);
        . = ALIGN(., 16);
        main_probe_DATA_END = .;
        main_probe_DATA_SIZE = ABSOLUTE(main_probe_DATA_END - main_probe_DATA_START);
        main_probe_RODATA_START = .;
        build/src/main_entry.c.o(.rodata);
        . = ALIGN(., 16);
        main_probe_RODATA_END = .;
        main_probe_RODATA_SIZE = ABSOLUTE(main_probe_RODATA_END - main_probe_RODATA_START);
    }
    main_probe_bss_VRAM = ADDR(.main_probe_bss);
    .main_probe_bss (NOLOAD) : SUBALIGN(16)
    {
        FILL(0x00000000);
        main_probe_BSS_START = .;
        build/src/main_entry.c.o(.bss);
        . = ALIGN(., 16);
        main_probe_BSS_END = .;
        main_probe_BSS_SIZE = ABSOLUTE(main_probe_BSS_END - main_probe_BSS_START);
    }
    __romPos += SIZEOF(.main_probe);
    __romPos = ALIGN(__romPos, 16);
    . = ALIGN(., 16);
    main_probe_ROM_END = __romPos;
    main_probe_VRAM_END = .;

    unk_after_probe_ROM_START = __romPos;
    unk_after_probe_VRAM = ADDR(.unk_after_probe);
    .unk_after_probe 0x800A389C : AT(unk_after_probe_ROM_START) SUBALIGN(16)
    {
        FILL(0x00000000);
        unk_after_probe_DATA_START = .;
        build/assets/unk_after_probe.bin.o(.data);
        unk_after_probe_DATA_END = .;
        unk_after_probe_DATA_SIZE = ABSOLUTE(unk_after_probe_DATA_END - unk_after_probe_DATA_START);
    }
    __romPos += SIZEOF(.unk_after_probe);
    unk_after_probe_ROM_END = __romPos;
    unk_after_probe_VRAM_END = .;

    probe_105cc_ROM_START = __romPos;
    probe_105cc_VRAM = ADDR(.probe_105cc);
    .probe_105cc 0x801055CC : AT(probe_105cc_ROM_START) SUBALIGN(16)
    {
        FILL(0x00000000);
        probe_105cc_TEXT_START = .;
        build/src/probe_105cc.c.o(.text);
        . = ALIGN(., 16);
        probe_105cc_TEXT_END = .;
        probe_105cc_TEXT_SIZE = ABSOLUTE(probe_105cc_TEXT_END - probe_105cc_TEXT_START);
        probe_105cc_DATA_START = .;
        build/src/probe_105cc.c.o(.data);
        . = ALIGN(., 16);
        probe_105cc_DATA_END = .;
        probe_105cc_DATA_SIZE = ABSOLUTE(probe_105cc_DATA_END - probe_105cc_DATA_START);
        probe_105cc_RODATA_START = .;
        build/src/probe_105cc.c.o(.rodata);
        . = ALIGN(., 16);
        probe_105cc_RODATA_END = .;
        probe_105cc_RODATA_SIZE = ABSOLUTE(probe_105cc_RODATA_END - probe_105cc_RODATA_START);
    }
    probe_105cc_bss_VRAM = ADDR(.probe_105cc_bss);
    .probe_105cc_bss (NOLOAD) : SUBALIGN(16)
    {
        FILL(0x00000000);
        probe_105cc_BSS_START = .;
        build/src/probe_105cc.c.o(.bss);
        . = ALIGN(., 16);
        probe_105cc_BSS_END = .;
        probe_105cc_BSS_SIZE = ABSOLUTE(probe_105cc_BSS_END - probe_105cc_BSS_START);
    }
    __romPos += SIZEOF(.probe_105cc);
    __romPos = ALIGN(__romPos, 16);
    . = ALIGN(., 16);
    probe_105cc_ROM_END = __romPos;
    probe_105cc_VRAM_END = .;

    unk_gap1_ROM_START = __romPos;
    unk_gap1_VRAM = ADDR(.unk_gap1);
    .unk_gap1 0x801057CC : AT(unk_gap1_ROM_START) SUBALIGN(16)
    {
        FILL(0x00000000);
        unk_gap1_DATA_START = .;
        build/assets/unk_gap1.bin.o(.data);
        unk_gap1_DATA_END = .;
        unk_gap1_DATA_SIZE = ABSOLUTE(unk_gap1_DATA_END - unk_gap1_DATA_START);
    }
    __romPos += SIZEOF(.unk_gap1);
    unk_gap1_ROM_END = __romPos;
    unk_gap1_VRAM_END = .;

    probe_105db_ROM_START = __romPos;
    probe_105db_VRAM = ADDR(.probe_105db);
    .probe_105db 0x80105DB0 : AT(probe_105db_ROM_START) SUBALIGN(16)
    {
        FILL(0x00000000);
        probe_105db_TEXT_START = .;
        build/src/probe_105db.c.o(.text);
        . = ALIGN(., 16);
        probe_105db_TEXT_END = .;
        probe_105db_TEXT_SIZE = ABSOLUTE(probe_105db_TEXT_END - probe_105db_TEXT_START);
        probe_105db_DATA_START = .;
        build/src/probe_105db.c.o(.data);
        . = ALIGN(., 16);
        probe_105db_DATA_END = .;
        probe_105db_DATA_SIZE = ABSOLUTE(probe_105db_DATA_END - probe_105db_DATA_START);
        probe_105db_RODATA_START = .;
        build/src/probe_105db.c.o(.rodata);
        . = ALIGN(., 16);
        probe_105db_RODATA_END = .;
        probe_105db_RODATA_SIZE = ABSOLUTE(probe_105db_RODATA_END - probe_105db_RODATA_START);
    }
    probe_105db_bss_VRAM = ADDR(.probe_105db_bss);
    .probe_105db_bss (NOLOAD) : SUBALIGN(16)
    {
        FILL(0x00000000);
        probe_105db_BSS_START = .;
        build/src/probe_105db.c.o(.bss);
        . = ALIGN(., 16);
        probe_105db_BSS_END = .;
        probe_105db_BSS_SIZE = ABSOLUTE(probe_105db_BSS_END - probe_105db_BSS_START);
    }
    __romPos += SIZEOF(.probe_105db);
    __romPos = ALIGN(__romPos, 16);
    . = ALIGN(., 16);
    probe_105db_ROM_END = __romPos;
    probe_105db_VRAM_END = .;

    unk_after_leads_ROM_START = __romPos;
    unk_after_leads_VRAM = ADDR(.unk_after_leads);
    .unk_after_leads 0x80105FD0 : AT(unk_after_leads_ROM_START) SUBALIGN(16)
    {
        FILL(0x00000000);
        unk_after_leads_DATA_START = .;
        build/assets/unk_after_leads.bin.o(.data);
        unk_after_leads_DATA_END = .;
        unk_after_leads_DATA_SIZE = ABSOLUTE(unk_after_leads_DATA_END - unk_after_leads_DATA_START);
    }
    __romPos += SIZEOF(.unk_after_leads);
    unk_after_leads_ROM_END = __romPos;
    unk_after_leads_VRAM_END = .;

    /DISCARD/ :
    {
        *(*);
    }
}
