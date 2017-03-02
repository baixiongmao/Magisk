#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>

#include "bootimg.h"
#include "elf.h"
#include "magiskboot.h"

unsigned char *base, *kernel, *ramdisk, *second, *dtb;
boot_img_hdr hdr;
int mtk_kernel = 0, mtk_ramdisk = 0;
file_t boot_type, ramdisk_type, dtb_type;

file_t check_type(unsigned char *buf) {
	if (memcmp(buf, CHROMEOS_MAGIC, CHROMEOS_MAGIC_SIZE) == 0) {
		return CHROMEOS;
	} else if (memcmp(buf, BOOT_MAGIC, BOOT_MAGIC_SIZE) == 0) {
		return AOSP;
	} else if (memcmp(buf, ELF_MAGIC, ELF_MAGIC_SIZE) == 0) {
		return ELF;
	} else if (memcmp(buf, "\x1f\x8b\x08\x00", 4) == 0) {
		return GZIP;
	} else if (memcmp(buf, "\x89\x4c\x5a\x4f\x00\x0d\x0a\x1a\x0a", 9) == 0) {
		return LZOP;
	} else if (memcmp(buf, "\xfd""7zXZ\x00", 6) == 0) {
		return XZ;
	} else if (memcmp(buf, "\x5d\x00\x00", 3) == 0 
			&& (buf[12] == (unsigned char) '\xff' || buf[12] == (unsigned char) '\x00')) {
		return LZMA;
	} else if (memcmp(buf, "BZh", 3) == 0) {
		return BZIP2;
	} else if ( (  memcmp(buf, "\x04\x22\x4d\x18", 4) == 0 
				|| memcmp(buf, "\x03\x21\x4c\x18", 4) == 0) 
				|| memcmp(buf, "\x02\x21\x4c\x18", 4) == 0) {
		return LZ4;
	} else if (memcmp(buf, "\x88\x16\x88\x58", 4) == 0) {
		return MTK;
	} else if (memcmp(buf, "QCDT", 4) == 0) {
		return QCDT;
	} else {
		return UNKNOWN;
	}
}

static void check_headers() {
	printf("KERNEL [%d] @ 0x%08x\n", hdr.kernel_size, hdr.kernel_addr);
	printf("RAMDISK [%d] @ 0x%08x\n", hdr.ramdisk_size, hdr.ramdisk_addr);
	printf("SECOND [%d] @ 0x%08x\n", hdr.second_size, hdr.second_addr);
	printf("DTB [%d] @ 0x%08x\n", hdr.dt_size, hdr.tags_addr);
	printf("PAGESIZE [%d]\n", hdr.page_size);
	if (hdr.os_version != 0) {
		int a,b,c,y,m = 0;
		int os_version, os_patch_level;
		os_version = hdr.os_version >> 11;
		os_patch_level = hdr.os_version & 0x7ff;
		
		a = (os_version >> 14) & 0x7f;
		b = (os_version >> 7) & 0x7f;
		c = os_version & 0x7f;
		printf("OS_VERSION [%d.%d.%d]\n", a, b, c);
		
		y = (os_patch_level >> 4) + 2000;
		m = os_patch_level & 0xf;
		printf("PATCH_LEVEL [%d-%02d]\n", y, m);
	}
	printf("NAME [%s]\n", hdr.name);
	printf("CMDLINE [%s]\n", hdr.cmdline);

	ramdisk_type = check_type(ramdisk);

	switch (ramdisk_type) {
		case GZIP:
			printf("COMPRESSION [%s]\n", "gzip");
			break;
		case LZOP:
			printf("COMPRESSION [%s]\n", "lzop");
			break;
		case XZ:
			printf("COMPRESSION [%s]\n", "xz");
			break;
		case LZMA:
			printf("COMPRESSION [%s]\n", "lzma");
			break;
		case BZIP2:
			printf("COMPRESSION [%s]\n", "bzip2");
			break;
		case LZ4:
			printf("COMPRESSION [%s]\n", "lz4");
			break;
		default:
			error(1, "Unknown ramdisk format!");
	}

	// Check MTK
	if (check_type(kernel) == MTK) {
		printf("MTK header found in kernel\n");
		mtk_kernel = 1;
	}
	if (check_type(ramdisk) == MTK) {
		printf("MTK header found in ramdisk\n");
		mtk_ramdisk = 1;
	}

	// Check dtb if ELF boot
	if (boot_type == ELF && hdr.dt_size) {
		dtb_type = check_type(dtb);
	}
}

static void page_align(unsigned char **pos) {
	uint32_t itemsize = *pos - base, pagemask = hdr.page_size - 1L;
	if (itemsize & pagemask) {
		*pos += hdr.page_size - (itemsize & pagemask);
	}
}

static void elf_header_check(void *elf, int is64) {

	size_t e_size, mach, ver, p_size, p_num, s_size, s_num;
	size_t r_e_size, r_p_size, r_s_size;

	if (is64) {
		e_size = ((elf64_ehdr *) elf)->e_ehsize;
		mach = ((elf64_ehdr *) elf)->e_machine;
		ver = ((elf64_ehdr *) elf)->e_version;
		p_size = ((elf64_ehdr *) elf)->e_phentsize;
		p_num = ((elf64_ehdr *) elf)->e_phnum;
		s_size = ((elf64_ehdr *) elf)->e_shentsize;
		s_num = ((elf64_ehdr *) elf)->e_shnum;
		r_e_size = sizeof(elf64_ehdr);
		r_p_size = sizeof(elf64_phdr);
		r_s_size = sizeof(elf64_shdr);
	} else {
		e_size = ((elf32_ehdr *) elf)->e_ehsize;
		mach = ((elf32_ehdr *) elf)->e_machine;
		ver = ((elf32_ehdr *) elf)->e_version;
		p_size = ((elf32_ehdr *) elf)->e_phentsize;
		p_num = ((elf32_ehdr *) elf)->e_phnum;
		s_size = ((elf32_ehdr *) elf)->e_shentsize;
		s_num = ((elf32_ehdr *) elf)->e_shnum;
		r_e_size = sizeof(elf32_ehdr);
		r_p_size = sizeof(elf32_phdr);
		r_s_size = sizeof(elf32_shdr);
	}

	if (e_size != r_e_size)
		error(1, "Header size not %d", r_e_size);

	if (mach != EM_ARM)
		error(1, "ELF machine is not ARM");

	if (ver != 1)
		error(1, "Unknown ELF version");

	if (p_size != r_p_size)
		error(1, "Program header size not %d", r_p_size);

	if (p_num < 2 || p_num > 4)
		error(1, "Unexpected number of elements: %d", p_num);

	if (s_num && s_size != r_s_size)
		error(1, "Section header size not %d", r_s_size);

	if (s_num > 1)
		error(1, "More than one section header");
}

static void elf_set(int i, size_t size, size_t offset, size_t addr) {
	if (size <= 4096) {
		// Possible cmdline
		memset(hdr.cmdline, 0, BOOT_ARGS_SIZE);
		strncpy((char *) hdr.cmdline, (char *) (base + offset), BOOT_ARGS_SIZE);
		hdr.cmdline[strcspn((char*) hdr.cmdline, "\n")] = '\0';
		return;
	}
	switch(i) {
		case 0:
			// kernel
			kernel = base + offset;
			hdr.kernel_size = size;
			hdr.kernel_addr = addr;
			break;
		case 1:
			// ramdisk
			ramdisk = base + offset;
			hdr.ramdisk_size = size;
			hdr.ramdisk_addr = addr;
			break;
		case 2:
			// dtb
			dtb = base + offset;
			hdr.dt_size = size;
			hdr.tags_addr = addr;
			break;
	}
}

static void parse_elf() {

	// Reset boot image header
	memset(&hdr, 0, sizeof(hdr));

	// Hardcode header magic and pagesize
	memcpy(hdr.magic, BOOT_MAGIC, BOOT_MAGIC_SIZE);
	hdr.page_size = 4096;

	switch(base[EI_CLASS]) {

		case ELFCLASS32: {

			elf32_ehdr *elf32;
			elf32_phdr *ph32;
			elf32_shdr *sh32;

			printf("IMAGE [ELF32]\n");

			elf32 = (elf32_ehdr *) base;

			elf_header_check(elf32, 0);

			ph32 = (elf32_phdr *) (base + elf32->e_phoff);
			sh32 = (elf32_shdr *) (base + elf32->e_shoff);

			for (int i = 0; i < elf32->e_phnum; ++i) {
				elf_set(i, ph32[i].p_filesz, ph32[i].p_offset, ph32[i].p_paddr);
			}

			if (elf32->e_shnum) {
				// cmdline
				memset(hdr.cmdline, 0, BOOT_ARGS_SIZE);
				strncpy((char *) hdr.cmdline, (char *) (base + sh32->s_offset + 8), BOOT_ARGS_SIZE);
				hdr.cmdline[strcspn((char*) hdr.cmdline, "\n")] = '\0';
			}

			break;
		}

		case ELFCLASS64: {

			elf64_ehdr *elf64;
			elf64_phdr *ph64;
			elf64_shdr *sh64;

			printf("IMAGE [ELF64]\n");

			elf64 = (elf64_ehdr *) base;

			elf_header_check(elf64, 1);

			ph64 = (elf64_phdr *) (base + elf64->e_phoff);
			sh64 = (elf64_shdr *) (base + elf64->e_shoff);

			for (int i = 0; i < elf64->e_phnum; ++i) {
				elf_set(i, ph64[i].p_filesz, ph64[i].p_offset, ph64[i].p_paddr);
			}

			if (elf64->e_shnum) {
				// cmdline
				memset(hdr.cmdline, 0, BOOT_ARGS_SIZE);
				strncpy((char *) hdr.cmdline, (char *) (base + sh64->s_offset + 8), BOOT_ARGS_SIZE);
				hdr.cmdline[strcspn((char*) hdr.cmdline, "\n")] = '\0';
			}
			break;
		}

		default:
			error(1, "ELF format error!");
	}

	check_headers();
}

static void parse_aosp() {

	printf("IMG [AOSP]\n");

	unsigned char *pos = base;

	// Read the header
	memcpy(&hdr, pos, sizeof(hdr));
	pos += hdr.page_size;

	// Kernel position
	kernel = pos;
	pos += hdr.kernel_size;
	page_align(&pos);

	// Ramdisk position
	ramdisk = pos;
	pos += hdr.ramdisk_size;
	page_align(&pos);

	if (hdr.second_size) {
		// Second position
		second = pos;
		pos += hdr.second_size;
		page_align(&pos);
	}

	if (hdr.dt_size) {
		// dtb position
		dtb = pos;
		pos += hdr.dt_size;
		page_align(&pos);
	}

	check_headers();
}

void parse_img(unsigned char *orig, size_t size) {
	for(base = orig; base < (orig + size); base += 256) {
		switch (check_type(base)) {
			case CHROMEOS:
				boot_type = CHROMEOS;
				continue;
			case AOSP:
				// Don't override CHROMEOS
				if (boot_type != CHROMEOS)
					boot_type = AOSP;
				parse_aosp();
				return;
			case ELF:
				boot_type = ELF;
				parse_elf();
				return;
			default:
				continue;
		}
	}
	error(1, "No boot image magic found!");
}
