#include "elf.h"
#include "fat.h"
#include "memdefs.h"
#include "memory.h"
#include "minmax.h"
#include "stdio.h"
#include <stddef.h>

bool ELF_Read(Partition* part, const char* path, void** entryPoint)
{
    uint8_t* headerBuffer = MEMORY_ELF_ADDR;
    uint8_t* loadBuffer = MEMORY_LOAD_KERNEL;
    uint32_t filePos = 0;
    uint32_t read;

    // Read header
    FAT_File* fd = FAT_Open(part, path);
    if (fd == NULL)
    {
        printf("ELF: can't open %s\n", path);
        return false;
    }

    if ((read = FAT_Read(part, fd, sizeof(ELFHeader), headerBuffer)) != sizeof(ELFHeader))
    {
        printf("ELF Load error!\n");
        FAT_Close(fd);
        return false;
    }
    filePos += read;

    // validate header
    bool ok = true;
    ELFHeader* header = (ELFHeader*)headerBuffer;
    ok = ok && (memcmp(header->Magic, ELF_MAGIC, 4) == 0);
    ok = ok && (header->Bitness == ELF_BITNESS_32BIT);
    ok = ok && (header->Endianness == ELF_ENDIANNESS_LITTLE);
    ok = ok && (header->ELFHeaderVersion == 1);
    ok = ok && (header->ELFVersion == 1);
    ok = ok && (header->Type == ELF_TYPE_EXECUTABLE);
    ok = ok && (header->InstructionSet == ELF_INSTRUCTION_SET_X86);
    ok = ok && (header->ProgramHeaderTablePosition >= filePos);
    ok = ok && (header->ProgramHeaderTableEntrySize >= sizeof(ELFProgramHeader));

    if (!ok)
    {
        printf("ELF: %s is not a valid i686 executable\n", path);
        FAT_Close(fd);
        return false;
    }

    *entryPoint = (void*)header->ProgramEntryPosition;

    // load program header
    uint32_t programHeaderOffset = header->ProgramHeaderTablePosition;
    uint32_t programHeaderSize = header->ProgramHeaderTableEntrySize * header->ProgramHeaderTableEntryCount;
    uint32_t programHeaderTableEntrySize = header->ProgramHeaderTableEntrySize;
    uint32_t programHeaderTableEntryCount = header->ProgramHeaderTableEntryCount;

    if (programHeaderSize > MEMORY_ELF_SIZE)
    {
        printf("ELF: program header table too big\n");
        FAT_Close(fd);
        return false;
    }

    filePos += FAT_Read(part, fd, programHeaderOffset - filePos, headerBuffer);
    if ((read = FAT_Read(part, fd, programHeaderSize, headerBuffer)) != programHeaderSize)
    {
        printf("ELF Load error!\n");
        FAT_Close(fd);
        return false;
    }
    filePos += read;
    FAT_Close(fd);

    // parse program header entries
    for (uint32_t i = 0; i < programHeaderTableEntryCount; i++)
    {
        ELFProgramHeader* progHeader = (ELFProgramHeader*)(headerBuffer + i * programHeaderTableEntrySize);
        if (progHeader->Type == ELF_PROGRAM_TYPE_LOAD) {
            // the program must be loaded above 1 MB, so it can't overwrite
            // the stage2, its buffers or the BIOS areas
            if (progHeader->VirtualAddress < (uint32_t)MEMORY_KERNEL_ADDR
                || progHeader->FileSize > progHeader->MemorySize)
            {
                printf("ELF: invalid segment at 0x%x\n", progHeader->VirtualAddress);
                return false;
            }

            uint8_t* virtAddress = (uint8_t*)progHeader->VirtualAddress;
            memset(virtAddress, 0, progHeader->MemorySize);

            // ugly nasty seeking
            // TODO: proper seeking
            fd = FAT_Open(part, path);
            if (fd == NULL)
            {
                printf("ELF: can't open %s\n", path);
                return false;
            }

            while (progHeader->Offset > 0) 
            {
                uint32_t shouldRead = min(progHeader->Offset, MEMORY_LOAD_SIZE);
                read = FAT_Read(part, fd, shouldRead, loadBuffer);
                if (read != shouldRead) {
                    printf("ELF Load error!\n");
                    FAT_Close(fd);
                    return false;
                }
                progHeader->Offset -= read;
            }

            // read program
            while (progHeader->FileSize > 0) 
            {
                uint32_t shouldRead = min(progHeader->FileSize, MEMORY_LOAD_SIZE);
                read = FAT_Read(part, fd, shouldRead, loadBuffer);
                if (read != shouldRead) {
                    printf("ELF Load error!\n");
                    FAT_Close(fd);
                    return false;
                }
                progHeader->FileSize -= read;

                memcpy(virtAddress, loadBuffer, read);
                virtAddress += read;
            }

            FAT_Close(fd);
        }
    }

    return true;
}
