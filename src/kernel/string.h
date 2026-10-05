#pragma once
#include <stddef.h>
#include <stdbool.h>

size_t strlen(const char* str);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t n);
int strcasecmp(const char* a, const char* b);           // case-insensitive (ASCII)
char* strchr(const char* str, int c);
char* strrchr(const char* str, int c);

// Safe copy/append: always 0-terminated, never write more than size bytes.
// Return the length of the string they tried to create (like BSD strlcpy/strlcat).
size_t strlcpy(char* dst, const char* src, size_t size);
size_t strlcat(char* dst, const char* src, size_t size);

void* memmove(void* dst, const void* src, size_t num);

int toupper(int c);
int tolower(int c);
bool isdigit(int c);
bool isalpha(int c);
bool isalnum(int c);
bool isspace(int c);

int atoi(const char* str);
