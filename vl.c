/*
 * QEMU based User Mode Linux 
 * 
 * This file is part of proprietary software - it is published here
 * only for demonstration and information purposes.
 * 
 * Copyright (c) 2003 Fabrice Bellard 
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <malloc.h>
#include <termios.h>
#include <sys/poll.h>
#include <errno.h>

#include "cpu-i386.h"
#include "disas.h"

#define DEBUG_LOGFILE "/tmp/vl.log"
//#define DEBUG_UNUSED_IOPORT

#define PHYS_RAM_BASE 0xa8000000
#define KERNEL_LOAD_ADDR   0x00100000
#define INITRD_LOAD_ADDR   0x00400000
#define KERNEL_PARAMS_ADDR 0x00090000

/* from plex86 (BSD license) */
struct  __attribute__ ((packed)) linux_params {
  // For 0x00..0x3f, see 'struct screen_info' in linux/include/linux/tty.h.
  // I just padded out the VESA parts, rather than define them.

  /* 0x000 */ uint8_t   orig_x;
  /* 0x001 */ uint8_t   orig_y;
  /* 0x002 */ uint16_t  ext_mem_k;
  /* 0x004 */ uint16_t  orig_video_page;
  /* 0x006 */ uint8_t   orig_video_mode;
  /* 0x007 */ uint8_t   orig_video_cols;
  /* 0x008 */ uint16_t  unused1;
  /* 0x00a */ uint16_t  orig_video_ega_bx;
  /* 0x00c */ uint16_t  unused2;
  /* 0x00e */ uint8_t   orig_video_lines;
  /* 0x00f */ uint8_t   orig_video_isVGA;
  /* 0x010 */ uint16_t  orig_video_points;
  /* 0x012 */ uint8_t   pad0[0x20 - 0x12]; // VESA info.
  /* 0x020 */ uint16_t  cl_magic;  // Commandline magic number (0xA33F)
  /* 0x022 */ uint16_t  cl_offset; // Commandline offset.  Address of commandline
                                 // is calculated as 0x90000 + cl_offset, bu
                                 // only if cl_magic == 0xA33F.
  /* 0x024 */ uint8_t   pad1[0x40 - 0x24]; // VESA info.

  /* 0x040 */ uint8_t   apm_bios_info[20]; // struct apm_bios_info
  /* 0x054 */ uint8_t   pad2[0x80 - 0x54];

  // Following 2 from 'struct drive_info_struct' in drivers/block/cciss.h.
  // Might be truncated?
  /* 0x080 */ uint8_t   hd0_info[16]; // hd0-disk-parameter from intvector 0x41
  /* 0x090 */ uint8_t   hd1_info[16]; // hd1-disk-parameter from intvector 0x46

  // System description table truncated to 16 bytes
  // From 'struct sys_desc_table_struct' in linux/arch/i386/kernel/setup.c.
  /* 0x0a0 */ uint16_t  sys_description_len;
  /* 0x0a2 */ uint8_t   sys_description_table[14];
                        // [0] machine id
                        // [1] machine submodel id
                        // [2] BIOS revision
                        // [3] bit1: MCA bus

  /* 0x0b0 */ uint8_t   pad3[0x1e0 - 0xb0];
  /* 0x1e0 */ uint32_t  alt_mem_k;
  /* 0x1e4 */ uint8_t   pad4[4];
  /* 0x1e8 */ uint8_t   e820map_entries;
  /* 0x1e9 */ uint8_t   eddbuf_entries; // EDD_NR
  /* 0x1ea */ uint8_t   pad5[0x1f1 - 0x1ea];
  /* 0x1f1 */ uint8_t   setup_sects; // size of setup.S, number of sectors
  /* 0x1f2 */ uint16_t  mount_root_rdonly; // MOUNT_ROOT_RDONLY (if !=0)
  /* 0x1f4 */ uint16_t  sys_size; // size of compressed kernel-part in the
                                // (b)zImage-file (in 16 byte units, rounded up)
  /* 0x1f6 */ uint16_t  swap_dev; // (unused AFAIK)
  /* 0x1f8 */ uint16_t  ramdisk_flags;
  /* 0x1fa */ uint16_t  vga_mode; // (old one)
  /* 0x1fc */ uint16_t  orig_root_dev; // (high=Major, low=minor)
  /* 0x1fe */ uint8_t   pad6[1];
  /* 0x1ff */ uint8_t   aux_device_info;
  /* 0x200 */ uint16_t  jump_setup; // Jump to start of setup code,
                                  // aka "reserved" field.
  /* 0x202 */ uint8_t   setup_signature[4]; // Signature for SETUP-header, ="HdrS"
  /* 0x206 */ uint16_t  header_format_version; // Version number of header format;
  /* 0x208 */ uint8_t   setup_S_temp0[8]; // Used by setup.S for communication with
                                        // boot loaders, look there.
  /* 0x210 */ uint8_t   loader_type;
                        // 0 for old one.
                        // else 0xTV:
                        //   T=0: LILO
                        //   T=1: Loadlin
                        //   T=2: bootsect-loader
                        //   T=3: SYSLINUX
                        //   T=4: ETHERBOOT
                        //   V=version
  /* 0x211 */ uint8_t   loadflags;
                        // bit0 = 1: kernel is loaded high (bzImage)
                        // bit7 = 1: Heap and pointer (see below) set by boot
                        //   loader.
  /* 0x212 */ uint16_t  setup_S_temp1;
  /* 0x214 */ uint32_t  kernel_start;
  /* 0x218 */ uint32_t  initrd_start;
  /* 0x21c */ uint32_t  initrd_size;
  /* 0x220 */ uint8_t   setup_S_temp2[4];
  /* 0x224 */ uint16_t  setup_S_heap_end_pointer;
  /* 0x226 */ uint8_t   pad7[0x2d0 - 0x226];

  /* 0x2d0 : Int 15, ax=e820 memory map. */
  // (linux/include/asm-i386/e820.h, 'struct e820entry')
#define E820MAX  32
#define E820_RAM  1
#define E820_RESERVED 2
#define E820_ACPI 3 /* usable as RAM once ACPI tables have been read */
#define E820_NVS  4
  struct {
    uint64_t addr;
    uint64_t size;
    uint32_t type;
    } e820map[E820MAX];

  /* 0x550 */ uint8_t   pad8[0x600 - 0x550];

  // BIOS Enhanced Disk Drive Services.
  // (From linux/include/asm-i386/edd.h, 'struct edd_info')
  // Each 'struct edd_info is 78 bytes, times a max of 6 structs in array.
  /* 0x600 */ uint8_t   eddbuf[0x7d4 - 0x600];

  /* 0x7d4 */ uint8_t   pad9[0x800 - 0x7d4];
  /* 0x800 */ uint8_t   commandline[0x800];

  /* 0x1000 */
  uint64_t gdt_table[256];
  uint64_t idt_table[48];
};

#define KERNEL_CS     0x10
#define KERNEL_DS     0x18

typedef void (IOPortWriteFunc)(CPUX86State *env, uint32_t address, uint32_t data);
typedef uint32_t (IOPortReadFunc)(CPUX86State *env, uint32_t address);

#define MAX_IOPORTS 1024

char phys_ram_file[1024];
CPUX86State *global_env;
FILE *logfile = NULL;
int loglevel;
IOPortReadFunc *ioport_readb_table[MAX_IOPORTS];
IOPortWriteFunc *ioport_writeb_table[MAX_IOPORTS];
IOPortReadFunc *ioport_readw_table[MAX_IOPORTS];
IOPortWriteFunc *ioport_writew_table[MAX_IOPORTS];

/***********************************************************/
/* x86 io ports */

uint32_t default_ioport_readb(CPUX86State *env, uint32_t address)
{
#ifdef DEBUG_UNUSED_IOPORT
    fprintf(stderr, "inb: port=0x%04x\n", address);
#endif
    return 0;
}

void default_ioport_writeb(CPUX86State *env, uint32_t address, uint32_t data)
{
#ifdef DEBUG_UNUSED_IOPORT
    fprintf(stderr, "outb: port=0x%04x data=0x%02x\n", address, data);
#endif
}

/* default is to make two byte accesses */
uint32_t default_ioport_readw(CPUX86State *env, uint32_t address)
{
    uint32_t data;
    data = ioport_readb_table[address](env, address);
    data |= ioport_readb_table[address + 1](env, address + 1) << 8;
    return data;
}

void default_ioport_writew(CPUX86State *env, uint32_t address, uint32_t data)
{
    ioport_writeb_table[address](env, address, data & 0xff);
    ioport_writeb_table[address + 1](env, address + 1, (data >> 8) & 0xff);
}

void init_ioports(void)
{
    int i;

    for(i = 0; i < MAX_IOPORTS; i++) {
        ioport_readb_table[i] = default_ioport_readb;
        ioport_writeb_table[i] = default_ioport_writeb;
        ioport_readw_table[i] = default_ioport_readw;
        ioport_writew_table[i] = default_ioport_writew;
    }
}

int register_ioport_readb(int start, int length, IOPortReadFunc *func)
{
    int i;

    for(i = start; i < start + length; i++)
        ioport_readb_table[i] = func;
    return 0;
}

int register_ioport_writeb(int start, int length, IOPortWriteFunc *func)
{
    int i;

    for(i = start; i < start + length; i++)
        ioport_writeb_table[i] = func;
    return 0;
}

void pstrcpy(char *buf, int buf_size, const char *str)
{
    int c;
    char *q = buf;

    if (buf_size <= 0)
        return;

    for(;;) {
        c = *str++;
        if (c == 0 || q >= buf + buf_size - 1)
            break;
        *q++ = c;
    }
    *q = '\0';
}

/* strcat and truncate. */
char *pstrcat(char *buf, int buf_size, const char *s)
{
    int len;
    len = strlen(buf);
    if (len < buf_size) 
        pstrcpy(buf + len, buf_size - len, s);
    return buf;
}

int load_kernel(const char *filename, uint8_t *addr)
{
    int fd, size, setup_sects;
    uint8_t bootsect[512];

    fd = open(filename, O_RDONLY);
    if (fd < 0)
        return -1;
    if (read(fd, bootsect, 512) != 512)
        goto fail;
    setup_sects = bootsect[0x1F1];
    if (!setup_sects)
        setup_sects = 4;
    /* skip 16 bit setup code */
    lseek(fd, (setup_sects + 1) * 512, SEEK_SET);
    size = read(fd, addr, 16 * 1024 * 1024);
    if (size < 0)
        goto fail;
    close(fd);
    return size;
 fail:
    close(fd);
    return -1;
}

/* return the size or -1 if error */
int load_image(const char *filename, uint8_t *addr)
{
    int fd, size;
    fd = open(filename, O_RDONLY);
    if (fd < 0)
        return -1;
    size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (read(fd, addr, size) != size) {
        close(fd);
        return -1;
    }
    close(fd);
    return size;
}

void cpu_x86_outb(CPUX86State *env, int addr, int val)
{
    ioport_writeb_table[addr & (MAX_IOPORTS - 1)](env, addr, val);
}

void cpu_x86_outw(CPUX86State *env, int addr, int val)
{
    ioport_writew_table[addr & (MAX_IOPORTS - 1)](env, addr, val);
}

void cpu_x86_outl(CPUX86State *env, int addr, int val)
{
    fprintf(stderr, "outl: port=0x%04x, data=%08x\n", addr, val);
}

int cpu_x86_inb(CPUX86State *env, int addr)
{
    return ioport_readb_table[addr & (MAX_IOPORTS - 1)](env, addr);
}

int cpu_x86_inw(CPUX86State *env, int addr)
{
    return ioport_readw_table[addr & (MAX_IOPORTS - 1)](env, addr);
}

int cpu_x86_inl(CPUX86State *env, int addr)
{
    fprintf(stderr, "inl: port=0x%04x\n", addr);
    return 0;
}

/***********************************************************/
void ioport80_write(CPUX86State *env, uint32_t addr, uint32_t data)
{
}

void hw_error(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "qemu: hardware error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
#ifdef TARGET_I386
    cpu_x86_dump_state(global_env, stderr, X86_DUMP_FPU | X86_DUMP_CCOP);
#endif
    va_end(ap);
    abort();
}

/***********************************************************/
/* vga emulation */
static uint8_t vga_index;
static uint8_t vga_regs[256];
static int last_cursor_pos;

void update_console_messages(void)
{
    int c, i, cursor_pos, eol;

    cursor_pos = vga_regs[0x0f] | (vga_regs[0x0e] << 8);
    eol = 0;
    for(i = last_cursor_pos; i < cursor_pos; i++) {
        c = phys_ram_base[0xb8000 + (i) * 2];
        if (c >= ' ') {
            putchar(c);
            eol = 0;
        } else {
            if (!eol)
                putchar('\n');
            eol = 1;
        }
    }
    fflush(stdout);
    last_cursor_pos = cursor_pos;
}

/* just to see first Linux console messages, we intercept cursor position */
void vga_ioport_write(CPUX86State *env, uint32_t addr, uint32_t data)
{
    switch(addr) {
    case 0x3d4:
        vga_index = data;
        break;
    case 0x3d5:
        vga_regs[vga_index] = data;
        if (vga_index == 0x0f)
            update_console_messages();
        break;
    }
            
}

/***********************************************************/
/* cmos emulation */

#define RTC_SECONDS             0
#define RTC_SECONDS_ALARM       1
#define RTC_MINUTES             2
#define RTC_MINUTES_ALARM       3
#define RTC_HOURS               4
#define RTC_HOURS_ALARM         5
#define RTC_ALARM_DONT_CARE    0xC0

#define RTC_DAY_OF_WEEK         6
#define RTC_DAY_OF_MONTH        7
#define RTC_MONTH               8
#define RTC_YEAR                9

#define RTC_REG_A               10
#define RTC_REG_B               11
#define RTC_REG_C               12
#define RTC_REG_D               13

/* PC cmos mappings */
#define REG_EQUIPMENT_BYTE          0x14

uint8_t cmos_data[128];
uint8_t cmos_index;

void cmos_ioport_write(CPUX86State *env, uint32_t addr, uint32_t data)
{
    if (addr == 0x70) {
        cmos_index = data & 0x7f;
    }
}

uint32_t cmos_ioport_read(CPUX86State *env, uint32_t addr)
{
    int ret;

    if (addr == 0x70) {
        return 0xff;
    } else {
        /* toggle update-in-progress bit for Linux (same hack as
           plex86) */
        ret = cmos_data[cmos_index];
        if (cmos_index == RTC_REG_A)
            cmos_data[RTC_REG_A] ^= 0x80; 
        else if (cmos_index == RTC_REG_C)
            cmos_data[RTC_REG_C] = 0x00; 
        return ret;
    }
}


static inline int to_bcd(int a)
{
    return ((a / 10) << 4) | (a % 10);
}

void cmos_init(void)
{
    struct tm *tm;
    time_t ti;

    ti = time(NULL);
    tm = gmtime(&ti);
    cmos_data[RTC_SECONDS] = to_bcd(tm->tm_sec);
    cmos_data[RTC_MINUTES] = to_bcd(tm->tm_min);
    cmos_data[RTC_HOURS] = to_bcd(tm->tm_hour);
    cmos_data[RTC_DAY_OF_WEEK] = to_bcd(tm->tm_wday);
    cmos_data[RTC_DAY_OF_MONTH] = to_bcd(tm->tm_mday);
    cmos_data[RTC_MONTH] = to_bcd(tm->tm_mon);
    cmos_data[RTC_YEAR] = to_bcd(tm->tm_year % 100);

    cmos_data[RTC_REG_A] = 0x26;
    cmos_data[RTC_REG_B] = 0x02;
    cmos_data[RTC_REG_C] = 0x00;
    cmos_data[RTC_REG_D] = 0x80;

    cmos_data[REG_EQUIPMENT_BYTE] = 0x02; /* FPU is there */

    register_ioport_writeb(0x70, 2, cmos_ioport_write);
    register_ioport_readb(0x70, 2, cmos_ioport_read);
}

/***********************************************************/
/* 8259 pic emulation */

typedef struct PicState {
    uint8_t last_irr; /* edge detection */
    uint8_t irr; /* interrupt request register */
    uint8_t imr; /* interrupt mask register */
    uint8_t isr; /* interrupt service register */
    uint8_t priority_add; /* used to compute irq priority */
    uint8_t irq_base;
    uint8_t read_reg_select;
    uint8_t special_mask;
    uint8_t init_state;
    uint8_t auto_eoi;
    uint8_t rotate_on_autoeoi;
    uint8_t init4; /* true if 4 byte init */
} PicState;

/* 0 is master pic, 1 is slave pic */
PicState pics[2];
int pic_irq_requested;

/* set irq level. If an edge is detected, then the IRR is set to 1 */
static inline void pic_set_irq1(PicState *s, int irq, int level)
{
    int mask;
    mask = 1 << irq;
    if (level) {
        if ((s->last_irr & mask) == 0)
            s->irr |= mask;
        s->last_irr |= mask;
    } else {
        s->last_irr &= ~mask;
    }
}

static inline int get_priority(PicState *s, int mask)
{
    int priority;
    if (mask == 0)
        return -1;
    priority = 7;
    while ((mask & (1 << ((priority + s->priority_add) & 7))) == 0)
        priority--;
    return priority;
}

/* return the pic wanted interrupt. return -1 if none */
static int pic_get_irq(PicState *s)
{
    int mask, cur_priority, priority;

    mask = s->irr & ~s->imr;
    priority = get_priority(s, mask);
    if (priority < 0)
        return -1;
    /* compute current priority */
    cur_priority = get_priority(s, s->isr);
    if (priority > cur_priority) {
        /* higher priority found: an irq should be generated */
        return priority;
    } else {
        return -1;
    }
}

void pic_set_irq(int irq, int level)
{
    pic_set_irq1(&pics[irq >> 3], irq & 7, level);
}

/* can be called at any time outside cpu_exec() to raise irqs if
   necessary */
void pic_handle_irq(void)
{
    int irq2, irq;

    /* first look at slave pic */
    irq2 = pic_get_irq(&pics[1]);
    if (irq2 >= 0) {
        /* if irq request by slave pic, signal master PIC */
        pic_set_irq1(&pics[0], 2, 1);
        pic_set_irq1(&pics[0], 2, 0);
    }
    /* look at requested irq */
    irq = pic_get_irq(&pics[0]);
    if (irq >= 0) {
        if (irq == 2) {
            /* from slave pic */
            pic_irq_requested = 8 + irq2;
        } else {
            /* from master pic */
            pic_irq_requested = irq;
        }
        global_env->hard_interrupt_request = 1;
    }
}

int cpu_x86_get_pic_interrupt(CPUX86State *env)
{
    int irq, irq2, intno;

    /* signal the pic that the irq was acked by the CPU */
    irq = pic_irq_requested;
    if (irq >= 8) {
        irq2 = irq & 7;
        pics[1].isr |= (1 << irq2);
        pics[1].irr &= ~(1 << irq2);
        irq = 2;
        intno = pics[1].irq_base + irq2;
    } else {
        intno = pics[0].irq_base + irq;
    }
    pics[0].isr |= (1 << irq);
    pics[0].irr &= ~(1 << irq);
    return intno;
}

void pic_ioport_write(CPUX86State *env, uint32_t addr, uint32_t val)
{
    PicState *s;
    int priority;

    s = &pics[addr >> 7];
    addr &= 1;
    if (addr == 0) {
        if (val & 0x10) {
            /* init */
            memset(s, 0, sizeof(PicState));
            s->init_state = 1;
            s->init4 = val & 1;
            if (val & 0x02)
                hw_error("single mode not supported");
            if (val & 0x08)
                hw_error("level sensitive irq not supported");
        } else if (val & 0x08) {
            if (val & 0x02)
                s->read_reg_select = val & 1;
            if (val & 0x40)
                s->special_mask = (val >> 5) & 1;
        } else {
            switch(val) {
            case 0x00:
            case 0x80:
                s->rotate_on_autoeoi = val >> 7;
                break;
            case 0x20: /* end of interrupt */
            case 0xa0:
                priority = get_priority(s, s->isr);
                if (priority >= 0) {
                    s->isr &= ~(1 << ((priority + s->priority_add) & 7));
                }
                if (val == 0xa0)
                    s->priority_add = (s->priority_add + 1) & 7;
                break;
            case 0x60 ... 0x67:
                priority = val & 7;
                s->isr &= ~(1 << priority);
                break;
            case 0xc0 ... 0xc7:
                s->priority_add = (val + 1) & 7;
                break;
            case 0xe0 ... 0xe7:
                priority = val & 7;
                s->isr &= ~(1 << priority);
                s->priority_add = (priority + 1) & 7;
                break;
            }
        }
    } else {
        switch(s->init_state) {
        case 0:
            /* normal mode */
            s->imr = val;
            break;
        case 1:
            s->irq_base = val & 0xf8;
            s->init_state = 2;
            break;
        case 2:
            if (s->init4) {
                s->init_state = 3;
            } else {
                s->init_state = 0;
            }
            break;
        case 3:
            s->auto_eoi = (val >> 1) & 1;
            s->init_state = 0;
            break;
        }
    }
}

uint32_t pic_ioport_read(CPUX86State *env, uint32_t addr)
{
    PicState *s;
    s = &pics[addr >> 7];
    addr &= 1;
    if (addr == 0) {
        if (s->read_reg_select)
            return s->isr;
        else
            return s->irr;
    } else {
        return s->imr;
    }
}

void pic_init(void)
{
    register_ioport_writeb(0x20, 2, pic_ioport_write);
    register_ioport_readb(0x20, 2, pic_ioport_read);
    register_ioport_writeb(0xa0, 2, pic_ioport_write);
    register_ioport_readb(0xa0, 2, pic_ioport_read);
}

/***********************************************************/
/* 8253 PIT emulation */

#define PIT_FREQ 1193182

#define RW_STATE_LSB 0
#define RW_STATE_MSB 1
#define RW_STATE_WORD0 2
#define RW_STATE_WORD1 3
#define RW_STATE_LATCHED_WORD0 4
#define RW_STATE_LATCHED_WORD1 5

typedef struct PITChannelState {
    uint16_t count;
    uint16_t latched_count;
    uint8_t rw_state;
    uint8_t mode;
    uint8_t bcd; /* not supported */
    uint8_t gate; /* timer start */
    int64_t count_load_time;
} PITChannelState;

PITChannelState pit_channels[3];
int speaker_data_on;

int64_t ticks_per_sec;

int64_t get_clock(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000LL + tv.tv_usec;
}

int64_t cpu_get_ticks(void)
{
    int64_t val;
    asm("rdtsc" : "=A" (val));
    return val;
}

void cpu_calibrate_ticks(void)
{
    int64_t usec, ticks;

    usec = get_clock();
    ticks = cpu_get_ticks();
    usleep(50 * 1000);
    usec = get_clock() - usec;
    ticks = cpu_get_ticks() - ticks;
    ticks_per_sec = (ticks * 1000000LL + (usec >> 1)) / usec;
}

static int pit_get_count(PITChannelState *s)
{
    int64_t d;
    int counter;

    d = ((cpu_get_ticks() - s->count_load_time) * PIT_FREQ) / 
        ticks_per_sec;
    switch(s->mode) {
    case 0:
    case 1:
    case 4:
    case 5:
        counter = (s->count - d) & 0xffff;
        break;
    default:
        counter = s->count - (d % s->count);
        break;
    }
    return counter;
}

/* get pit output bit */
static int pit_get_out(PITChannelState *s)
{
    int64_t d;
    int out;

    d = ((cpu_get_ticks() - s->count_load_time) * PIT_FREQ) / 
        ticks_per_sec;
    switch(s->mode) {
    default:
    case 0:
        out = (d >= s->count);
        break;
    case 1:
        out = (d < s->count);
        break;
    case 2:
        if ((d % s->count) == 0 && d != 0)
            out = 1;
        else
            out = 0;
        break;
    case 3:
        out = (d % s->count) < (s->count >> 1);
        break;
    case 4:
    case 5:
        out = (d == s->count);
        break;
    }
    return out;
}

void pit_ioport_write(CPUX86State *env, uint32_t addr, uint32_t val)
{
    int channel, access;
    PITChannelState *s;
    
    addr &= 3;
    if (addr == 3) {
        channel = val >> 6;
        if (channel == 3)
            return;
        s = &pit_channels[channel];
        access = (val >> 4) & 3;
        switch(access) {
        case 0:
            s->latched_count = pit_get_count(s);
            s->rw_state = RW_STATE_LATCHED_WORD0;
            break;
        default:
            s->rw_state = access - 1 +  RW_STATE_LSB;
            break;
        }
        s->mode = (val >> 1) & 7;
        s->bcd = val & 1;
    } else {
        s = &pit_channels[addr];
        switch(s->rw_state) {
        case RW_STATE_LSB:
            s->count_load_time = cpu_get_ticks();
            s->count = val;
            break;
        case RW_STATE_MSB:
            s->count_load_time = cpu_get_ticks();
            s->count = (val << 8);
            break;
        case RW_STATE_WORD0:
        case RW_STATE_WORD1:
            if (s->rw_state & 1) {
                s->count_load_time = cpu_get_ticks();
                s->count = (s->latched_count & 0xff) | (val << 8);
            } else {
                s->latched_count = val;
            }
            s->rw_state ^= 1;
            break;
        }
    }
}

uint32_t pit_ioport_read(CPUX86State *env, uint32_t addr)
{
    int ret, count;
    PITChannelState *s;
    
    addr &= 3;
    s = &pit_channels[addr];
    switch(s->rw_state) {
    case RW_STATE_LSB:
    case RW_STATE_MSB:
    case RW_STATE_WORD0:
    case RW_STATE_WORD1:
        count = pit_get_count(s);
        if (s->rw_state & 1)
            ret = (count >> 8) & 0xff;
        else
            ret = count & 0xff;
        if (s->rw_state & 2)
            s->rw_state ^= 1;
        break;
    default:
    case RW_STATE_LATCHED_WORD0:
    case RW_STATE_LATCHED_WORD1:
        if (s->rw_state & 1)
            ret = s->latched_count >> 8;
        else
            ret = s->latched_count & 0xff;
        s->rw_state ^= 1;
        break;
    }
    return ret;
}

void speaker_ioport_write(CPUX86State *env, uint32_t addr, uint32_t val)
{
    speaker_data_on = (val >> 1) & 1;
    pit_channels[2].gate = val & 1;
}

uint32_t speaker_ioport_read(CPUX86State *env, uint32_t addr)
{
    int out;
    out = pit_get_out(&pit_channels[2]);
    return (speaker_data_on << 1) | pit_channels[2].gate | (out << 5);
}

void pit_init(void)
{
    pit_channels[0].gate = 1;
    pit_channels[1].gate = 1;
    pit_channels[2].gate = 0;
    
    register_ioport_writeb(0x40, 4, pit_ioport_write);
    register_ioport_readb(0x40, 3, pit_ioport_read);

    register_ioport_readb(0x61, 1, speaker_ioport_read);
    register_ioport_writeb(0x61, 1, speaker_ioport_write);
    cpu_calibrate_ticks();
}

/***********************************************************/
/* serial port emulation */

#define UART_IRQ        4

#define UART_LCR_DLAB	0x80	/* Divisor latch access bit */

#define UART_IER_MSI	0x08	/* Enable Modem status interrupt */
#define UART_IER_RLSI	0x04	/* Enable receiver line status interrupt */
#define UART_IER_THRI	0x02	/* Enable Transmitter holding register int. */
#define UART_IER_RDI	0x01	/* Enable receiver data interrupt */

#define UART_IIR_NO_INT	0x01	/* No interrupts pending */
#define UART_IIR_ID	0x06	/* Mask for the interrupt ID */

#define UART_IIR_MSI	0x00	/* Modem status interrupt */
#define UART_IIR_THRI	0x02	/* Transmitter holding register empty */
#define UART_IIR_RDI	0x04	/* Receiver data interrupt */
#define UART_IIR_RLSI	0x06	/* Receiver line status interrupt */

#define UART_LSR_TEMT	0x40	/* Transmitter empty */
#define UART_LSR_THRE	0x20	/* Transmit-hold-register empty */
#define UART_LSR_BI	0x10	/* Break interrupt indicator */
#define UART_LSR_FE	0x08	/* Frame error indicator */
#define UART_LSR_PE	0x04	/* Parity error indicator */
#define UART_LSR_OE	0x02	/* Overrun error indicator */
#define UART_LSR_DR	0x01	/* Receiver data ready */

typedef struct SerialState {
    uint8_t divider;
    uint8_t rbr; /* receive register */
    uint8_t ier;
    uint8_t iir; /* read only */
    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr; /* read only */
    uint8_t msr;
    uint8_t scr;
} SerialState;

SerialState serial_ports[1];

void serial_update_irq(void)
{
    SerialState *s = &serial_ports[0];

    if ((s->lsr & UART_LSR_DR) && (s->ier & UART_IER_RDI)) {
        s->iir = UART_IIR_RDI;
    } else if ((s->lsr & UART_LSR_THRE) && (s->ier & UART_IER_THRI)) {
        s->iir = UART_IIR_THRI;
    } else {
        s->iir = UART_IIR_NO_INT;
    }
    if (s->iir != UART_IIR_NO_INT) {
        pic_set_irq(UART_IRQ, 1);
    } else {
        pic_set_irq(UART_IRQ, 0);
    }
}

void serial_ioport_write(CPUX86State *env, uint32_t addr, uint32_t val)
{
    SerialState *s = &serial_ports[0];
    unsigned char ch;
    int ret;
    
    addr &= 7;
    switch(addr) {
    default:
    case 0:
        if (s->lcr & UART_LCR_DLAB) {
            s->divider = (s->divider & 0xff00) | val;
        } else {
            s->lsr &= ~UART_LSR_THRE;
            serial_update_irq();

            ch = val;
            do {
                ret = write(1, &ch, 1);
            } while (ret != 1);
            s->lsr |= UART_LSR_THRE;
            s->lsr |= UART_LSR_TEMT;
            serial_update_irq();
        }
        break;
    case 1:
        if (s->lcr & UART_LCR_DLAB) {
            s->divider = (s->divider & 0x00ff) | (val << 8);
        } else {
            s->ier = val;
            serial_update_irq();
        }
        break;
    case 2:
        break;
    case 3:
        s->lcr = val;
        break;
    case 4:
        s->mcr = val;
        break;
    case 5:
        break;
    case 6:
        s->msr = val;
        break;
    case 7:
        s->scr = val;
        break;
    }
}

uint32_t serial_ioport_read(CPUX86State *env, uint32_t addr)
{
    SerialState *s = &serial_ports[0];
    uint32_t ret;

    addr &= 7;
    switch(addr) {
    default:
    case 0:
        if (s->lcr & UART_LCR_DLAB) {
            ret = s->divider & 0xff; 
        } else {
            ret = s->rbr;
            s->lsr &= ~(UART_LSR_DR | UART_LSR_BI);
            serial_update_irq();
        }
        break;
    case 1:
        if (s->lcr & UART_LCR_DLAB) {
            ret = (s->divider >> 8) & 0xff;
        } else {
            ret = s->ier;
        }
        break;
    case 2:
        ret = s->iir;
        break;
    case 3:
        ret = s->lcr;
        break;
    case 4:
        ret = s->mcr;
        break;
    case 5:
        ret = s->lsr;
        break;
    case 6:
        ret = s->msr;
        break;
    case 7:
        ret = s->scr;
        break;
    }
    return ret;
}

#define TERM_ESCAPE 0x01 /* ctrl-a is used for escape */
static int term_got_escape;

void term_print_help(void)
{
    printf("\n"
           "C-a h    print this help\n"
           "C-a x    exit emulatior\n"
           "C-a b    send break (magic sysrq)\n"
           "C-a C-a  send C-a\n"
           );
}

/* called when a char is received */
void serial_received_byte(SerialState *s, int ch)
{
    if (term_got_escape) {
        term_got_escape = 0;
        switch(ch) {
        case 'h':
            term_print_help();
            break;
        case 'x':
            exit(0);
            break;
        case 'b':
            /* send break */
            s->rbr = 0;
            s->lsr |= UART_LSR_BI | UART_LSR_DR;
            serial_update_irq();
            break;
        case TERM_ESCAPE:
            goto send_char;
        }
    } else if (ch == TERM_ESCAPE) {
        term_got_escape = 1;
    } else {
    send_char:
        s->rbr = ch;
        s->lsr |= UART_LSR_DR;
        serial_update_irq();
    }
}

/* init terminal so that we can grab keys */
static struct termios oldtty;

static void term_exit(void)
{
    tcsetattr (0, TCSANOW, &oldtty);
}

static void term_init(void)
{
    struct termios tty;

    tcgetattr (0, &tty);
    oldtty = tty;

    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                          |INLCR|IGNCR|ICRNL|IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN|ISIG);
    tty.c_cflag &= ~(CSIZE|PARENB);
    tty.c_cflag |= CS8;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    
    tcsetattr (0, TCSANOW, &tty);

    atexit(term_exit);

    fcntl(0, F_SETFL, O_NONBLOCK);
}

void serial_init(void)
{
    SerialState *s = &serial_ports[0];

    s->lsr = UART_LSR_TEMT | UART_LSR_THRE;

    register_ioport_writeb(0x3f8, 8, serial_ioport_write);
    register_ioport_readb(0x3f8, 8, serial_ioport_read);

    term_init();
}

/* cpu signal handler */
static void host_segv_handler(int host_signum, siginfo_t *info, 
                              void *puc)
{
    if (cpu_signal_handler(host_signum, info, puc))
        return;
    term_exit();
    abort();
}

static int timer_irq_pending;

static void host_alarm_handler(int host_signum, siginfo_t *info, 
                               void *puc)
{
    /* just exit from the cpu to have a change to handle timers */
    cpu_x86_interrupt(global_env);
    timer_irq_pending = 1;
}

void help(void)
{
    printf("Virtual Linux version " QEMU_VERSION ", Copyright (c) 2003 Fabrice Bellard\n"
           "usage: vl [-h] bzImage initrd [kernel parameters...]\n"
           "\n"
           "'bzImage' is a Linux kernel image (PAGE_OFFSET must be defined\n"
           "to 0x90000000 in asm/page.h and arch/i386/vmlinux.lds)\n"
           "'initrd' is an initrd image\n"
           "-m megs   set virtual RAM size to megs MB\n"
           "-d        output log in /tmp/vl.log\n"
           "\n"
           "During emulation, use C-a h to get terminal commands:\n"
           );
    term_print_help();
    exit(1);
}

int main(int argc, char **argv)
{
    int c, ret, initrd_size, i;
    struct linux_params *params;
    struct sigaction act;
    struct itimerval itv;
    CPUX86State *env;

    /* we never want that malloc() uses mmap() */
    mallopt(M_MMAP_THRESHOLD, 4096 * 1024);
    
    phys_ram_size = 32 * 1024 * 1024;
    for(;;) {
        c = getopt(argc, argv, "hm:d");
        if (c == -1)
            break;
        switch(c) {
        case 'h':
            help();
            break;
        case 'm':
            phys_ram_size = atoi(optarg) * 1024 * 1024;
            if (phys_ram_size <= 0)
                help();
            break;
        case 'd':
            loglevel = 1;
            break;
        }
    }
    if (optind + 1 >= argc)
        help();

    /* init debug */
    if (loglevel) {
        logfile = fopen(DEBUG_LOGFILE, "w");
        if (!logfile) {
            perror(DEBUG_LOGFILE);
            _exit(1);
        }
        setvbuf(logfile, NULL, _IOLBF, 0);
    }

    /* init the memory */
    strcpy(phys_ram_file, "/tmp/vlXXXXXX");
    if (mkstemp(phys_ram_file) < 0) {
        fprintf(stderr, "Could not create temporary memory file\n");
        exit(1);
    }
    phys_ram_fd = open(phys_ram_file, O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (phys_ram_fd < 0) {
        fprintf(stderr, "Could not open temporary memory file\n");
        exit(1);
    }
    ftruncate(phys_ram_fd, phys_ram_size);
    unlink(phys_ram_file);
    phys_ram_base = mmap((void *)PHYS_RAM_BASE, phys_ram_size, 
                         PROT_WRITE | PROT_READ, MAP_SHARED | MAP_FIXED, 
                         phys_ram_fd, 0);
    if (phys_ram_base == MAP_FAILED) {
        fprintf(stderr, "Could not map physical memory\n");
        exit(1);
    }

    /* now we can load the kernel */
    ret = load_kernel(argv[optind], phys_ram_base + KERNEL_LOAD_ADDR);
    if (ret < 0) {
        fprintf(stderr, "%s: could not load kernel\n", argv[optind]);
        exit(1);
    }

    /* load initrd */
    initrd_size = load_image(argv[optind + 1], phys_ram_base + INITRD_LOAD_ADDR);
    if (initrd_size < 0) {
        fprintf(stderr, "%s: could not load initrd\n", argv[optind + 1]);
        exit(1);
    }

    /* init kernel params */
    params = (void *)(phys_ram_base + KERNEL_PARAMS_ADDR);
    memset(params, 0, sizeof(struct linux_params));
    params->mount_root_rdonly = 0;
    params->cl_magic = 0xA33F;
    params->cl_offset = params->commandline - (uint8_t *)params;
    params->ext_mem_k = (phys_ram_size / 1024) - 1024;
    for(i = optind + 2; i < argc; i++) {
        if (i != optind + 2)
            pstrcat(params->commandline, sizeof(params->commandline), " ");
        pstrcat(params->commandline, sizeof(params->commandline), argv[i]);
    }
    params->loader_type = 0x01;
    if (initrd_size > 0) {
        params->initrd_start = INITRD_LOAD_ADDR;
        params->initrd_size = initrd_size;
    }
    params->orig_video_lines = 25;
    params->orig_video_cols = 80;

    /* init basic PC hardware */
    init_ioports();
    register_ioport_writeb(0x80, 1, ioport80_write);

    register_ioport_writeb(0x3d4, 2, vga_ioport_write);

    cmos_init();
    pic_init();
    pit_init();
    serial_init();

    /* setup cpu signal handlers for MMU / self modifying code handling */
    sigfillset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = host_segv_handler;
    sigaction(SIGSEGV, &act, NULL);
    sigaction(SIGBUS, &act, NULL);

    act.sa_sigaction = host_alarm_handler;
    sigaction(SIGALRM, &act, NULL);

    /* init CPU state */
    env = cpu_init();
    global_env = env;

    /* setup basic memory access */
    env->cr[0] = 0x00000033;
    cpu_x86_init_mmu(env);
    
    memset(params->idt_table, 0, sizeof(params->idt_table));

    params->gdt_table[2] = 0x00cf9a000000ffffLL; /* KERNEL_CS */
    params->gdt_table[3] = 0x00cf92000000ffffLL; /* KERNEL_DS */
    
    env->idt.base = (void *)params->idt_table;
    env->idt.limit = sizeof(params->idt_table) - 1;
    env->gdt.base = (void *)params->gdt_table;
    env->gdt.limit = sizeof(params->gdt_table) - 1;

    cpu_x86_load_seg(env, R_CS, KERNEL_CS);
    cpu_x86_load_seg(env, R_DS, KERNEL_DS);
    cpu_x86_load_seg(env, R_ES, KERNEL_DS);
    cpu_x86_load_seg(env, R_SS, KERNEL_DS);
    cpu_x86_load_seg(env, R_FS, KERNEL_DS);
    cpu_x86_load_seg(env, R_GS, KERNEL_DS);
    
    env->eip = KERNEL_LOAD_ADDR;
    env->regs[R_ESI] = KERNEL_PARAMS_ADDR;
    env->eflags = 0x2;

    itv.it_interval.tv_sec = 0;
    itv.it_interval.tv_usec = 10 * 1000;
    itv.it_value.tv_sec = 0;
    itv.it_value.tv_usec = 10 * 1000;
    setitimer(ITIMER_REAL, &itv, NULL);

    for(;;) {
        struct pollfd ufds[1], *pf;
        int ret, n, timeout;
        uint8_t ch;

        ret = cpu_x86_exec(env);

        /* if hlt instruction, we wait until the next IRQ */
        if (ret == EXCP_HLT) 
            timeout = 10;
        else
            timeout = 0;
        /* poll any events */
        pf = ufds;
        if (!(serial_ports[0].lsr & UART_LSR_DR)) {
            pf->fd = 0;
            pf->events = POLLIN;
            pf++;
        }
        ret = poll(ufds, pf - ufds, timeout);
        if (ret > 0) {
            if (ufds[0].revents & POLLIN) {
                n = read(0, &ch, 1);
                if (n == 1) {
                    serial_received_byte(&serial_ports[0], ch);
                }
            }
        }

        /* just for testing */
        if (timer_irq_pending) {
            pic_set_irq(0, 1);
            pic_set_irq(0, 0);
            timer_irq_pending = 0;
        }

        pic_handle_irq();
    }

    return 0;
}
