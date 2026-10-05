#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef int fd_t;

#define VFS_FD_STDIN    0
#define VFS_FD_STDOUT   1
#define VFS_FD_STDERR   2
#define VFS_FD_DEBUG    3

int VFS_Write(fd_t file, uint8_t* data, size_t size);

// Redirection: while a capture buffer is set, everything written to stdout
// goes into the buffer instead of the screen (used by "command > file").
void VFS_StartCapture(char* buffer, size_t capacity);
size_t VFS_StopCapture();           // returns the number of captured bytes
bool VFS_CaptureOverflow();         // true if the output didn't fit

// Copy stdout/stderr also to the debug port (QEMU -debugcon), default on
void VFS_SetStdoutMirror(bool enabled);

// writes one screen character (code page 437) to the debug port as UTF-8
void VFS_DebugPutChar(uint8_t c);
