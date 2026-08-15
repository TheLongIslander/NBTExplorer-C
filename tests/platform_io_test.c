#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

static int write_text(const char* path, const char* text) {
    FILE* file = nbt_fopen(path, "wb");
    size_t length = strlen(text);
    int ok;

    if (!file) return 0;
    ok = fwrite(text, 1, length, file) == length;
    if (fclose(file) != 0) ok = 0;
    return ok;
}

int main(int argc, char** argv) {
    const char* directory;
    const char* separator;
    char target_path[2048];
    char error[512] = {0};
    char* temp_path = NULL;
    char content[16] = {0};
    FILE* file;
    int fd;
    int written;

    if (argc != 2) {
        fprintf(stderr, "expected a writable test directory\n");
        return 1;
    }

    directory = argv[1];
    separator = (directory[0] != '\0' &&
                 (directory[strlen(directory) - 1] == '/' ||
                  directory[strlen(directory) - 1] == '\\')) ? "" : "/";
    written = snprintf(
        target_path,
        sizeof(target_path),
        "%s%splatform-replace-\xE6\xB5\x8B\xE8\xAF\x95-\xF0\x9F\x8C\x8D.tmp",
        directory,
        separator
    );
    if (written < 0 || (size_t)written >= sizeof(target_path)) {
        fprintf(stderr, "test path is too long\n");
        return 1;
    }

    nbt_remove_file(target_path);
    if (!write_text(target_path, "old")) {
        fprintf(stderr, "could not create target file\n");
        return 1;
    }

    fd = nbt_open_temp_file(target_path, "test", &temp_path, error, sizeof(error));
    if (fd < 0 || !temp_path) {
        fprintf(stderr, "could not create neighboring temporary file: %s\n", error);
        nbt_remove_file(target_path);
        return 1;
    }
    if (nbt_close_fd(fd) != 0 || !write_text(temp_path, "new")) {
        fprintf(stderr, "could not write temporary file\n");
        nbt_remove_file(temp_path);
        nbt_remove_file(target_path);
        free(temp_path);
        return 1;
    }

    if (!nbt_replace_file(temp_path, target_path, error, sizeof(error))) {
        fprintf(stderr, "could not replace existing target: %s\n", error);
        nbt_remove_file(temp_path);
        nbt_remove_file(target_path);
        free(temp_path);
        return 1;
    }
    free(temp_path);

    file = nbt_fopen(target_path, "rb");
    if (!file || fread(content, 1, 3, file) != 3 || memcmp(content, "new", 3) != 0) {
        fprintf(stderr, "replacement file did not contain the expected data\n");
        if (file) fclose(file);
        nbt_remove_file(target_path);
        return 1;
    }
    if (fclose(file) != 0) {
        nbt_remove_file(target_path);
        return 1;
    }

    nbt_remove_file(target_path);
    return 0;
}
