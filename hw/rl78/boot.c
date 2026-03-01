#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/datadir.h"
#include "hw/core/loader.h"
#include "elf.h"
#include "target/rl78/cpu.h"
#include "boot.h"

bool rl78_load_firmware(RL78CPU *cpu, MachineState *ms, MemoryRegion *flash,
                        const char *firmware)
{
    g_autofree char *filename = NULL;
    int bytes_loaded;

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, firmware);
    if (filename == NULL) {
        error_report("Unable to find %s", firmware);
        return false;
    }

    bytes_loaded = load_elf_as(filename, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, ELFDATA2LSB, EM_RL78, 0, 0, NULL);

    if (bytes_loaded < 0) {
        error_report("Unable to load firmware image %s as ELF", firmware);
        return false;
    }

    return true;
}
