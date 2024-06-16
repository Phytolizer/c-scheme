#include <ctype.h>
#include <s7.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef struct {
#ifdef _WIN32
    HANDLE handle;
#else
    int fd;
#endif
    int valid;
} platform_file;

static platform_file platform_fopen(char const* path) {
#ifdef _WIN32
    platform_file result;
    result.handle = CreateFileA(
            path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, NULL
    );
    result.valid = result.handle != INVALID_HANDLE_VALUE;
    return result;
#else
    platform_file result;
    result.fd = open(path, O_RDONLY);
    result.valid = result.fd != -1;
    return result;
#endif
}

static void platform_fclose(platform_file file) {
    if (file.valid) {
#ifdef _WIN32
        CloseHandle(file.handle);
#else
        close(file.fd);
#endif
    }
}

static size_t get_filelen(platform_file f) {
#ifdef _WIN32
    LARGE_INTEGER size;
    GetFileSizeEx(f.handle, &size);
    return (size_t) size.QuadPart;
#else
    struct stat st;
    fstat(f.fd, &st);
    return (size_t) st.st_size;
#endif
}

static size_t platform_fread(platform_file f, char* buf, size_t len) {
#ifdef _WIN32
    DWORD read;
    ReadFile(f.handle, buf, (DWORD) len, &read, NULL);
    return (size_t) read;
#else
    return (size_t) read(f.fd, buf, len);
#endif
}

static int slurp_file(
        char const* path, char** out_buf, size_t* out_len,
        char const** out_error
) {
    size_t len;
    size_t nread;
    char* buf;
    platform_file f;

    f = platform_fopen(path);
    if (!f.valid) {
        *out_error = "failed to open file";
        return 0;
    }

    len = get_filelen(f);

    buf = malloc(len + 1);
    if (buf == NULL) {
        *out_error = "failed to allocate buffer";
        platform_fclose(f);
        return 0;
    }

    nread = platform_fread(f, buf, len);
    if (nread != len) {
        *out_error = "failed to read file";
        platform_fclose(f);
        return 0;
    }

    buf[len] = '\0';
    platform_fclose(f);

    *out_buf = buf;
    *out_len = len;
    return 1;
}

static char const* current_path;
static size_t current_line;
static bool had_s7_error;

static s7_pointer error_handler(s7_scheme* s7, s7_pointer args) {
    fprintf(stderr, "%s:%zu: %s\n", current_path, current_line,
            s7_string(s7_car(args)));
    had_s7_error = true;
    return s7_f(s7);
}

static size_t close_delim(
        char const* input, size_t len, char open, char close, int* out_err
) {
    // input points at the open paren.
    size_t depth = 1;
    for (size_t i = 1; i < len; ++i) {
        if (input[i] == '\n') {
            ++current_line;
            fputc('\n', stdout);
        } else if (input[i] == open) {
            ++depth;
        } else if (input[i] == close) {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    fprintf(stderr, "Unmatched '%c'\n", open);
    *out_err = 1;
    return 0;
}

int expand(s7_scheme* s7, char const* data, size_t len) {
    int ret = 0;
    current_line = 1;
    printf("#line 1 \"%s\"\n", current_path);
    char* temp_buf = NULL;
    size_t temp_len = 0;
    for (size_t i = 0; i < len; ++i) {
        if (data[i] == '$') {
            fflush(stdout);
            ++i;
            if (i >= len) {
                fprintf(stderr, "Unexpected end of input\n");
                ret = 1;
                break;
            }
            if (data[i] != '{' && data[i] != '(') {
                fprintf(stderr,
                        "Expected open brace or open paren after '$'\n");
                ret = 1;
                break;
            }
            char delim = data[i];
            size_t close = close_delim(
                    data + i, len - i, delim, delim == '{' ? '}' : ')', &ret
            );
            if (ret) {
                break;
            }
            if (close + 1 > len || data[i + close + 1] != '$') {
                fprintf(stderr, "Expected '$' after closing brace\n");
                ret = 1;
                break;
            }
            // 2 for (), 1 for NUL
            size_t len = close + 2 + 1;
            if (temp_len < len) {
                temp_buf = realloc(temp_buf, len);
                temp_len = len;
            }
            snprintf(
                    temp_buf, temp_len, "(%.*s)", (int) close - 1, data + i + 1
            );
            s7_pointer eval = s7_eval_c_string(s7, temp_buf);
            if (delim == '(') {
                s7_display(s7, eval, s7_current_output_port(s7));
                s7_flush_output_port(s7, s7_current_output_port(s7));
            }
            i += close + 1;
        } else {
            fputc(data[i], stdout);
        }
    }
    free(temp_buf);
    if (ret == 0 && had_s7_error) {
        ret = 2;
    }
    return ret;
}

int main(int argc, char* argv[]) {
    if (argc <= 1) {
        fprintf(stderr, "Usage: %s <file>\n", argv[0] ?: "scheme-pp");
        return 1;
    }
    int ret = 0;

    s7_scheme* s7 = s7_init();
    s7_define_function(
            s7, "error-handler", error_handler, 1, 0, false, "error handler"
    );
    s7_eval_c_string(
            s7,
            "(set! (hook-functions *error-hook*)"
            "  (list (lambda (hook)"
            "          (error-handler"
            "            (apply format #f (hook 'data)))"
            "          (set! (hook 'result) '$))))"
    );
    for (int i = 1; i < argc; ++i) {
        current_path = argv[i];
        char* data;
        size_t len;
        char const* error;
        if (!slurp_file(current_path, &data, &len, &error)) {
            fprintf(stderr, "Error reading \"%s\": %s\n", current_path, error);
            ret = 1;
            break;
        }

        if (expand(s7, data, len)) {
            ret = 1;
            break;
        }

        free(data);
    }

    s7_free(s7);
    return ret;
}
