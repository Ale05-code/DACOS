#include "path.h"
#include <string.h>

#define MAX_COMPONENTS  32

bool Path_Resolve(const char* cwd, const char* input, char* out, size_t size)
{
    // copy "cwd/input" (or just "input" if it is absolute) into a work buffer
    char work[PATH_MAX_LENGTH * 2];
    work[0] = '\0';
    if (input[0] != '/')
    {
        strlcpy(work, cwd, sizeof(work));
        strlcat(work, "/", sizeof(work));
    }
    if (strlcat(work, input, sizeof(work)) >= sizeof(work))
        return false;

    // split into components, handling "." and ".."
    char* components[MAX_COMPONENTS];
    int count = 0;

    char* p = work;
    while (*p)
    {
        while (*p == '/')
            *p++ = '\0';
        if (*p == '\0')
            break;

        char* start = p;
        while (*p && *p != '/')
            p++;
        if (*p == '/')
            *p++ = '\0';

        if (strcmp(start, ".") == 0)
            continue;
        if (strcmp(start, "..") == 0)
        {
            if (count > 0)
                count--;
            continue;
        }
        if (count >= MAX_COMPONENTS)
            return false;
        components[count++] = start;
    }

    // join again
    if (size < 2)
        return false;
    out[0] = '\0';
    if (count == 0)
        return strlcpy(out, "/", size) < size;

    for (int i = 0; i < count; i++)
    {
        if (strlcat(out, "/", size) >= size || strlcat(out, components[i], size) >= size)
            return false;
    }
    return true;
}

void Path_Split(const char* path, char* parent, size_t parentSize, char* name, size_t nameSize)
{
    const char* slash = strrchr(path, '/');
    if (slash == NULL)
    {
        strlcpy(parent, "/", parentSize);
        strlcpy(name, path, nameSize);
        return;
    }

    strlcpy(name, slash + 1, nameSize);

    size_t parentLen = slash - path;
    if (parentLen == 0)
        strlcpy(parent, "/", parentSize);
    else
    {
        size_t n = parentLen < parentSize - 1 ? parentLen : parentSize - 1;
        for (size_t i = 0; i < n; i++)
            parent[i] = path[i];
        parent[n] = '\0';
    }
}

bool Path_IsInside(const char* path, const char* prefix)
{
    size_t len = strlen(prefix);
    if (strncmp(path, prefix, len) != 0)
        return false;
    return path[len] == '\0' || path[len] == '/' || (len == 1 && prefix[0] == '/');
}
