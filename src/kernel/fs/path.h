#pragma once
#include <stdbool.h>
#include <stddef.h>

#define PATH_MAX_LENGTH     256

// Builds the absolute, normalized path of "input" seen from the directory "cwd":
//   cwd = "/docs", input = "../a/./b.txt"  →  "/a/b.txt"
// Returns false if the result doesn't fit in "size" bytes.
bool Path_Resolve(const char* cwd, const char* input, char* out, size_t size);

// "/a/b/c.txt" → parent "/a/b", name "c.txt";  "/x" → parent "/", name "x"
void Path_Split(const char* path, char* parent, size_t parentSize, char* name, size_t nameSize);

// true if "path" is "prefix" or inside it ("/boot" is inside "/boot", "/boot/x" too, "/bootx" no)
bool Path_IsInside(const char* path, const char* prefix);
