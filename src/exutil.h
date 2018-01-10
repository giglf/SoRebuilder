#ifndef _SO_REBUILDER_EXUTIL_H_
#define _SO_REBUILDER_EXUTIL_H_

#include "elf.h"
//check the environment and define the necessery type
#ifndef __LP64__
typedef Elf32_Addr Elf_Addr;
typedef Elf32_Dyn Elf_Dyn;
typedef Elf32_Ehdr Elf_Ehdr;
typedef Elf32_Half Elf_Half;
typedef Elf32_Off Elf_Off;
typedef Elf32_Phdr Elf_Phdr;
typedef Elf32_Rel Elf_Rel;
typedef Elf32_Rela Elf_Rela;
typedef Elf32_Shdr Elf_Shdr;
typedef Elf32_Sword Elf_Sword;
typedef Elf32_Sym Elf_Sym;
typedef Elf32_Word Elf_Word;
#else
// Because we handle with 32bits ELF file. 
// In 32bits Elf file, the header size, section size, etc, 
// should use 32 bit type, or it will read out range of header.
typedef Elf64_Addr Elf_Addr;
typedef Elf32_Dyn Elf_Dyn;
typedef Elf32_Ehdr Elf_Ehdr;
typedef Elf64_Half Elf_Half;
typedef Elf64_Off Elf_Off;
typedef Elf32_Phdr Elf_Phdr;
typedef Elf32_Rel Elf_Rel;
typedef Elf32_Rela Elf_Rela;
typedef Elf32_Shdr Elf_Shdr;
typedef Elf64_Sword Elf_Sword;
typedef Elf32_Sym Elf_Sym;
typedef Elf64_Word Elf_Word;
#endif


#ifndef PAGE_SIZE
#define PAGE_SIZE 0x1000
#endif

#define PAGE_MASK (~(PAGE_SIZE-1))
// Returns the address of the page containing address 'x'.
#define PAGE_START(x)  ((x) & PAGE_MASK)

// Returns the offset of address 'x' in its page.
#define PAGE_OFFSET(x) ((x) & ~PAGE_MASK)

// Returns the address of the next page after address 'x', unless 'x' is
// itself at the start of a page.
#define PAGE_END(x)    PAGE_START((x) + (PAGE_SIZE-1))

#endif