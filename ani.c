#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "ani.h"
#include "debug.h"

typedef struct {
    FILE *file;
    size_t csize;
    char eof;
} ParseContext;

// Read exact bytes into buffer
static int read_exact(ParseContext *ctx, void *buf, size_t n) {
    return fread(buf, 1, n, ctx->file) == n;
}

// Read a 32 byte int, little endian
static uint32_t read_u32_le(ParseContext *ctx) {
    uint8_t b[4];
    if (fread(b, 1, 4, ctx->file) != 4)
        return 0xFFFFFFFF;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static int consume_bytes(ParseContext *ctx, size_t count) {
    return fseek(ctx->file, count, SEEK_CUR);
}

static ChunkAnih *parse_anih(ParseContext *ctx) {
    if (ctx->csize < 36) {
        err("anih chunk too small (%u)", ctx->csize);
        consume_bytes(ctx, ctx->csize + (ctx->csize & 1));
    }
    // Consume anih chunk
    uint8_t anih_buf[36];
    if (!read_exact(ctx, anih_buf, 36)) {
        err("read anih fail");
        return NULL;
    }
    ChunkAnih *anih = malloc(sizeof(ChunkAnih));
    if (!anih) {
        err("OOM in function `%s`, line `%d`", __PRETTY_FUNCTION__, __LINE__);
        return NULL;
    }

    // parse as 9 DWORDs little-endian
    anih->cbSize = anih_buf[0] | (anih_buf[1] << 8) | (anih_buf[2] << 16) | (anih_buf[3] << 24);
    anih->cFrames = anih_buf[4] | (anih_buf[5] << 8) | (anih_buf[6] << 16) | (anih_buf[7] << 24);
    anih->cSteps = anih_buf[8] | (anih_buf[9] << 8) | (anih_buf[10] << 16) | (anih_buf[11] << 24);
    anih->cx = anih_buf[12] | (anih_buf[13] << 8) | (anih_buf[14] << 16) | (anih_buf[15] << 24);
    anih->cy = anih_buf[16] | (anih_buf[17] << 8) | (anih_buf[18] << 16) | (anih_buf[19] << 24);
    anih->cBitCount =
        anih_buf[20] | (anih_buf[21] << 8) | (anih_buf[22] << 16) | (anih_buf[23] << 24);
    anih->cPlanes =
        anih_buf[24] | (anih_buf[25] << 8) | (anih_buf[26] << 16) | (anih_buf[27] << 24);
    anih->jifRate =
        anih_buf[28] | (anih_buf[29] << 8) | (anih_buf[30] << 16) | (anih_buf[31] << 24);
    anih->flags = anih_buf[32] | (anih_buf[33] << 8) | (anih_buf[34] << 16) | (anih_buf[35] << 24);
    info(
        " anih: cbSize=%u cFrames=%u cSteps=%u cx=%u cy=%u bitCount=%u planes=%u jifRate=%u flags=0x%08x",
        anih->cbSize,
        anih->cFrames,
        anih->cSteps,
        anih->cx,
        anih->cy,
        anih->cBitCount,
        anih->cPlanes,
        anih->jifRate,
        anih->flags);
    // skip remaining of anih chunk if any
    if (ctx->csize > 36) {
        consume_bytes(ctx, ctx->csize - 36);
    }
    if (ctx->csize & 1) {
        consume_bytes(ctx, 1);
    }
    return anih;
}

static void cleanup_anih(ChunkAnih *c) {
    if (!c) {
        return;
    }
    free(c);
}

static ChunkSeq *parse_seq(ParseContext *ctx) {
    ChunkSeq *seq = malloc(sizeof(ChunkSeq));
    if (!seq) {
        err("OOM in function `%s`, line `%d`", __PRETTY_FUNCTION__, __LINE__);
        return NULL;
    }
    seq->count = ctx->csize / 4;
    seq->indexes = malloc(seq->count * sizeof(unsigned));
    if (!seq->indexes) {
        err("OOM in function `%s`, line `%d`", __PRETTY_FUNCTION__, __LINE__);
        return NULL;
    }
    info(" seq: %u entries", seq->count);
    for (uint32_t i = 0; i < seq->count; i++) {
        uint32_t v = read_u32_le(ctx);
        info("  seq[%u] = %u", i, v);
    }
    if (ctx->csize & 1) {
        consume_bytes(ctx, 1);
    }
    return seq;
}

static void cleanup_seq(ChunkSeq *c) {
    if (!c) {
        return;
    }
    free(c->indexes);
    free(c);
}

static ChunkRate *parse_rate(ParseContext *ctx) {
    ChunkRate *rate = malloc(sizeof(ChunkRate));
    if (!rate) {
        err("OOM in function `%s`, line `%d`", __PRETTY_FUNCTION__, __LINE__);
        return NULL;
    }
    rate->count = ctx->csize / 4;
    rate->jiffies = malloc(rate->count * sizeof(unsigned));
    if (!rate->jiffies) {
        free(rate);
        err("OOM in function `%s`, line `%d`", __PRETTY_FUNCTION__, __LINE__);
        return NULL;
    }
    info(" rate: %u entries", rate->count);
    for (uint32_t i = 0; i < rate->count; i++) {
        uint32_t v = read_u32_le(ctx);
        info("  rate[%u] = %u jiffies (%.3f s)", i, v, v / 60.0);
        rate->jiffies[i] = v;
    }
    if (ctx->csize & 1) {
        consume_bytes(ctx, 1);
    }
    return rate;
}

static void cleanup_rate(ChunkRate *c) {
    if (!c) {
        return;
    }
    if (c->jiffies) {
        free(c->jiffies);
    }
    free(c);
}

static Frame *parse_frame(ParseContext *ctx) {
    char subid[5] = {0};
    if (!read_exact(ctx, subid, 4)) {
        ctx->eof = 1;
        return NULL;
    }
    uint32_t subsize = read_u32_le(ctx);
    info("  subchunk '%.4s' size=%u", subid, subsize);
    Frame *frame = NULL;
    if (strncmp(subid, "icon", 4) == 0) {
        // read icon data
        uint8_t *buf = malloc(subsize);
        if (!buf) {
            err("OOM in function `%s`, line `%d`", __PRETTY_FUNCTION__, __LINE__);
            return NULL;
        }
        if (!read_exact(ctx, buf, subsize)) {
            err("read icon fail");
            free(buf);
            return NULL;
        }
        frame = malloc(sizeof(Frame));
        if (!frame) {
            err("OOM in function `%s`, line `%d`", __PRETTY_FUNCTION__, __LINE__);
            free(buf);
            return NULL;
        }
        frame->size = subsize;
        frame->buffer = buf;
    } else {
        // skip subchunk data
        consume_bytes(ctx, subsize);
    }
    if (subsize & 1) {
        consume_bytes(ctx, 1);
    }
    return frame;
}

static void cleanup_frame(Frame *f) {
    if (!f) {
        return;
    }
    if (f->buffer) {
        free(f->buffer);
    }
    free(f);
}

static void cleanup_list(ChunkList *c) {
    if (!c) {
        return;
    }
    if (c->frames) {
        for (int i = 0; i < c->count; ++i) {
            cleanup_frame(c->frames[i]);
        }
        free(c->frames);
    }
    free(c);
}

static ChunkList *parse_list(ParseContext *ctx) {
    // LIST chunk has a 4-byte list-type then subchunks
    char listtype[5] = {0};
    if (!read_exact(ctx, listtype, 4)) {
        ctx->eof = 1;
        return NULL;
    }
    uint32_t list_payload = ctx->csize - 4;
    info(" LIST type='%.4s' payload=%u", listtype, list_payload);
    long list_end = ftell(ctx->file) + list_payload;
    ChunkList *list = NULL;
    if (strncmp(listtype, "fram", 4) == 0) {
        list = malloc(sizeof(ChunkList));
        if (!list) {
            err("OOM in function `%s`, line `%d`", __PRETTY_FUNCTION__, __LINE__);
            return NULL;
        }
        size_t capacitty = 4;
        list->count = 0;
        list->frames = malloc(capacitty * sizeof(Frame *));
        if (!list->frames) {
            free(list);
            err("OOM in function `%s`, line `%d`", __PRETTY_FUNCTION__, __LINE__);
            return NULL;
        }
        // inside fram: series of 'icon' chunks
        unsigned icon_count = 0;
        while (ftell(ctx->file) < list_end) {
            Frame *frame = parse_frame(ctx);
            if (!frame) {
                continue;
            }
            if (icon_count == 0) {
                // Parse hotspot, in 1st frame
                uint8_t *buf = frame->buffer;
                uint16_t type = buf[2] | (buf[3] << 8);
                if (type == 2) {
                    list->hotx = buf[10] | (buf[11] << 8);
                    list->hoty = buf[12] | (buf[13] << 8);
                } else {
                    list->hotx = 0;
                    list->hoty = 0;
                }
            }
            if (list->count + 1 > capacitty) {
                capacitty <<= 1;
                Frame **tmp = realloc(list->frames, capacitty * sizeof(Frame *));
                if (!tmp) {
                    cleanup_list(list);
                    return NULL;
                }
                list->frames = tmp;
            }
            icon_count++;
            list->frames[list->count++] = frame;
        }
        info("Done. Extracted %d icon chunks.", icon_count);
    } else {
        // skip entire list
        consume_bytes(ctx, list_payload);
    }
    if (ctx->csize & 1) {
        consume_bytes(ctx, 1);
    }
    return list;
}

static Chunk *parse_chunk(ParseContext *ctx) {
    long pos = ftell(ctx->file);

    // Read cid
    char cid[5] = {0};
    if (!read_exact(ctx, cid, 4)) {
        ctx->eof = 1;
        return NULL;
    }
    ctx->csize = read_u32_le(ctx);
    if (feof(ctx->file)) {
        ctx->eof = 1;
        return NULL;
    }
    info("Chunk '%.4s' size=%u at offset %ld", cid, ctx->csize, pos);

    Chunk *chunk = malloc(sizeof(Chunk));
    chunk->size = ctx->csize;
    chunk->off = pos;
#define is_cid(CID) !strncmp(cid, CID, 4)
    if (is_cid("anih")) {
        chunk->ty = ty_anih;
        chunk->inner = parse_anih(ctx);
        if (!chunk->inner) {
            free(chunk);
            err("Cannot parse anih");
            chunk = NULL;
        }
    } else if (is_cid("seq ")) {
        chunk->ty = ty_seq;
        chunk->inner = parse_seq(ctx);
        if (!chunk->inner) {
            free(chunk);
            err("Cannot parse seq");
            chunk = NULL;
        }
    } else if (is_cid("rate")) {
        // rate chunk: array of DWORDs
        chunk->ty = ty_rate;
        chunk->inner = parse_rate(ctx);
        if (!chunk->inner) {
            free(chunk);
            err("Cannot parse rate");
            chunk = NULL;
        }
    } else if (is_cid("LIST")) {
        chunk->ty = ty_list;
        chunk->inner = parse_list(ctx);
        if (!chunk->inner) {
            free(chunk);
            err("Cannot parse list");
            chunk = NULL;
        }
    } else {
        // Not interested in, skip
        consume_bytes(ctx, ctx->csize);
        if (ctx->csize & 1) {
            consume_bytes(ctx, 1);
        }
        free(chunk);
        chunk = NULL;
    }
#undef is_cid
    return chunk;
}

static void cleanup_chunk(Chunk *c) {
    if (!c) {
        return;
    }
    switch (c->ty) {
        case ty_anih: cleanup_anih(c->inner); break;
        case ty_seq: cleanup_seq(c->inner); break;
        case ty_rate: cleanup_rate(c->inner); break;
        case ty_list: cleanup_list(c->inner); break;
        default: assert(0);
    }
    free(c);
}

// Parse a file
AniFile *parse_ani(FILE *file) {
    AniFile *ani = malloc(sizeof(AniFile));
    if (!ani) {
        err("OOM in function `%s`, line `%d`", __PRETTY_FUNCTION__, __LINE__);
        return NULL;
    }
    unsigned capacity = 4;
    ani->chunk_count = 0;
    ani->chunks = malloc(capacity * sizeof(Chunk *));
    if (!ani->chunks) {
        err("OOM in function `%s`, line `%d`", __PRETTY_FUNCTION__, __LINE__);
        free(ani);
        return NULL;
    }

    // Generate context
    ParseContext ctx;
    ctx.file = file;
    ctx.eof = 0;

    // Read RIFF header
    char riff_tag[5] = {0}, acon_tag[5] = {0};
    if (!read_exact(&ctx, riff_tag, 4)) {
        err("read error");
    }

    uint32_t riff_size = read_u32_le(&ctx);
    if (!read_exact(&ctx, acon_tag, 4)) {
        err("read error");
    }

    if (strncmp(riff_tag, "RIFF", 4) != 0 || strncmp(acon_tag, "ACON", 4) != 0) {
        err("Not a RIFF ACON file: %.4s %.4s", riff_tag, acon_tag);
    }
    info("RIFF ACON detected, size=%u", riff_size);

    // Parse chunks
    while (1) {
        Chunk *chunk = parse_chunk(&ctx);
        if (ctx.eof) {
            break;
        }
        if (!chunk) {
            continue;
        }
        if (ani->chunk_count + 1 > capacity) {
            capacity <<= 1;
            Chunk **tmp = realloc(ani->chunks, capacity * sizeof(Chunk *));
            if (!tmp) {
                cleanup_ani(ani);
                return NULL;
            };
            ani->chunks = tmp;
        }
        ani->chunks[ani->chunk_count++] = chunk;
    }

    return ani;
}

void cleanup_ani(AniFile *ani) {
    if (ani) {
        for (int i = 0; i < ani->chunk_count; ++i) {
            cleanup_chunk(ani->chunks[i]);
        }
        free(ani->chunks);
        free(ani);
    }
}

void walk(const WalkContext *ctx) {
    for (unsigned i = 0; i < ctx->ani->chunk_count; ++i) {
        debug("Visit chunk `%d`", i);
        if (ctx->visit_chunk) {
            ctx->visit_chunk(ctx->ani->chunks[i], ctx->data);
        }
        if (ctx->ani->chunks[i]->ty == ty_list) {
            debug(" Hit chunk list");
            ChunkList *list = (ChunkList *)ctx->ani->chunks[i]->inner;
            if (ctx->visit_frame) {
                for (unsigned j = 0; j < list->count; ++j) {
                    debug("  Visit frame `%d`", j);
                    ctx->visit_frame(list->frames[j], ctx->data);
                }
            }
        }
    }
}
