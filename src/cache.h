/*
 * CMSC 22200, Fall 2016
 *
 * ARM pipeline timing simulator
 *
 */
#ifndef _CACHE_H_
#define _CACHE_H_

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

typedef struct {
    bool valid;
    uint32_t clock; // Age
    uint64_t tag;
    uint64_t* block;
} line_t;

typedef struct {
    line_t** set;
    int set_no;
    int block_size; // in bytes
    int ways;
} cache_t;

uint64_t takebits64(uint64_t input, uint32_t a, uint32_t b);
cache_t *cache_new(int sets, int ways, int block);
void cache_destroy(cache_t *c);
int cache_update(cache_t *c, uint64_t addr, int* lineNo);
int cache_compare(cache_t* c, uint64_t add1, uint64_t add2);
void cache_remove(cache_t* c, uint64_t address, int line);
int my_log2(int n);
#endif
