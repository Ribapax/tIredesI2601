#define _POSIX_C_SOURCE 200809L

#include "app/file_transfer.h"

#include "net/frame.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct file_mapping {
    char cell;
    uint8_t frame_type;
    uint8_t file_id;
    const char *file_name;
    const char *source_path;
};

static const struct file_mapping file_mappings[] = {
    {'1', FRAME_TYPE_FILE_TXT, 1U, "1.txt", "resources/files/1.txt"},
    {'2', FRAME_TYPE_FILE_TXT, 2U, "2.txt", "resources/files/2.txt"},
    {'3', FRAME_TYPE_FILE_JPG, 3U, "3.jpg", "resources/files/3.jpg"},
    {'4', FRAME_TYPE_FILE_JPG, 4U, "4.jpg", "resources/files/4.jpg"},
    {'5', FRAME_TYPE_FILE_MP4, 5U, "5.mp4", "resources/files/5.mp4"},
    {'6', FRAME_TYPE_FILE_MP4, 6U, "6.mp4", "resources/files/6.mp4"},
    {'R', FRAME_TYPE_FILE_TXT, 7U, "ghost.txt", "resources/files/ghost.txt"},
    {'B', FRAME_TYPE_FILE_TXT, 7U, "ghost.txt", "resources/files/ghost.txt"},
    {'G', FRAME_TYPE_FILE_TXT, 7U, "ghost.txt", "resources/files/ghost.txt"},
    {'Y', FRAME_TYPE_FILE_TXT, 7U, "ghost.txt", "resources/files/ghost.txt"}
};

static game_file_transfer_opener current_opener;
static void *current_opener_data;
static int opener_enabled = 1;

static int default_file_opener(const char *path, void *user_data)
{
    char command[GAME_FILE_TRANSFER_MAX_PATH_LEN + 64U];

    (void)user_data;
    if (path == NULL || strchr(path, '\'') != NULL) {
        return -1;
    }

    if (snprintf(command, sizeof(command),
                 "xdg-open '%s' >/dev/null 2>&1 &", path) >=
        (int)sizeof(command)) {
        return -1;
    }

    return system(command) == -1 ? -1 : 0;
}

static void ensure_default_opener_initialized(void)
{
    if (current_opener == NULL && opener_enabled) {
        current_opener = default_file_opener;
        current_opener_data = NULL;
    }
}

static void copy_mapping(const struct file_mapping *mapping,
                         struct game_file_transfer_metadata *metadata)
{
    metadata->cell = mapping->cell;
    metadata->frame_type = mapping->frame_type;
    metadata->file_id = mapping->file_id;
    metadata->file_name = mapping->file_name;
    metadata->source_path = mapping->source_path;
}

static int ensure_output_dir(const char *dir)
{
    struct stat info;

    if (dir == NULL || dir[0] == '\0') {
        return -1;
    }
    if (mkdir(dir, 0775) != 0 && errno != EEXIST) {
        return -1;
    }
    if (stat(dir, &info) != 0 || !S_ISDIR(info.st_mode)) {
        return -1;
    }

    return 0;
}

int game_file_transfer_metadata_for_cell(
    char cell, struct game_file_transfer_metadata *metadata)
{
    if (metadata == NULL) {
        return -1;
    }

    for (size_t i = 0U; i < sizeof(file_mappings) / sizeof(file_mappings[0]);
         i++) {
        if (file_mappings[i].cell == cell) {
            copy_mapping(&file_mappings[i], metadata);
            return 0;
        }
    }

    return -1;
}

int game_file_transfer_metadata_for_message(
    uint8_t frame_type, uint8_t file_id,
    struct game_file_transfer_metadata *metadata)
{
    if (metadata == NULL) {
        return -1;
    }

    for (size_t i = 0U; i < sizeof(file_mappings) / sizeof(file_mappings[0]);
         i++) {
        if (file_mappings[i].frame_type == frame_type &&
            file_mappings[i].file_id == file_id) {
            copy_mapping(&file_mappings[i], metadata);
            return 0;
        }
    }

    return -1;
}

int game_file_transfer_output_path(
    const struct game_file_transfer_metadata *metadata,
    const char *output_dir, char *out, size_t out_len)
{
    const char *dir = output_dir == NULL ?
                      GAME_FILE_TRANSFER_DEFAULT_OUTPUT_DIR : output_dir;
    int written;

    if (metadata == NULL || metadata->file_name == NULL ||
        metadata->file_name[0] == '\0' || out == NULL || out_len == 0U) {
        return -1;
    }

    written = snprintf(out, out_len, "%s/%s", dir, metadata->file_name);
    if (written < 0 || (size_t)written >= out_len) {
        return -1;
    }

    return 0;
}

void game_file_transfer_set_opener(game_file_transfer_opener opener,
                                   void *user_data)
{
    current_opener = opener;
    current_opener_data = user_data;
    opener_enabled = opener != NULL;
}

void game_file_transfer_reset_opener(void)
{
    current_opener = default_file_opener;
    current_opener_data = NULL;
    opener_enabled = 1;
}

int game_file_transfer_open_received_file(
    const struct game_file_transfer_metadata *metadata,
    const char *output_dir, char *path_out, size_t path_out_len,
    FILE **file_out)
{
    const char *dir = output_dir == NULL ?
                      GAME_FILE_TRANSFER_DEFAULT_OUTPUT_DIR : output_dir;

    if (path_out == NULL || path_out_len == 0U || file_out == NULL) {
        return -1;
    }
    *file_out = NULL;

    if (ensure_output_dir(dir) != 0 ||
        game_file_transfer_output_path(metadata, dir, path_out,
                                       path_out_len) != 0) {
        return -1;
    }

    *file_out = fopen(path_out, "wb");
    return *file_out == NULL ? -1 : 0;
}

int game_file_transfer_open_received_path(const char *path)
{
    ensure_default_opener_initialized();
    if (!opener_enabled) {
        return 0;
    }
    if (current_opener == NULL) {
        return -1;
    }

    return current_opener(path, current_opener_data);
}

int game_file_transfer_store_and_open_received(
    const struct game_file_transfer_metadata *metadata,
    const uint8_t *data, size_t data_len,
    const char *output_dir, char *path_out, size_t path_out_len)
{
    FILE *file;
    char local_path[GAME_FILE_TRANSFER_MAX_PATH_LEN];
    char *path = path_out == NULL ? local_path : path_out;
    size_t path_len = path_out == NULL ? sizeof(local_path) : path_out_len;

    if (metadata == NULL || data == NULL || data_len == 0U) {
        return -1;
    }

    if (game_file_transfer_open_received_file(metadata, output_dir, path,
                                              path_len, &file) != 0) {
        return -1;
    }
    if (fwrite(data, 1U, data_len, file) != data_len) {
        (void)fclose(file);
        return -1;
    }
    if (fclose(file) != 0) {
        return -1;
    }

    return game_file_transfer_open_received_path(path);
}
