#ifndef NK_ASSETS_H
#define NK_ASSETS_H

#include <stdio.h>
#include "nk_types.h"

#define NK_SCENE_SPRITES 80
#define NK_CHARACTER_SPRITES 256
#define NK_CHARACTER_COUNT 8
#define NK_CHARACTER_CLIPS 14
#define NK_TITLE_CLIPS 5
#define NK_SELECT_CLIPS 2
#define NK_GLOBAL_CLIPS 31
#define NK_TIMBRE_COUNT 128

#define NK_CHAR_BUDDY 0
#define NK_CHAR_FETUS 1
#define NK_CHAR_GONZOLES 2
#define NK_CHAR_GURDIP 3
#define NK_CHAR_SINAMMON 4
#define NK_CHAR_KLUBBOR 5
#define NK_CHAR_ED 6
#define NK_CHAR_HENRY 7

typedef struct NkBlob {
    NkU8 *data;
    size_t size;
} NkBlob;

typedef struct NkPalette {
    NkU8 rgb[768];
    NkU32 argb[256];
} NkPalette;

typedef struct NkSprite {
    int width;
    int height;
    NkU8 *pixels;
} NkSprite;

typedef struct NkClip {
    NkS8 *data;
    size_t length;
    unsigned int rate;
} NkClip;

typedef struct NkScene {
    NkPalette palette;
    NkSprite sprites[NK_SCENE_SPRITES];
    int sprite_count;
    NkBlob music1;
    NkBlob music2;
    NkBlob auxiliary;
    NkSprite layers[3];
    NkSprite background;
} NkScene;

typedef struct NkCharacter {
    char name[24];
    NkSprite sprites[NK_CHARACTER_SPRITES];
    int sprite_count;
    NkU8 metadata[15];
    NkU8 remap[256];
    NkClip clips[NK_CHARACTER_CLIPS];
    int clip_count;
} NkCharacter;

typedef struct NkFont {
    NkSprite glyphs[256];
} NkFont;

typedef struct NkTimbre {
    NkU8 id;
    NkU8 opl[13];
    int present;
} NkTimbre;

typedef struct NkAssets {
    char root[1024];
    NkScene title;
    NkScene select;
    NkScene stage;
    NkCharacter characters[NK_CHARACTER_COUNT];
    NkCharacter head;
    NkFont fonts[3];
    NkClip title_clips[NK_TITLE_CLIPS];
    int title_clip_count;
    NkClip select_clips[NK_SELECT_CLIPS];
    int select_clip_count;
    NkClip global_clips[NK_GLOBAL_CLIPS];
    int global_clip_count;
    int global_complete;
    NkTimbre timbres[NK_TIMBRE_COUNT];
    int timbre_count;
} NkAssets;

NkU16 nk_read_u16(const void *ptr);
NkU32 nk_read_u32(const void *ptr);
NkU16 nk_read_be16(const void *ptr);
NkU32 nk_read_be32(const void *ptr);
int nk_ascii_casecmp(const char *a, const char *b);
int nk_path_join(char *out, size_t out_size, const char *a, const char *b);
int nk_file_load(const char *path, NkBlob *blob);
void nk_blob_free(NkBlob *blob);
int nk_find_file(const char *directory, const char *name,
                 char *out, size_t out_size);
unsigned long nk_rand(unsigned long *state);
int nk_clamp_int(int value, int minimum, int maximum);
double nk_clamp_double(double value, double minimum, double maximum);
void nk_set_error(char *error, size_t error_size, const char *message);

void nk_palette_load(NkPalette *palette, const NkU8 *vga_rgb);
int nk_sprite_decode(const NkU8 *data, size_t size, NkSprite *sprite);
void nk_sprite_free(NkSprite *sprite);
void nk_scene_free(NkScene *scene);
void nk_character_free(NkCharacter *character);
void nk_font_free(NkFont *font);
void nk_clip_free(NkClip *clip);
void nk_assets_free(NkAssets *assets);
int nk_assets_load(NkAssets *assets, const char *root,
                   char *error, size_t error_size);
int nk_assets_verify(const char *root, FILE *out);
int nk_cinematic_extract_frame(const char *root, int ending_number,
                               NkPalette *palette, NkSprite *frame,
                               NkClip *clip, char *error, size_t error_size);

#endif
