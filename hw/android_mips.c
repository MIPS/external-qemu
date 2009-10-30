/* Copyright (C) 2007-2008 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/
#include "hw.h"
#include "boards.h"
#include "devices.h"
#include "net.h"
#include "sysemu.h"
#include "mips.h"
#include "goldfish_device.h"
#include "android/globals.h"
#include "audio/audio.h"

#define MIPS_CPU_SAVE_VERSION  1

char* audio_input_source = NULL;

void goldfish_memlog_init(uint32_t base);

static struct goldfish_device event0_device = {
    .name = "goldfish_events",
    .id = 0,
    .size = 0x1000,
    .irq_count = 1
};

static struct goldfish_device nand_device = {
    .name = "goldfish_nand",
    .id = 0,
    .size = 0x1000
};

static struct goldfish_device trace_device = {
    .name = "qemu_trace",
    .id = -1,
    .size = 0x1000
};

/* Board init.  */

#define TEST_SWITCH 1
#if TEST_SWITCH
uint32_t switch_test_write(void *opaque, uint32_t state)
{
    goldfish_switch_set_state(opaque, state);
    return state;
}
#endif

static void android_mips_init(ram_addr_t ram_size, int vga_ram_size,
    const char *boot_device, DisplayState *ds, 
    const char *kernel_filename, 
    const char *kernel_cmdline,
    const char *initrd_filename,
    const char *cpu_model)
{
    CPUState *env;
    qemu_irq *goldfish_pic;
    int i;

    if (!cpu_model)
        cpu_model = "mips32";

    env = cpu_init(cpu_model);

    register_savevm( "cpu", 0, MIPS_CPU_SAVE_VERSION, cpu_save, cpu_load, env );

    cpu_register_physical_memory(0, ram_size, IO_MEM_RAM);

    /* Init internal devices */
    cpu_mips_irq_init_cpu(env);
    cpu_mips_clock_init(env);
    cpu_mips_irqctrl_init();

#define GOLDFISH_INTERRUPT	0x1f000000
#define GOLDFISH_DEVICEBUS	0x1f001000
#define GOLDFISH_TTY		0x1f002000
#define GOLDFISH_RTC		0x1f003000
#define GOLDFISH_AUDIO		0x1f004000
#define GOLDFISH_MMC		0x1f005000
#define GOLDFISH_MEMLOG		0x1f006000
#define GOLDFISH_DEVICES	0x1f010000

    goldfish_pic = goldfish_interrupt_init(GOLDFISH_INTERRUPT,
					   env->irq[2], env->irq[3]);
    goldfish_device_init(goldfish_pic, GOLDFISH_DEVICES, 0x7f0000, 10, 22);

    goldfish_device_bus_init(GOLDFISH_DEVICEBUS, 1);

    goldfish_timer_and_rtc_init(GOLDFISH_RTC, 3);

    goldfish_tty_add(serial_hds[0], 0, GOLDFISH_TTY, 4);
    for(i = 1; i < MAX_SERIAL_PORTS; i++) {
        //printf("android_mips_init serial %d %x\n", i, serial_hds[i]);
        if(serial_hds[i]) {
            goldfish_tty_add(serial_hds[i], i, 0, 0);
        }
    }

    for(i = 0; i < MAX_NICS; i++) {
        if (nd_table[i].vlan) {
            if (nd_table[i].model == NULL
                || strcmp(nd_table[i].model, "smc91c111") == 0) {
                struct goldfish_device *smc_device;
                smc_device = qemu_mallocz(sizeof(*smc_device));
                smc_device->name = "smc91x";
                smc_device->id = i;
                smc_device->size = 0x1000;
                smc_device->irq_count = 1;
                goldfish_add_device_no_io(smc_device);
                smc91c111_init(&nd_table[i], smc_device->base, goldfish_pic[smc_device->irq]);
            } else {
                fprintf(stderr, "qemu: Unsupported NIC: %s\n", nd_table[0].model);
                exit (1);
            }
        }
    }

    goldfish_fb_init(ds, 0);
#ifdef HAS_AUDIO
    AUD_init();
    goldfish_audio_init(GOLDFISH_AUDIO, 0, audio_input_source);
#endif
    {
        int  idx = drive_get_index( IF_IDE, 0, 0 );
        if (idx >= 0)
            goldfish_mmc_init(GOLDFISH_MMC, 0, drives_table[idx].bdrv);
    }

    goldfish_memlog_init(GOLDFISH_MEMLOG);

    if (android_hw->hw_battery)
        goldfish_battery_init();

    goldfish_add_device_no_io(&event0_device);
    events_dev_init(event0_device.base, goldfish_pic[event0_device.irq]);

#ifdef CONFIG_NAND
    goldfish_add_device_no_io(&nand_device);
    nand_dev_init(nand_device.base);
#endif
#ifdef CONFIG_TRACE
    extern const char *trace_filename;
    if(trace_filename != NULL) {
        goldfish_add_device_no_io(&trace_device);
        trace_dev_init(trace_device.base);
    }
#endif

#if TEST_SWITCH
    {
        void *sw;
        sw = goldfish_switch_add("test", NULL, NULL, 0);
        goldfish_switch_set_state(sw, 1);
        goldfish_switch_add("test2", switch_test_write, sw, 1);
    }
#endif

#if 0
    memset(&info, 0, sizeof info);
    info.ram_size        = ram_size;
    info.kernel_filename = kernel_filename;
    info.kernel_cmdline  = kernel_cmdline;
    info.initrd_filename = initrd_filename;
    info.nb_cpus         = 1;
    info.board_id        = 3232;

    mips_load_kernel(env, &info);
#endif
}

QEMUMachine android_mips_machine = {
    "android_mips",
    "MIPS Android Emulator",
    android_mips_init,
    0,
    0,
    NULL
};
