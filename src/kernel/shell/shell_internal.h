#pragma once
// Things shared by the shell, the commands, the editor and neofetch.
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <boot/bootparams.h>
#include <fs/path.h>
#include <fs/fat.h>

#define SHELL_MAX_LINE      1024
#define SHELL_MAX_ARGS      64
#define FILE_BUFFER_SIZE    (64 * 1024)     // biggest file for edit, cp, >>

typedef int (*CommandFunction)(int argc, char** argv);

typedef struct {
    const char* Name;
    CommandFunction Function;
    const char* Usage;
    const char* Description;
} Command;

extern const Command g_Commands[];
extern const int g_CommandCount;

extern char g_Cwd[PATH_MAX_LENGTH];         // current directory
extern BootParams* g_ShellBootParams;
extern uint8_t g_FileBuffer[FILE_BUFFER_SIZE];

const Command* Shell_FindCommand(const char* name);

// relative/absolute path typed by the user → absolute normalized path
bool Shell_ResolvePath(const char* command, const char* arg, char* out);

// "command: arg: message"
void Shell_Error(const char* command, const char* arg, const char* message);

// command history (for the "history" command)
int Shell_HistoryCount();
const char* Shell_HistoryGet(int index);    // 0 = oldest

int Editor_Run(const char* path);
void Neofetch_Run();
