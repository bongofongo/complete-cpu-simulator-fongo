/*
 * ARM pipeline timing simulator
 *
 * CMSC 22200
 */

#include "stdbool.h"
#include "shell.h"
#include <limits.h>

#ifndef _BP_H_
#define _BP_H_



typedef struct {
    unsigned char GHR;
    unsigned char PHT[256];
} GSHARE;

typedef struct {
    bool valid;
    bool conditional; // 1: conditional, 0: unconditional
    uint64_t address_tag; // Full Fetch PC
    uint64_t btarget; // Branch target (last two bits = 00)
} BTB;

typedef struct bp_t {
    GSHARE* gshare;
    BTB* btb[1024];
} bp_t;
extern bp_t* bp;

uint64_t bp_predict(uint64_t PC, bool* hit);
void bp_update(uint64_t btarget, uint64_t PC, bool conditional, bool taken);

void update_gshare_predictor(uint64_t PC, int taken);
bool gshare_predict(uint64_t PC);
void update_btb(uint64_t PC, uint64_t btarget, bool conditional);
uint64_t query_btb(uint64_t PC, bool *conditional);

#endif
