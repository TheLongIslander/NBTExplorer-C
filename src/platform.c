#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <wchar.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

static void set_err(char* err, size_t err_sz, const char* message) {
    if (err && err_sz > 0) {
        snprintf(err, err_sz, "%s", message);
    }
}

char* nbt_strdup(const char* text) {
    size_t len;
    char* copy;

    if (!text) return NULL;
    len = strlen(text);
    copy = malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, text, len + 1);
    return copy;
}

#ifdef _WIN32
static wchar_t* utf8_to_wide(const char* text) {
    wchar_t* wide;
    int count;

    if (!text) {
        errno = EINVAL;
        return NULL;
    }
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (count <= 0) {
        errno = EINVAL;
        return NULL;
    }
    wide = malloc((size_t)count * sizeof(*wide));
    if (!wide) {
        errno = ENOMEM;
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, count) <= 0) {
        free(wide);
        errno = EINVAL;
        return NULL;
    }
    return wide;
}

static char* wide_to_utf8(const wchar_t* text) {
    char* utf8;
    int count;

    if (!text) {
        errno = EINVAL;
        return NULL;
    }
    count = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (count <= 0) {
        errno = EINVAL;
        return NULL;
    }
    utf8 = malloc((size_t)count);
    if (!utf8) {
        errno = ENOMEM;
        return NULL;
    }
    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, count, NULL, NULL) <= 0) {
        free(utf8);
        errno = EINVAL;
        return NULL;
    }
    return utf8;
}
#endif

FILE* nbt_fopen(const char* path, const char* mode) {
#ifdef _WIN32
    wchar_t* wide_path;
    wchar_t* wide_mode;
    FILE* file;

    wide_path = utf8_to_wide(path);
    if (!wide_path) return NULL;
    wide_mode = utf8_to_wide(mode);
    if (!wide_mode) {
        free(wide_path);
        return NULL;
    }
    file = _wfopen(wide_path, wide_mode);
    free(wide_mode);
    free(wide_path);
    return file;
#else
    return fopen(path, mode);
#endif
}

static const char* last_path_separator(const char* path) {
    const char* slash = strrchr(path, '/');
#ifdef _WIN32
    const char* backslash = strrchr(path, '\\');

    if (!slash) return backslash;
    if (!backslash) return slash;
    return slash > backslash ? slash : backslash;
#else
    return slash;
#endif
}

#ifndef _WIN32
static char* make_temp_template(const char* target_path, const char* prefix) {
    const char* separator;
    size_t directory_len;
    size_t prefix_len;
    size_t total_len;
    char* path;

    if (!target_path || !prefix || prefix[0] == '\0') return NULL;

    separator = last_path_separator(target_path);
    directory_len = separator ? (size_t)(separator - target_path + 1) : 0;
    prefix_len = strlen(prefix);

    if (directory_len > SIZE_MAX - prefix_len - 10) return NULL;
    total_len = directory_len + prefix_len + 9; /* '.', '_', XXXXXX, NUL */
    path = malloc(total_len);
    if (!path) return NULL;

    if (directory_len > 0) {
        memcpy(path, target_path, directory_len);
    }
    snprintf(path + directory_len, total_len - directory_len, ".%s_XXXXXX", prefix);
    return path;
}
#endif

#ifdef _WIN32
static char* target_directory(const char* target_path) {
    const char* separator = last_path_separator(target_path);
    size_t length;
    char* directory;

    if (!separator) return nbt_strdup(".");
    length = (size_t)(separator - target_path + 1);
    directory = malloc(length + 1);
    if (!directory) return NULL;
    memcpy(directory, target_path, length);
    directory[length] = '\0';
    return directory;
}

static void set_windows_err(char* err, size_t err_sz, const char* operation, DWORD code) {
    if (err && err_sz > 0) {
        snprintf(err, err_sz, "%s failed (Windows error %lu)", operation, (unsigned long)code);
    }
}
#endif

int nbt_open_temp_file(
    const char* target_path,
    const char* prefix,
    char** out_path,
    char* err,
    size_t err_sz
) {
    if (!target_path || !prefix || !out_path) {
        set_err(err, err_sz, "invalid temporary-file arguments");
        return -1;
    }
    *out_path = NULL;

#ifdef _WIN32
    {
        char* directory = target_directory(target_path);
        wchar_t* wide_directory;
        wchar_t temp_path[MAX_PATH + 1];
        wchar_t short_prefix[4] = {L'N', L'B', L'T', L'\0'};
        UINT result;
        HANDLE handle;
        int fd;

        if (!directory) {
            set_err(err, err_sz, "out of memory");
            return -1;
        }
        wide_directory = utf8_to_wide(directory);
        free(directory);
        if (!wide_directory) {
            set_err(err, err_sz, "target directory is not valid UTF-8");
            return -1;
        }
        if (wcslen(wide_directory) >= MAX_PATH - 14) {
            free(wide_directory);
            set_err(err, err_sz, "target directory path is too long for a Windows temporary file");
            return -1;
        }
        for (size_t i = 0; i < 3 && prefix[i] != '\0'; i++) {
            unsigned char c = (unsigned char)prefix[i];
            short_prefix[i] = c < 0x80 ? (wchar_t)c : L'N';
        }

        result = GetTempFileNameW(wide_directory, short_prefix, 0, temp_path);
        free(wide_directory);
        if (result == 0) {
            set_windows_err(err, err_sz, "GetTempFileName", GetLastError());
            return -1;
        }

        handle = CreateFileW(
            temp_path,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        if (handle == INVALID_HANDLE_VALUE) {
            DWORD code = GetLastError();
            DeleteFileW(temp_path);
            set_windows_err(err, err_sz, "CreateFile", code);
            return -1;
        }

        fd = _open_osfhandle((intptr_t)handle, _O_RDWR | _O_BINARY);
        if (fd < 0) {
            CloseHandle(handle);
            DeleteFileW(temp_path);
            set_err(err, err_sz, "failed to attach a CRT descriptor to the temporary file");
            return -1;
        }

        *out_path = wide_to_utf8(temp_path);
        if (!*out_path) {
            _close(fd);
            DeleteFileW(temp_path);
            set_err(err, err_sz, "out of memory while encoding the temporary path");
            return -1;
        }
        return fd;
    }
#else
    {
        char* temp_path = make_temp_template(target_path, prefix);
        int fd;

        if (!temp_path) {
            set_err(err, err_sz, "out of memory");
            return -1;
        }

        fd = mkstemp(temp_path);
        if (fd < 0) {
            if (err && err_sz > 0) {
                snprintf(err, err_sz, "mkstemp(%s) failed: %s", temp_path, strerror(errno));
            }
            free(temp_path);
            return -1;
        }

        *out_path = temp_path;
        return fd;
    }
#endif
}

int nbt_close_fd(int fd) {
#ifdef _WIN32
    return _close(fd);
#else
    return close(fd);
#endif
}

int nbt_remove_file(const char* path) {
#ifdef _WIN32
    wchar_t* wide_path = utf8_to_wide(path);
    int result;

    if (!wide_path) return -1;
    result = _wremove(wide_path);
    free(wide_path);
    return result;
#else
    return remove(path);
#endif
}

int nbt_replace_file(const char* temp_path, const char* target_path, char* err, size_t err_sz) {
    if (!temp_path || !target_path) {
        set_err(err, err_sz, "invalid file-replacement arguments");
        return 0;
    }

#ifdef _WIN32
    {
        wchar_t* wide_temp_path = utf8_to_wide(temp_path);
        wchar_t* wide_target_path = utf8_to_wide(target_path);
        DWORD code;

        if (!wide_temp_path || !wide_target_path) {
            free(wide_temp_path);
            free(wide_target_path);
            set_err(err, err_sz, "file path is not valid UTF-8");
            return 0;
        }
        if (MoveFileExW(
                wide_temp_path,
                wide_target_path,
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            free(wide_temp_path);
            free(wide_target_path);
            return 1;
        }
        code = GetLastError();
        free(wide_temp_path);
        free(wide_target_path);
        set_windows_err(err, err_sz, "MoveFileEx", code);
        return 0;
    }
#else
    if (rename(temp_path, target_path) != 0) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "rename(%s -> %s) failed: %s", temp_path, target_path, strerror(errno));
        }
        return 0;
    }
    return 1;
#endif
}

int nbt_dup_fd(int fd) {
#ifdef _WIN32
    return _dup(fd);
#else
    return dup(fd);
#endif
}

int nbt_dup2_fd(int source_fd, int destination_fd) {
#ifdef _WIN32
    return _dup2(source_fd, destination_fd);
#else
    return dup2(source_fd, destination_fd);
#endif
}

int nbt_fileno(FILE* stream) {
#ifdef _WIN32
    return _fileno(stream);
#else
    return fileno(stream);
#endif
}
