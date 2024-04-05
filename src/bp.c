/*
 * ARM pipeline timing simulator
 *
 * CMSC 22200
 */

#include "bp.h"
#include "stdbool.h"
#include "shell.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>



uint64_t bp_predict(uint64_t PC, bool* hit)
{
    bool conditional;
    uint64_t btarget = query_btb(PC, &conditional);
    //printf("bp_predict btarget: 0x%lx\n", btarget);
    //printf("btb_conditional? %d, target = 0x%lx\n", conditional, btarget);
    if (btarget) {
        *hit = true;
        if (!conditional || gshare_predict(PC)) {
            //printf("predict branch_target: conditional: %d, gshare_prediction: %d\n", conditional, gshare_predict(PC));
            return btarget;
        } 
    }
    //printf("predict PC+4: conditional: %d, gshare_prediction: %d\n", conditional, gshare_predict(PC));
    return PC + 4;
}

void bp_update(uint64_t btarget, uint64_t PC, bool conditional, bool taken)
{
    update_btb(PC, btarget, conditional);

    if (conditional) {
        update_gshare_predictor(PC, taken);
    }

}

void update_gshare_predictor(uint64_t PC, int taken) {
    // XOR the GHR with bits [9:2] of the PC to get the index into the PHT
    unsigned char index = (bp->gshare->GHR ^ (PC >> 1)) & 0xFF;

    // Update the 2-bit saturating counter in the PHT
    if (taken) {
        if (bp->gshare->PHT[index] < 3) {
            bp->gshare->PHT[index]++;
        }
    } else {
        if (bp->gshare->PHT[index] > 0) {
            bp->gshare->PHT[index]--;
        }
    }

    // Update the GHR
    bp->gshare->GHR = (bp->gshare->GHR << 1) | (taken & 0x1);
}

bool gshare_predict(uint64_t PC) {
    // XOR the GHR with bits [9:2] of the PC to get the index into the PHT
    unsigned char index = (bp->gshare->GHR ^ (PC >> 1)) & 0xFF; // 8 bits

    // If the PHT entry is weakly taken (10) or strongly taken (11), predict taken
    //printf ("ghare_predict PHT[%d] = %d\n", index, bp->gshare->PHT[index]);
    return (bp->gshare->PHT[index] >= 2);
}

void update_btb(uint64_t PC, uint64_t btarget, bool conditional) {
    // Index the BTB using bits [11:2] of the PC
    unsigned int index = (PC >> 2) & 0x3FF; // 10 bits

    // Update the BTB entry
    bp->btb[index]->address_tag = PC;
    bp->btb[index]->valid = true;
    bp->btb[index]->conditional = conditional;
    bp->btb[index]->btarget = btarget;
}

// Returns 0 if miss, else returns target address
uint64_t query_btb(uint64_t PC, bool *conditional) {
    // Index the BTB using bits [11:2] of the PC
    unsigned int index = (PC >> 2) & 0x3FF;

    // misses in the BTB (i.e., address tag != PC or valid bit == 0),
    if (bp->btb[index]->valid && bp->btb[index]->address_tag == PC) {
        *conditional = bp->btb[index]->conditional;
        //printf("BTB HIT\n");
        return bp->btb[index]->btarget;
    } else {
        *conditional = 0;
        //printf("BTB MISS\n");
        return 0;
    }
}
