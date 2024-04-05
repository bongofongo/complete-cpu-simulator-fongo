/*
 * CMSC 22200, Fall 2016
 *
 * ARM pipeline timing simulator
 *
 */

#include "cache.h"
#include "shell.h"
#include "pipe.h"
#include "bp.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

cache_t *cache_new(int sets, int ways, int block)
{
    cache_t* cres = malloc(sizeof(cache_t));
    cres->set = malloc(sets * sizeof(line_t*));
    int i, j, k;
    for (i = 0; i < sets; i++) {
        cres->set[i] = malloc(ways * sizeof(line_t));
        for (j = 0; j < ways; j++) {
            cres->set[i][j].block = malloc((block/4) * sizeof(uint64_t));
            for (k = 0; k < block/4; k++) {
                cres->set[i][j].block[k] = 0;
            }
            cres->set[i][j].valid = 0;
            cres->set[i][j].clock = 0;
            cres->set[i][j].tag = 0;
        }
        
    }
    cres->block_size = block;
    cres->set_no = sets;
    cres->ways = ways;
    return cres;
}

void cache_destroy(cache_t *c)
{
    int block = c->block_size;
    int set_no = c->set_no;
    int ways = c->ways;
    int i, j;
    for (i = 0; i < set_no; i++) {
        for (j = 0; j < ways; j++) {
            free(c->set[i][j].block);
        }
        free(c->set[i]);
    }
    free(c->set);
    free(c);
}

int cache_update(cache_t *c, uint64_t addr, int* lineNo)
{
    // How to know if in right set?
    int i;
    // What does return mean?
    // Update if hit? Update if miss?
    int s = my_log2(c->set_no);
    int b = my_log2(c->block_size);
    int block_offset = takebits64(addr, b-1, 0);
    int set_i = takebits64(addr, b+s, b);
    uint64_t addr_tag = takebits(addr, 63, b+s+1);
    uint32_t LRU = 0xffffffff;
    int LRU_line = 0;
    // CHECK HIT
    //printf("ways: %d, set_ind(%d), block_offset(%d)\n", c->ways, set_i, block_offset);
    printf("Caching address: 0x%lx\n", addr);
    for (i = 0; i < c->ways; i++) {
        //printf("test hit %d\n", i);
        if (c->set[set_i][i].tag == addr_tag && c->set[set_i][i].valid == true) {
            //printf("HIT: Set %d line %d\n", set_i, i);
            //*set_index = set_i;
            //printf("hit: cacheTag: %ld\n", c->set[set_i][i].tag);
            return 1;
            c->set[set_i][i].clock = stat_cycles;
        }
    }
    // IF MISS
    for (i = 0; i < c->ways; i++) {
        if (c->set[set_i][i].clock < LRU) {
            LRU = c->set[set_i][i].clock;
            LRU_line = i;
        }
    }
    *lineNo = LRU_line;
    //printf("LRU: Line %d\n", LRU_line);
    c->set[set_i][LRU_line].valid = true;
    c->set[set_i][LRU_line].tag = takebits64(addr, 63, b+s+1);
    c->set[set_i][LRU_line].clock = stat_cycles;
    for (i = 0; i < c->block_size/4; i++) {
        c->set[set_i][LRU_line].block[i] = mem_read_32(addr+(i*4));
    }
    return 0;
}

int cache_compare(cache_t* c, uint64_t add1, uint64_t add2) {
    int b = my_log2(c->block_size);
    return ((add1 >> b) == (add2 >> b));
}
void cache_remove(cache_t* c, uint64_t address, int line) {
    int s = my_log2(c->set_no);
    int b = my_log2(c->block_size);
    int block_offset = takebits64(address, b-1, 0);
    int set_i = takebits64(address, b+s, b);
    for (int i = 0; i < c->block_size/4; i++) {
        c->set[set_i][line].block[i] = 0;
    }
    c->set[set_i][line].clock = 0;
    c->set[set_i][line].valid = false;
    c->set[set_i][line].tag = 0;
}

uint32_t cache_read(cache_t* c, uint64_t address, int set_index) {
    printf("cache hit\n");
}

uint64_t takebits64(uint64_t input, uint32_t a, uint32_t b) {
    uint64_t mask = ((1 << (a - b + 1)) - 1);
    return ((input >> b) & mask);
}

int my_log2(int n) {
    int result = -1;

    while (n != 0) {
        n >>= 1;
        result++;
    }

    return result;
}
