#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "nk_assets.h"

NkU16 nk_read_u16(const void *ptr)
{
    const NkU8 *p;
    p = (const NkU8 *)ptr;
    return (NkU16)((NkU16)p[0] | ((NkU16)p[1] << 8));
}

NkU32 nk_read_u32(const void *ptr)
{
    const NkU8 *p;
    p = (const NkU8 *)ptr;
    return (NkU32)((NkU32)p[0] |
                   ((NkU32)p[1] << 8) |
                   ((NkU32)p[2] << 16) |
                   ((NkU32)p[3] << 24));
}

NkU16 nk_read_be16(const void *ptr)
{
    const NkU8 *p;
    p = (const NkU8 *)ptr;
    return (NkU16)(((NkU16)p[0] << 8) | (NkU16)p[1]);
}

NkU32 nk_read_be32(const void *ptr)
{
    const NkU8 *p;
    p = (const NkU8 *)ptr;
    return (NkU32)(((NkU32)p[0] << 24) |
                   ((NkU32)p[1] << 16) |
                   ((NkU32)p[2] << 8) |
                   (NkU32)p[3]);
}

int nk_ascii_casecmp(const char *a, const char *b)
{
    unsigned char ca;
    unsigned char cb;
    if (a == NULL || b == NULL) {
        if (a == b) {
            return 0;
        }
        return a == NULL ? -1 : 1;
    }
    for (;;) {
        ca = (unsigned char)tolower((int)(unsigned char)*a);
        cb = (unsigned char)tolower((int)(unsigned char)*b);
        if (ca != cb || ca == 0 || cb == 0) {
            return (int)ca - (int)cb;
        }
        ++a;
        ++b;
    }
}

int nk_path_join(char *out, size_t out_size, const char *a, const char *b)
{
    size_t alen;
    size_t blen;
    int separator;
    if (out == NULL || out_size == 0 || a == NULL || b == NULL) {
        return 0;
    }
    alen = strlen(a);
    blen = strlen(b);
    separator = alen != 0 && a[alen - 1] != '/';
    if (alen + blen + (separator ? 1u : 0u) + 1u > out_size) {
        out[0] = '\0';
        return 0;
    }
    memcpy(out, a, alen);
    if (separator) {
        out[alen++] = '/';
    }
    memcpy(out + alen, b, blen + 1u);
    return 1;
}

int nk_file_load(const char *path, NkBlob *blob)
{
    FILE *fp;
    long length;
    size_t got;
    if (blob == NULL) {
        return 0;
    }
    blob->data = NULL;
    blob->size = 0;
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    length = ftell(fp);
    if (length < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }
    if (length == 0) {
        fclose(fp);
        return 1;
    }
    blob->data = (NkU8 *)malloc((size_t)length);
    if (blob->data == NULL) {
        fclose(fp);
        return 0;
    }
    got = fread(blob->data, 1, (size_t)length, fp);
    fclose(fp);
    if (got != (size_t)length) {
        free(blob->data);
        blob->data = NULL;
        return 0;
    }
    blob->size = (size_t)length;
    return 1;
}

void nk_blob_free(NkBlob *blob)
{
    if (blob != NULL) {
        free(blob->data);
        blob->data = NULL;
        blob->size = 0;
    }
}

int nk_find_file(const char *directory, const char *name,
                 char *out, size_t out_size)
{
    DIR *dir;
    struct dirent *entry;
    struct stat st;
    char path[1024];
    if (directory == NULL || name == NULL || out == NULL || out_size == 0) {
        return 0;
    }
    out[0] = '\0';
    dir = opendir(directory);
    if (dir == NULL) {
        return 0;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (nk_ascii_casecmp(entry->d_name, name) != 0) {
            continue;
        }
        if (!nk_path_join(path, sizeof(path), directory, entry->d_name)) {
            continue;
        }
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        if (strlen(path) + 1u <= out_size) {
            strcpy(out, path);
            closedir(dir);
            return 1;
        }
    }
    closedir(dir);
    return 0;
}

unsigned long nk_rand(unsigned long *state)
{
    unsigned long value;
    value = (*state * 1664525ul + 1013904223ul) & 0xfffffffful;
    *state = value;
    return value;
}

int nk_clamp_int(int value, int minimum, int maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

double nk_clamp_double(double value, double minimum, double maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

void nk_set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0) {
        strncpy(error, message, error_size - 1u);
        error[error_size - 1u] = '\0';
    }
}
