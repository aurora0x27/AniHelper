#pragma once

#include <stdio.h>
#include <stdint.h>

// Interested chunk type
enum ChunkType { ty_anih, ty_seq, ty_rate, ty_list };

typedef struct {
    uint32_t cbSize;
    uint32_t cFrames;
    uint32_t cSteps;
    uint32_t cx;
    uint32_t cy;
    uint32_t cBitCount;
    uint32_t cPlanes;
    uint32_t jifRate;
    uint32_t flags;
} ChunkAnih;

typedef struct {
    unsigned count;
    unsigned *indexes;
} ChunkSeq;

typedef struct {
    unsigned count;
    uint32_t *jiffies;
} ChunkRate;

typedef struct {
    size_t size;
    void *buffer;
} Frame;

typedef struct {
    unsigned count;
    Frame **frames;
} ChunkList;

typedef struct {
    unsigned size;
    unsigned off;
    enum ChunkType ty;
    void *inner;
} Chunk;

typedef struct {
    unsigned chunk_count;
    Chunk **chunks;
} AniFile;

typedef void (*VisitChunkCallback)(const Chunk *, void *data);

typedef void (*VisitFrameCallback)(const Frame *, void *data);

typedef struct {
    AniFile *ani;
    void *data;
    VisitChunkCallback visit_chunk;
    VisitFrameCallback visit_frame;
} WalkContext;

void walk(WalkContext *ctx);

AniFile *parse_ani(FILE *file);

void cleanup_ani(AniFile *file);
