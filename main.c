#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <linux/limits.h>
#include <sys/stat.h>
#include <errno.h>

#include "debug.h"
#include "ani.h"
#include "string_builder.h"

enum OutFormat { Json, Plain, Silent };

enum Mode { Extract, Describe };

// for logger
char debug_mode = 0;

// Options
typedef struct {
    enum Mode mode;
    enum OutFormat out_format;
    unsigned task_num;
    const char **tasks;
    const char prefix[PATH_MAX];
} GlobalContext;

typedef struct {
    float time_ms;
    void *buf;
    size_t buf_size;
} IconInfo;

typedef struct {
    unsigned count;
    uint32_t cx;
    uint32_t cy;
    uint32_t hotx;
    uint32_t hoty;
    uint32_t jif_rate;
    char has_rate;  // has rate chunk
    IconInfo *icons;
} CursorData;

static void collect_chunk_info(const Chunk *chunk, void *data) {
    CursorData *d = (CursorData *)data;
    switch (chunk->ty) {
        case ty_anih: {
            ChunkAnih *inner = chunk->inner;
            d->count = inner->cFrames;
            assert(inner->cFrames == inner->cSteps);
            d->cx = inner->cx;
            d->cy = inner->cy;
            d->jif_rate = inner->jifRate;
            d->icons = malloc(d->count * sizeof(IconInfo));
            if (!d->icons) {
                err("OOM in function `%s`, line `%d`", __PRETTY_FUNCTION__, __LINE__);
            }
            break;
        }
        case ty_rate: {
            d->has_rate = 1;
            if (d->icons) {
                ChunkRate *inner = chunk->inner;
                assert(d->count == inner->count);
                for (unsigned i = 0; i < inner->count; ++i) {
                    float duration = inner->jiffies[i] == 0 ? d->jif_rate * 1000.0 / 60.0
                                                            : inner->jiffies[i] * 1000.0 / 60.0;
                    d->icons[i].time_ms = duration;
                }
            }
            break;
        }
        case ty_seq: {
            break;
        }
        case ty_list: {
            if (d->icons) {
                ChunkList *inner = chunk->inner;
                assert(inner->count == d->count);
                for (unsigned i = 0; i < inner->count; ++i) {
                    d->icons[i].buf = inner->frames[i]->buffer;
                    d->icons[i].buf_size = inner->frames[i]->size;
                }
                d->hotx = inner->hotx;
                d->hoty = inner->hoty;
            }
            break;
        }
        default: assert(0);
    }
    if (!d->has_rate) {
        for (unsigned i = 0; i < d->count; ++i) {
            d->icons[i].time_ms = d->jif_rate * 1000.0 / 60.0;
        }
    }
}

// Helper function to create a directory and its parents if they don't exist
static int create_dir_recursive(char *dir) {
#define MKDIR(path) mkdir(path, 0755)
    char *p = dir;
    char tmp[256];  // Adjust size as needed
    size_t len;

    while (*p) {
        p++;
        if (*p == '/' || *p == '\\' || *p == '\0') {
            len = p - dir;
            if (len == 0)
                continue;
            memcpy(tmp, dir, len);
            tmp[len] = '\0';

            if (MKDIR(tmp) == 0) {
                // Directory created successfully
            } else if (errno != EEXIST) {
                fprintf(stderr, "Failed to create directory '%s': %s\n", tmp, strerror(errno));
                return -1;
            }
        }
    }
    return 0;
#undef MKDIR
}

static void write_file(const char *path, const void *buf, size_t n) {
    char *dir_path = strdup(path);
    if (!dir_path) {
        err("OOM in function `%s`, line `%d`", __PRETTY_FUNCTION__, __LINE__);
        return;
    }

    // Find the last slash to get the directory path
    char *last_slash = strrchr(dir_path, '/');
    if (!last_slash) {
        last_slash = strrchr(dir_path, '\\');
    }

    if (last_slash) {
        *last_slash = '\0';  // Null-terminate to get the directory path
        if (create_dir_recursive(dir_path) != 0) {
            free(dir_path);
            return;
        }
    }

    free(dir_path);

    FILE *out = fopen(path, "wb");
    if (!out) {
        err("Failed to open %s: %s\n", path, strerror(errno));
        return;
    }

    // Check if fwrite successfully wrote all n bytes
    if (fwrite(buf, 1, n, out) != n) {
        err("Failed to write all bytes to %s\n", path);
    }

    fclose(out);
}

const static char *basename(const char *name) {
    const char *basename = strlen(name) + name;
    while (basename != name && *(basename - 1) != '/') {
        --basename;
    }
    return basename;
}

static int emit_info(const GlobalContext *ctx, const CursorData *data, const char *filename) {
    // Dump content(json)
    // {
    //   "name": xxx.ani,
    //   "width": cx,
    //   "height": cy,
    //   "hotx": hotx,
    //   "hoty": hoty
    //   "frames": [
    //      {
    //        "path": "path/to/frame01.ico",
    //        "duration": time_ms
    //      },
    //      {
    //        "path": "path/to/frame02.ico",
    //        "duration": time_ms
    //      },
    //      {
    //        "path": "path/to/frame03.ico",
    //        "duration": time_ms
    //      }
    //   ]
    // }
    const char *realname = basename(filename);
    switch (ctx->out_format) {
        case Json: {
            StringBuilder *json = sb_new();
            if (!json) {
                err("OOM in function `%s`, line `%d`", __PRETTY_FUNCTION__, __LINE__);
                return 1;
            }
            sb_appendf(json, "{\"name\": \"%s\",", realname);
            sb_appendf(json, "\"width\": %u,", data->cx);
            sb_appendf(json, "\"height\": %u,", data->cy);
            sb_appendf(json, "\"hotx\": %u,", data->hotx);
            sb_appendf(json, "\"hoty\": %u,", data->hoty);
            sb_appendf(json, "\"jif_rate\": %u,", data->jif_rate);
            sb_appendf(json, "\"frames\": [");
            if (data->count >= 1 && data->icons) {
                for (unsigned i = 0; i < data->count - 1; ++i) {
                    StringBuilder *path_buf = sb_new();
                    if (!path_buf) {
                        err("OOM in function `%s`, line `%d`", __PRETTY_FUNCTION__, __LINE__);
                        return 1;
                    }
                    sb_appendf(path_buf, "%s/%s/frame-%03d.ico", ctx->prefix, realname, i);
                    if (ctx->mode == Extract) {
                        write_file(path_buf->data, data->icons[i].buf, data->icons[i].buf_size);
                        debug("Writing to file `%s`", path_buf->data);
                    }

                    sb_appendf(json, "{\"path\": \"%s\",", path_buf->data);
                    sb_appendf(json, "\"duration\": %.3f},", data->icons[i].time_ms);

                    sb_cleanup(path_buf);
                }

                StringBuilder *path_buf = sb_new();
                if (!path_buf) {
                    err("OOM in function `%s`, line `%d`", __PRETTY_FUNCTION__, __LINE__);
                    return 1;
                }
                sb_appendf(path_buf,
                           "%s/%s/frame-%03d.ico",
                           ctx->prefix,
                           realname,
                           data->count - 1);
                if (ctx->mode == Extract) {
                    write_file(path_buf->data,
                               data->icons[data->count - 1].buf,
                               data->icons[data->count - 1].buf_size);
                    debug("Writing to file `%s`", path_buf->data);
                }

                sb_appendf(json, "{\"path\": \"%s\",", path_buf->data);
                sb_appendf(json, "\"duration\": %.3lf}", data->icons[data->count - 1].time_ms);

                sb_cleanup(path_buf);
            }
            sb_appendf(json, "]}");
            puts(json->data);
            sb_cleanup(json);
            return 0;
        }
        case Plain: {
            StringBuilder *text = sb_new();
            if (!text) {
                err("OOM in function `%s`, line `%d`", __PRETTY_FUNCTION__, __LINE__);
                return 1;
            }
            sb_appendf(text, "Name: %s\n", realname);
            sb_appendf(text, "Width: %u\n", data->cx);
            sb_appendf(text, "Height: %u\n", data->cy);
            sb_appendf(text, "HotX: %u\n", data->hotx);
            sb_appendf(text, "HotY: %u\n", data->hoty);
            sb_appendf(text, "JifRate: %u\n", data->jif_rate);
            sb_appendf(text, "Frames:\n");
            if (data->count >= 1 && data->icons) {
                for (unsigned i = 0; i < data->count; ++i) {
                    StringBuilder *path_buf = sb_new();
                    if (!path_buf) {
                        err("OOM in function `%s`, line `%d`", __PRETTY_FUNCTION__, __LINE__);
                        return 1;
                    }
                    sb_appendf(text, "  Frame%3d\n", i);
                    sb_appendf(path_buf, "%s/%s/frame-%03d.ico", ctx->prefix, realname, i);
                    if (ctx->mode == Extract) {
                        write_file(path_buf->data, data->icons[i].buf, data->icons[i].buf_size);
                        debug("Writing to file `%s`", path_buf->data);
                    }

                    sb_appendf(text, "    Output file: %s\n", path_buf->data);
                    sb_appendf(text, "    Duration: %.3f\n", data->icons[i].time_ms);

                    sb_cleanup(path_buf);
                }
            }
            puts(text->data);
            sb_cleanup(text);
            return 0;
        }
        case Silent: {
            if (ctx->mode == Extract) {
                warn("Begin to extract `%s`", filename);
                for (unsigned i = 0; i < data->count; ++i) {
                    StringBuilder *path_buf = sb_new();
                    if (!path_buf) {
                        err("OOM in function `%s`, line `%d`", __PRETTY_FUNCTION__, __LINE__);
                        return 1;
                    }
                    sb_appendf(path_buf, "%s/%s/frame-%03d.ico", ctx->prefix, realname, i);
                    debug("Writing to file `%s`", path_buf->data);
                    sb_cleanup(path_buf);
                }
            }
            return 0;
        }
        default: assert(0);
    }
}

static int run_task(const GlobalContext *ctx) {
    if (!ctx->task_num) {
        return 1;
    }
    int ok = 0;
    for (unsigned i = 0; i < ctx->task_num; ++i) {
        const char *path = ctx->tasks[i];
        FILE *target = fopen(path, "rb");
        if (!target) {
            err("Cannot open file `%s`", path);
            ok = 2;
            continue;
        }
        AniFile *ani = parse_ani(target);
        debug("Finish parsing `%s`", path);
        WalkContext walk_ctx;
        walk_ctx.ani = ani;
        walk_ctx.visit_chunk = &collect_chunk_info;
        walk_ctx.visit_frame = NULL;
        CursorData data;
        data.count = 0;
        data.icons = NULL;
        data.cx = 0;
        data.cy = 0;
        data.hotx = 0;
        data.hoty = 0;
        data.has_rate = 0;
        walk_ctx.data = &data;
        walk(&walk_ctx);
        debug("Finish collecting info of `%s`", path);
        ok = emit_info(ctx, &data, path);
        if (data.icons) {
            free(data.icons);
        } else {
            err("Cannot visit ani info");
        }
        cleanup_ani(ani);
        fclose(target);
    }
    return ok;
}

static void print_help(const char *prog_name) {
    printf("Describe or extract *.ani files\n");
    printf("Usage: %s <options> files\n", prog_name);
    printf("Options:\n");
    printf("-debug      Display full log\n");
    printf("-json       Display information as json\n");
    printf("-silent     Donnot display information\n");
    printf("-extract    Do the extract job\n");
    printf("-o          Assign output rootdir\n");
    printf("-h          Show help menu\n");
}

static void cleanup_global_ctx(GlobalContext *ctx) {
    if (ctx) {
        if (ctx->tasks) {
            free(ctx->tasks);
        }
        free(ctx);
    }
}

static GlobalContext *parse_args(int argc, char **argv) {
    char print_help_and_exit = 0;
    int i = 1;
    GlobalContext *ctx = malloc(sizeof(GlobalContext));
    if (!ctx) {
        err("Mem error when parsing args");
        return NULL;
    }
    ctx->mode = Describe;
    ctx->out_format = Plain;
    ctx->task_num = 0;
    unsigned capacity = 4;
    ctx->tasks = malloc(capacity * sizeof(char *));
    if (!ctx->tasks) {
        cleanup_global_ctx(ctx);
        err("OOM in function `%s`, line `%d`", __PRETTY_FUNCTION__, __LINE__);
        return NULL;
    }
    if (!getcwd((char *)ctx->prefix, PATH_MAX)) {
        err("Cannot get cwd as prefix");
        cleanup_global_ctx(ctx);
        return NULL;
    }
    while (i < argc) {
#define is_arg(ARG) !strcmp(argv[i], ARG)
        if (is_arg("-h")) {
            print_help_and_exit = 1;
        } else if (is_arg("-debug")) {
            debug_mode = 1;
        } else if (is_arg("-json")) {
            ctx->out_format = Json;
        } else if (is_arg("-silent")) {
            ctx->out_format = Silent;
        } else if (is_arg("-extract")) {
            ctx->mode = Extract;
        } else if (is_arg("-o")) {
            if (i + 1 >= argc || *argv[i + 1] == '-') {
                warn("No path is assigned after '-o'");
            } else {
                strcpy((char *)ctx->prefix, argv[i + 1]);
                ++i;
            }
        } else if (*argv[i] == '-') {
            warn("Not an option: `%s`", argv[i]);
        } else {
            // push a task
            if (ctx->task_num + 1 > capacity) {
                capacity <<= 1;
                const char **tmp = realloc(ctx->tasks, capacity * sizeof(char *));
                if (!tmp) {
                    cleanup_global_ctx(ctx);
                    err("OOM in function `%s`, line `%d`", __PRETTY_FUNCTION__, __LINE__);
                    return NULL;
                }
                ctx->tasks = tmp;
            }
            ctx->tasks[ctx->task_num++] = argv[i];
        }
#undef is_arg
        ++i;
    }
    if (debug_mode) {
        debug("Output format: %s",
              ctx->out_format == Json    ? "Json"
              : ctx->out_format == Plain ? "Plain"
                                         : "Silent");
        debug("Mode: %s", ctx->mode == Extract ? "Extract" : "Describe");
        debug("Prefix: %s", ctx->prefix);
        if (!ctx->task_num) {
            warn("No file to convert");
        } else {
            debug("Tasks:");
            for (int i = 0; i < ctx->task_num; ++i) {
                debug("Task%d: `%s`", i, ctx->tasks[i]);
            }
        }
    }
    if (print_help_and_exit) {
        print_help(*argv);
        cleanup_global_ctx(ctx);
        return NULL;
    }
    return ctx;
}

int main(int argc, char **argv) {
    if (argc <= 1) {
        print_help(argv[0]);
    }
    GlobalContext *ctx = parse_args(argc, argv);
    if (ctx == NULL) {
        warn("Cannot parse args");
        return 1;
    }
    int res = run_task(ctx);
    cleanup_global_ctx(ctx);
    return res;
}
