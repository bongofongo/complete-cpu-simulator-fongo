/*
 * CMSC 22200
 *
 * ARM pipeline timing simulator
 */

#include "pipe.h"
#include "shell.h"
#include "stdbool.h"
#include "cache.h"
#include "bp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

/* global pipeline state */
CPU_State CURRENT_STATE;
bp_t* bp;
PIPE* pipe;
cache_t* iCache;
cache_t* dCache;

GSHARE* make_gshare() {
    GSHARE* gres = (GSHARE*)malloc(sizeof(GSHARE));
    gres->GHR = 0;
    for (int i = 0; i < 256; i++) {
        gres->PHT[i] = 0;
    }
    return gres;
}
BTB* make_btb() {
    BTB* btb = (BTB*)malloc(sizeof(BTB));
    btb->valid = 0;
    btb->conditional = 0;
    btb->address_tag = 0;
    btb->btarget = 0;
    return btb;
}
bp_t* make_bpt() {
    bp_t* bres = (bp_t*)malloc(sizeof(bp_t));
    bres->gshare = make_gshare();
    for (int i = 0; i < 1024; i++) {
        bres->btb[i] = make_btb();
    }
    return bres;
}

instruction *make_new_inst() {
    instruction *res = (instruction*)malloc(sizeof(instruction));
    res->name = (char*)malloc(8*sizeof(char)); // specific type of instruction (e.g. ADD, ADDS, etc.)
    res->name = "";
    res->type = NO_TYPE;
    res->rt, res->rn, res->rm = 100;
    res->rnVal, res->rmVal, res->rtVal = 0;
    res->imm, res->offset, res->shamt = 0;
    res->BR = 0;
    res->condBR = 0;
    res->hw = 0;
    res->op2 = 0;
    res->hltInst = false;
    res->hit = false;
    res->valid = false;

    res->fetched_instruction = 0;
    res->next_address, res->branch_address = 0;
    res->current_address = 0;
    res->flagged = false;
    res->forwarded = 0;

    res->effective_address = 0;
    res->ALU_out, res->mem_out = 0;
    res->memWrite, res->memRead = false;
    res->writeBack = 0;
    res->loadBytes = 0;

    res->FLAG_N, res->FLAG_Z = 0;
    return res;
}

void pipe_reg_transfer(instruction* inst1, instruction* inst2) {
    inst2->name = inst1->name;
    inst2->type = inst1->type;
    inst2->rt = inst1->rt;
    inst2->rtVal = inst1->rtVal;
    inst2->rn = inst1->rn;
    inst2->rnVal = inst1->rnVal;
    inst2->rm = inst1->rm;
    inst2->rmVal = inst1->rmVal;
    inst2->imm = inst1->imm;
    inst2->shamt = inst1->shamt;
    inst2->BR = inst1->BR;
    inst2->offset = inst1->offset;
    inst2->condBR = inst1->condBR;
    inst2->hw = inst1->hw;
    inst2->op2 = inst1->op2;
    inst2->hltInst = inst1->hltInst;
    inst2->hit = inst1->hit;
    inst2->valid = inst1->valid;

    inst2->fetched_instruction = inst1->fetched_instruction;
    inst2->next_address = inst1->next_address;
    inst2->current_address = inst1->current_address;
    inst2->branch_address = inst1->branch_address;
    inst2->flagged = inst1->flagged;
    inst2->forwarded = inst1->forwarded;

    inst2->effective_address = inst1->effective_address;
    inst2->ALU_out = inst1->ALU_out;
    inst2->mem_out = inst1->mem_out;
    inst2->memWrite = inst1->memWrite;
    inst2->memRead = inst1->memRead;
    inst2->writeBack = inst1->writeBack;
    inst2->loadBytes = inst1->loadBytes;

    inst2->FLAG_N = inst1->FLAG_N;
    inst2->FLAG_Z = inst1->FLAG_Z;
}

PIPE* make_new_pipe() {
    PIPE* pres = (PIPE*)malloc(sizeof(PIPE));
    pres->IFtoDE = make_new_inst();
    pres->DEtoEX = make_new_inst();
    pres->EXtoMEM = make_new_inst();
    pres->MEMtoWB = make_new_inst();
    pres->stall = 0;
    pres->btaken = false;
    pres->halt = -1;
    pres->flush = 0;
    pres->fetch_stall = 0;
    pres->missAddress = 0;
    pres->missPending = false;
    pres->lineNumber = 0;
    pres->memStall = 0;
    return pres;
}

void freePipe(PIPE* p) {
    free(p->IFtoDE);
    free(p->DEtoEX);
    free(p->EXtoMEM);
    free(p->MEMtoWB);
    free(p);
}

void pipe_init()
{
    memset(&CURRENT_STATE, 0, sizeof(CPU_State));
    CURRENT_STATE.PC = 0x00400000;
    pipe = make_new_pipe();
    bp = make_bpt();
    iCache  = cache_new(64, 4, 32);
    dCache = cache_new(256, 8, 32);
}

void ex_hazard() {

    /* 1a. pipe->exEXtoMEM->rt = pipe->DEtoEX->rn 
       1b. pipe->exEXtoMEM->rt = pipe->DEtoEX->rm 
       2a. pipe->MEMtoWB->rt = pipe->DEtoEX->rn 
       2b. pipe->MEMtoWB->rt = pipe->DEtoEX->rm */

    // First instruction - Mem to Ex Forward
    int ForwardA, ForwardB = 0;
    if (pipe->EXtoMEM->writeBack == 1
        && (pipe->EXtoMEM->rt != 31)
        && (pipe->EXtoMEM->rt == pipe->DEtoEX->rn)) {
            //printf("DEtoEX->rn: %d, EXtoMEM->ALU_out: %ld\n", pipe->DEtoEX->rn, pipe->EXtoMEM->ALU_out);
            ForwardA = 10;
            pipe->DEtoEX->rnVal = pipe->EXtoMEM->ALU_out;
            pipe->DEtoEX->forwarded = 1;
        }
    if (pipe->EXtoMEM->writeBack
        && (pipe->EXtoMEM->rt  != 31)
        && (pipe->EXtoMEM->rt == pipe->DEtoEX->rm)) {
            //printf("DEtoEX->rm: %d, EXtoMEM->rt: %d, EXtoMEM->ALU_out: %ld\n", pipe->DEtoEX->rm, pipe->EXtoMEM->rt, pipe->EXtoMEM->ALU_out);
            ForwardB = 10;
            pipe->DEtoEX->rmVal = pipe->EXtoMEM->ALU_out;
            pipe->DEtoEX->forwarded = 2;
        }
}

void mem_hazard() {
    // Second Instruction - WB to Ex Forward
    //printf("MEMtoWB->writeBack: %d, MEMtoWB->rt: %d, EXtoMEM->rt: %d, DEtoEX->rn: %d, DEtoEX->rm: %d\n",
    //pipe->MEMtoWB->writeBack, pipe->MEMtoWB->rt, pipe->EXtoMEM->rt, pipe->DEtoEX->rn, pipe->DEtoEX->rm);
    int ForwardA, ForwardB = 0;
    if (pipe->MEMtoWB->writeBack == 1
        && (pipe->MEMtoWB->rt != 31)
        && !(pipe->EXtoMEM->writeBack && (pipe->EXtoMEM->rt != 31)
            && (pipe->EXtoMEM->rt != pipe->DEtoEX->rn))
        && (pipe->MEMtoWB->rt == pipe->DEtoEX->rn)) {
            ForwardA = 01;
            if (pipe->MEMtoWB->writeBack == 1) {
                pipe->DEtoEX->rnVal = pipe->MEMtoWB->ALU_out;
            }
            if (pipe->MEMtoWB->writeBack == 2) {
                pipe->DEtoEX->rnVal = pipe->MEMtoWB->mem_out;
            }
            pipe->DEtoEX->forwarded = 1;
        }

    if (pipe->MEMtoWB->writeBack
        && (pipe->MEMtoWB->rt != 31)
        && !(pipe->EXtoMEM->writeBack && (pipe->EXtoMEM->rt != 31)
            && (pipe->EXtoMEM->rt != pipe->DEtoEX->rm))
        && (pipe->MEMtoWB->rt == pipe->DEtoEX->rm)) {
            ForwardB = 01;
            if (pipe->MEMtoWB->writeBack == 1) {
                pipe->DEtoEX->rmVal = pipe->MEMtoWB->ALU_out;
            }
            if (pipe->MEMtoWB->writeBack == 2) {
                pipe->DEtoEX->rmVal = pipe->MEMtoWB->mem_out;
            }
            printf("rmVal: %ld\n", pipe->DEtoEX->rmVal);
            pipe->DEtoEX->forwarded = 2;
        }

    // Load / store forward
    if ((pipe->MEMtoWB->memRead) && (pipe->DEtoEX->memWrite) &&
        (pipe->MEMtoWB->rt == pipe->DEtoEX->rt)) {
            pipe->DEtoEX->rtVal = pipe->MEMtoWB->mem_out;
            pipe->DEtoEX->forwarded = 3;
    }
}

// Goes in Decode.
void hazard_detection_unit() {
        // Load Stall
    //printf("HDU: %s, memRead? %d, DEtoEX->rt: %d, IFtoDE->rn: %d, IFtoDE->rm: %d, IFtoDE->rt: %d\n", pipe->DEtoEX->name, pipe->DEtoEX->memRead, pipe->DEtoEX->rt, pipe->IFtoDE->rn, pipe->IFtoDE->rm, pipe->IFtoDE->rt);
    if (pipe->DEtoEX->memRead &&
       ((pipe->DEtoEX->rt == pipe->IFtoDE->rn) ||
        (pipe->DEtoEX->rt == pipe->IFtoDE->rm))) {
           printf("     stalled Load1\n");
           pipe->stall = 1;
       }
       //Load Store Stall
    if (pipe->DEtoEX->memRead && 
       (pipe->DEtoEX->rt == pipe->IFtoDE->rt) && pipe->IFtoDE->memWrite) {
           pipe->stall = 1;
           printf("     stalled Load2\n");
       }
}

void pipe_cycle()
{
    printf("cycle %d\n\n", stat_cycles);
    //printf("CURRENT_STATE.PC: 0x%lx\n", CURRENT_STATE.PC);
    pipe_stage_wb();
    if (pipe->memStall == 0) {
        pipe_stage_mem();
        if (pipe->memStall != 0) {
            pipe->memStall--;
            if (pipe->flush > 0) {
                pipe->flush--;
                printf("flushing %d\n", pipe->flush);
            }
            if (pipe->fetch_stall) {
                pipe->fetch_stall--;
                printf("fetch_stalling %d\n", pipe->fetch_stall);
            }
            printf("mem stalling %d\n", pipe->memStall);
            return;
        }
        pipe_stage_execute();
        if (pipe->stall  == 0) {
            if (pipe->halt < 0) {
                pipe_stage_decode();
                pipe_stage_fetch();
            }
        } else {
            pipe->stall--;
        }
        
    } else {
        pipe->memStall--;
        if (pipe->flush > 0) {
            pipe->flush--;
            printf("flushing %d\n", pipe->flush);
        }
        if (pipe->fetch_stall) {
            pipe->fetch_stall--;
            printf("fetch_stalling %d\n", pipe->fetch_stall);
        }
        printf("mem stalling %d\n", pipe->memStall);
    }
    

}

void pipe_stage_wb()
{
    printf("WB: %s X%d, ..., writeBack: %d\n", pipe->MEMtoWB->name, pipe->MEMtoWB->rt, pipe->MEMtoWB->writeBack);

    mem_hazard();
    if (pipe->memStall > 0) {
        return;
    }
    if (pipe->MEMtoWB->writeBack == 1) {
        CURRENT_STATE.REGS[pipe->MEMtoWB->rt] = pipe->MEMtoWB->ALU_out;
    }
    if (pipe->MEMtoWB->writeBack == 2) {
        CURRENT_STATE.REGS[pipe->MEMtoWB->rt] = pipe->MEMtoWB->mem_out;
    }

    if (pipe->MEMtoWB->flagged) {
        CURRENT_STATE.FLAG_Z = pipe->MEMtoWB->FLAG_Z;
        CURRENT_STATE.FLAG_N = pipe->MEMtoWB->FLAG_N;
    }

    if (pipe->MEMtoWB->valid) {
        ++stat_inst_retire;
    } else {
        if (!strcmp(pipe->MEMtoWB->name, "flush")) {
            printf("flushed\n");
        }
        else if (!strcmp(pipe->MEMtoWB->name, "fetch_stall")) {
            printf("fetch_stall\n");
        }
    }
    
    pipe->halt--;


    if (pipe->halt == 0) {
        cache_destroy(iCache);
        RUN_BIT = 0;
    }
   
}

void loadWrite_dCache(bool load, bool write, uint64_t address) {
    int line;
    int hit = cache_update(dCache, address, &line);
    if (!hit) {
        printf("dCache miss\n");
        pipe->memStall = 10;
        return;
    }
    printf("dCache Hit\n");
}
void pipe_stage_mem()
{
    printf("MEM: %s X%d, ...\n", pipe->EXtoMEM->name, pipe->EXtoMEM->rt);
    ex_hazard();

    if (pipe->EXtoMEM->type == D_TYPE) {
        loadWrite_dCache(pipe->EXtoMEM->memRead, pipe->EXtoMEM->memWrite, pipe->EXtoMEM->effective_address);
        if (pipe->memStall > 0) {
            instruction* temp = make_new_inst();
            temp->name = "dCache stall bubble";
            pipe_reg_transfer(temp, pipe->MEMtoWB);
            return;
        }
        if (pipe->EXtoMEM->memWrite == true) {
            switch (pipe->EXtoMEM->loadBytes) {
                case 0:
                    return;
                case 1: 
                    mem_write_32(pipe->EXtoMEM->effective_address, CURRENT_STATE.REGS[pipe->EXtoMEM->rt]);
                    break;
                case 2:
                    mem_write_32(pipe->EXtoMEM->effective_address, (int16_t)CURRENT_STATE.REGS[pipe->EXtoMEM->rt]);
                    break;
                case 3:
                    mem_write_32(pipe->EXtoMEM->effective_address, (char)CURRENT_STATE.REGS[pipe->EXtoMEM->rt]);
                    break;
            }
        }
        if (pipe->EXtoMEM->memRead == true) {
            switch (pipe->EXtoMEM->loadBytes) {
                case 0:
                    return;
                case 1: 
                    //Writing 64 bits
                    pipe->EXtoMEM->mem_out = (((uint64_t)(mem_read_32(pipe->EXtoMEM->effective_address + 4))) << 32) | mem_read_32(pipe->EXtoMEM->effective_address);
                    break;
                case 2:
                    pipe->EXtoMEM->mem_out = (int16_t)mem_read_32(pipe->EXtoMEM->effective_address);
                    break;
                case 3:
                    pipe->EXtoMEM->mem_out = (char)mem_read_32(pipe->EXtoMEM->effective_address);
                    break;
            }
        }
    }
    // Flag Forwarding
    pipe->DEtoEX->FLAG_Z = pipe->EXtoMEM->FLAG_Z;
    pipe->DEtoEX->FLAG_N = pipe->EXtoMEM->FLAG_N;
    pipe_reg_transfer(pipe->EXtoMEM, pipe->MEMtoWB);

}

void pipe_stage_execute()
{
    printf("EX: %s, memRead? %d, rt: %d, rn: %d, rm: %d\n", pipe->DEtoEX->name, pipe->DEtoEX->memRead, pipe->DEtoEX->rt, pipe->DEtoEX->rn, pipe->DEtoEX->rm);
    if (!pipe->DEtoEX->valid) {
        pipe_reg_transfer(pipe->DEtoEX, pipe->EXtoMEM);
        return;
    }
    if (pipe->DEtoEX->type == R_TYPE) {
        pipe->DEtoEX->ALU_out = exec_R(pipe->DEtoEX);
    } else if (pipe->DEtoEX->type == I_TYPE) {
        pipe->DEtoEX->ALU_out = exec_I(pipe->DEtoEX);
    } else if (pipe->DEtoEX->type == D_TYPE) {
        //printf("offset + regs = 0x%lx + 0x%lx\n", pipe->DEtoEX->offset, CURRENT_STATE.REGS[pipe->DEtoEX->rn]);
        if (pipe->DEtoEX->forwarded == 1) {
            pipe->DEtoEX->effective_address = pipe->DEtoEX->offset + pipe->DEtoEX->rnVal;
        } else {
            pipe->DEtoEX->effective_address = pipe->DEtoEX->offset + CURRENT_STATE.REGS[pipe->DEtoEX->rn];
        }
    }

    //printf("EX: pipe->DEtoEX->branch_address = %lx, offset = %lx\n", pipe->DEtoEX->branch_address, pipe->DEtoEX->offset);
    //printf("FLAG_N: %d, FLAG_Z: %d\n", pipe->DEtoEX->FLAG_N, pipe->DEtoEX->FLAG_Z);
    if (pipe->DEtoEX->type == CB_TYPE || pipe->DEtoEX->type == B_TYPE) {
        instruction* new_inst = make_new_inst();
        new_inst->valid = true;
        new_inst->name = "Branched";
        bool conditional;
        pipe->btaken = exec_B(pipe->DEtoEX, &conditional);
        bp_update(pipe->DEtoEX->branch_address, pipe->DEtoEX->current_address, conditional, pipe->btaken);

        // Conditional not taken, but predicted it would.
        if ((pipe->DEtoEX->hit == true) && (pipe->btaken == false)) {
            pipe->flush = 2;
            CURRENT_STATE.PC = pipe->DEtoEX->current_address + 4;
            pipe_reg_transfer(new_inst, pipe->EXtoMEM);
            return;
        }

        //The instruction is a branch, but the predicted target destination does not match the actual target.
        if (pipe->DEtoEX->branch_address != pipe->DEtoEX->next_address) {
            printf("EX: branch_add: %lx, next_add: %lx, current_add: 0x%lx\n", pipe->DEtoEX->branch_address, pipe->DEtoEX->next_address, pipe->DEtoEX->current_address);
            printf("miss pending? %d\n", pipe->missPending);
            pipe->flush = 2;
            CURRENT_STATE.PC = pipe->DEtoEX->branch_address;
            if (pipe->missPending == true) {
                // If branch address doesn't match pending miss address
                if (0 == cache_compare(iCache, pipe->missAddress, pipe->DEtoEX->branch_address)) {
                    printf("CANCEL CACHE MISS, missAddress: 0x%lx, branchAddress: 0x%lx, LineNumber: %d\n", pipe->missAddress, pipe->DEtoEX->branch_address, pipe->lineNumber);
                    cache_remove(iCache, pipe->missAddress, pipe->lineNumber);
                    pipe->missPending = false;
                    pipe->fetch_stall = 0;
                }
            }
            
            pipe_reg_transfer(new_inst, pipe->EXtoMEM);
            printf("branch mispredicted.\n");
            return;
        }
        // BTB miss
        if (pipe->DEtoEX->hit == false) {
            pipe->flush = 2;
            CURRENT_STATE.PC = pipe->DEtoEX->branch_address;
            pipe_reg_transfer(new_inst, pipe->EXtoMEM);
            return;
        }
        pipe_reg_transfer(new_inst, pipe->EXtoMEM);
        printf("Branch predicted correctly or untaken\n");
        return;
    }

    // Insert Bubble
    if (pipe->stall) {
        instruction* temp = make_new_inst();
        temp->valid = false;
        temp->name = "bubble";
        pipe_reg_transfer(temp, pipe->EXtoMEM);
    } else {
        pipe_reg_transfer(pipe->DEtoEX, pipe->EXtoMEM);
    }

}

void pipe_stage_decode()
{
	decode(pipe->IFtoDE->fetched_instruction, pipe->IFtoDE);
    // Hazard Detection
    hazard_detection_unit();
    printf("DE: %s X%d, ... current_add: 0x%lx\n", pipe->IFtoDE->name, pipe->IFtoDE->rt, pipe->IFtoDE->current_address);
    if (pipe->flush > 0) {
        instruction* temp = make_new_inst();
        temp->valid = false;
        temp->name = "flush";
        pipe_reg_transfer(temp, pipe->DEtoEX);
        pipe->flush--;
    } else {
        pipe_reg_transfer(pipe->IFtoDE, pipe->DEtoEX);
    }
}

void pipe_stage_fetch()
{
    instruction* temp = make_new_inst();
    //printf("PC: %lx\n", CURRENT_STATE.PC);

    if (pipe->fetch_stall > 0) {
        //printf("IF: flushed--\n");
        temp->valid = false;
        temp->name = "cache bubble";
        //printf("    CACHING BUBBLE INSERT\n");
        pipe_reg_transfer(temp, pipe->IFtoDE);
        pipe->fetch_stall--;
        if (pipe->flush > 0) {
            pipe->flush--;
        }
        return;
    } else {
        pipe->missPending = false;
    }
    if (pipe->flush > 0) {
        //printf("IF: flushed--\n");
        temp->valid = false;
        temp->name = "flush";
        pipe_reg_transfer(temp, pipe->IFtoDE);
        pipe->flush--;
        return;
    }
    
    else {
        if (cache_update(iCache, CURRENT_STATE.PC, &pipe->lineNumber)) {
            printf("iCache hit\n");
            temp->fetched_instruction = mem_read_32(CURRENT_STATE.PC);
        } else {
            pipe->missAddress = CURRENT_STATE.PC;
            pipe-> missPending = true;
            pipe->fetch_stall = 9;
            printf("iCache miss\n");
            temp->name = "cache bubble";
            pipe_reg_transfer(temp, pipe->IFtoDE);
            return;
        }
        temp->current_address = CURRENT_STATE.PC;
        CURRENT_STATE.PC = bp_predict(CURRENT_STATE.PC, &temp->hit);
        temp->next_address = CURRENT_STATE.PC;
        printf("FETCH: bp->HIT: %d, current_address: 0x%lx, predicted_(next)_address: 0x%lx\n", temp->hit, temp->current_address, temp->next_address);
        pipe_reg_transfer(temp, pipe->IFtoDE);
    }

}

int64_t sign_extend(uint64_t value, unsigned int n) {
    if (value & (1ULL << (n - 1))) { // check if the sign bit is set
        value |= (UINT64_MAX << n);  // sign extend by filling with 1s
    }
    return (int64_t)value;           // cast to signed 64-bit integer && return
}

uint32_t takebits(uint32_t input, uint32_t a, uint32_t b) {
    uint32_t mask = ((1 << (a - b + 1)) - 1);
    return ((input >> b) & mask);
}

int64_t takebits_extend(unsigned int input, unsigned int a, unsigned int b) {
    uint64_t temp = takebits(input, a, b) << 2;
    return sign_extend(temp, (2 + (a - b)));
}

// helper to set data for (extended) R-type instructions
// rm=20-16, opt=15-13, imm=12-10, rn=9-5, rt=4-0
void setinst_ex_R(uint32_t input, instruction *inst) {
    inst->type = R_TYPE;
    inst->rm = takebits(input, 20, 16);
    inst->rt = takebits(input, 4, 0);
    inst->opt = takebits(input, 15, 13);
    inst->imm = takebits(input, 12, 10);
    inst->rn = takebits(input, 9, 5);
    inst->writeBack = 1;
    inst->memRead, inst->memWrite = false;
    inst->valid = true;
}

// helper to set data for (shifted) R-type instructions
// rm=20-16, imm=15-10, rn=9-5, rt=4-0
void setinst_sh_R(uint32_t input, instruction *inst) {
    inst->type = R_TYPE;
    inst->rm = takebits(input, 20, 16);
    inst->imm = takebits(input, 15, 10);
    inst->rn = takebits(input, 9, 5);
    inst->rt = takebits(input, 4, 0);
    inst->writeBack = 1;
    inst->memRead, inst->memWrite = false;
    inst->valid = true;
}

// helper to set data for (immediate) I-type instructions
// shift=23-22, imm=21-10, rn=9-5, rt=4-0
void setinst_I(uint32_t input, instruction *inst) {
    inst->type = I_TYPE;
    inst->shamt = takebits(input, 23, 22);
    inst->imm = takebits(input, 21, 10);
    inst->rn = takebits(input, 9, 5);
    inst->rt = takebits(input, 4, 0);
    inst->writeBack = 1;
    inst->memRead, inst->memWrite = false;
    inst->valid = true;
}

// helper to set data for (load/store) D-type instructions
// imm=20-12, 0=11-10, rn=9-5, rt=4-0
void setinst_D(uint32_t input, instruction *inst) {
    inst->type = D_TYPE;
    inst->offset = takebits(input, 20, 12);
    inst->rn = takebits(input, 9, 5);
    inst->rt = takebits(input, 4, 0);
    inst->valid = true;
}


void decode(uint32_t input, instruction *instruction) {
    uint32_t op21 = takebits(input, 31, 21); // bits 31 to 21
    switch (op21) {
        case 0b10001011000 :
            instruction->name = "ADD";
            setinst_sh_R(input, instruction);
            return;
        case 0b10101011001 :
            instruction->name = "ADDS";
            setinst_ex_R(input, instruction);
            return;
        case 0b10101011000 :
            instruction->name = "ADDS";
            setinst_sh_R(input, instruction);
            return;
        case 0b10001010000 :
            instruction->name = "AND";
            setinst_sh_R(input, instruction);
            return;
        case 0b11101010000 :
            instruction->name = "ANDS";
            setinst_sh_R(input, instruction);
            return;
        case 0b10101010000 :
            instruction->name = "ORR";
            setinst_sh_R(input, instruction);
            return;
        case 0b11101011000 :
            setinst_sh_R(input, instruction);
            if (instruction->rt == 0b11111) {
                instruction->name = "CMP";
                instruction->writeBack = 0;
            } else {
                instruction->name = "SUBS";
            }
            return;
        case 0b11001010000 :
            instruction->name = "EOR";
            setinst_sh_R(input, instruction);
            return;
        case 0b11010100010 :
            instruction->name = "HLT";
            instruction->imm = takebits(input, 20, 5);
            instruction->op2 = takebits(input, 4, 2);
            instruction->hltInst = true;
            instruction-> valid = true;
            pipe->halt = 3;
            return;
        case 0b11111000010 :
            instruction->name = "LDUR";
            setinst_D(input, instruction);
            instruction->memRead = true;
            instruction->memWrite = false;
            instruction->loadBytes = 1;
            instruction->writeBack = 2;
            return;
        case 0b00111000010 :
            instruction->name = "LDURB";
            setinst_D(input, instruction);
            instruction->memRead = true;
            instruction->memWrite = false;
            instruction->loadBytes = 3;
            instruction->writeBack = 2;
            return;
        case 0b01111000010 :
            instruction->name = "LDURH";
            setinst_D(input, instruction);
            instruction->memRead = true;
            instruction->memWrite = false;
            instruction->loadBytes = 2;
            instruction->writeBack = 2;
            return;
        case 0b11111000000 :
            instruction->name = "STUR";
            setinst_D(input, instruction);
            instruction->memWrite = true;
            instruction->memRead = false;
            instruction->loadBytes = 1;
            instruction->writeBack = 0;
            return;
        case 0b00111000000 :
            instruction->name = "STURB";
            setinst_D(input, instruction);
            instruction->memWrite = true;
            instruction->memRead = false;
            instruction->loadBytes = 3;
            instruction->writeBack = 0;
            return;
        case 0b01111000000 :
            instruction->name = "STURH";
            setinst_D(input, instruction);
            instruction->memWrite = true;
            instruction->memRead = false;
            instruction->loadBytes = 2;
            instruction->writeBack = 0;
            return;
        case 0b11001011001 :
            instruction->name = "SUB";
            setinst_ex_R(input, instruction);
            return;
        case 0b11001011000 :
            instruction->name = "SUB";
            setinst_sh_R(input, instruction);
            return;
        case 0b11101011001 :
            instruction->name = "SUBS";
            setinst_ex_R(input, instruction);
            return;
        case 0b10011011000 :
            instruction->name = "MUL";
            instruction->type = R_TYPE;
            // rm=20-16, 0, 1=ra=14-10, rn=9-5, rt=4-0
            instruction->rm = takebits(input, 20, 16);
            instruction->rn = takebits(input, 9, 5);
            instruction->rt = takebits(input, 4, 0);
            instruction->valid = true;
            instruction->writeBack = 1;
            instruction->memRead, instruction->memWrite = false;
            return;
        case 0b11010110000 :
            instruction->name = "BR";
            instruction->type = B_TYPE;
            // br: op2=20-16, op3=15-10, rn=9-5, op4=4-0
            instruction->op2 = takebits(input, 20, 16);
            instruction->rn = takebits(input, 9, 5);
            instruction->valid = true;
            return;
    }
    uint32_t op22 = takebits(input, 31, 22); // bits 31 to 22
    switch (op22) {
        case 0b1001000100 :
            instruction->name = "ADD";
            setinst_I(input, instruction);
            return;
        case 0b1011000100 :
            instruction->name = "ADDS";
            setinst_I(input, instruction);
            return;
        case 0b1111000100 :
            setinst_I(input, instruction);
            if (instruction->rt == 0b11111) {
                instruction->name = "CMP";
                instruction->writeBack = 0;
            } else {
                instruction->name = "SUBS";
            }
            return;
        case 0b1101000100 :
            instruction->name = "SUB";
            setinst_I(input, instruction);
            return;
        case 0b1101001101 :
            instruction->type = I_TYPE;
            instruction->writeBack = true;
            instruction->memRead, instruction->memWrite = false;
            // immr=21-16, imms=15-10, rn=9-5, rt=4-0
            instruction->shamt = takebits(input, 21, 16);
            instruction->imm = takebits(input, 15, 10);
            instruction->rn = takebits(input, 9, 5);
            instruction->valid = true;
            instruction->rt = takebits(input, 4, 0);
            if ((instruction->imm == 0b111111) || (instruction->imm == 0b011111)) {
                instruction->name = "LSR";
            } else {
                instruction->name = "LSL";
            }
            return;
    }
    uint32_t op23 = takebits(input, 31, 23); // bits 31 to 23
    switch (op23) {
        case 0b110100101 :
            instruction->valid = true;
            instruction->memRead, instruction->memWrite = false;
            instruction->writeBack = true;
            instruction->name = "MOVZ";
            instruction->type = I_TYPE;
            instruction->hw = takebits(input, 22, 21);
            instruction->imm = takebits(input, 20, 5);
            instruction->rt = takebits(input, 4, 0);
            return;
    }

    uint32_t op24 = takebits(input, 31, 24); // bits 31 to 24
    switch (op24) {
        case 0b10110100 :
            instruction->valid = true;
            instruction->name = "CBZ";
            instruction->type = CB_TYPE;
            instruction->offset = takebits_extend(input, 23, 5);
            instruction->rt = takebits(input, 4, 0);
            return;
        case 0b10110101 :
            instruction->valid = true;
            instruction->name = "CBNZ";
            instruction->type = CB_TYPE;
            instruction->offset = takebits_extend(input, 23, 5);
            instruction->rt = takebits(input, 4, 0);
            return;
        case 0b01010100 :
            instruction->valid = true;
            instruction->type = CB_TYPE;
            instruction->offset = takebits_extend(input, 23, 5);
            instruction->condBR = takebits(input, 3, 0);
            switch (instruction->condBR) {
                case 0b0000 :
                    instruction->name = "BEQ";
                    return;
                case 0b0001 :
                    instruction->name = "BNE";
                    return;
                case 0b1100 :
                    instruction->name = "BGT";
                    return;
                case 0b1011 :
                    instruction->name = "BLT";
                    return;
                case 0b1010 :
                    instruction->name = "BGE";
                    return;
                case 0b1101 :
                    instruction->name = "BLE";
                    return;
            }
    }

    uint32_t op26 = takebits(input, 31, 26); // bits 31 to 26
    switch (op26) {
        case 0b000101 :
            instruction->valid = true;
            instruction->name = "B";
            instruction->type = B_TYPE;
            instruction->offset = takebits_extend(input, 25, 0);
            return;
    }
}

// EXECUTE SECTION

void setflags(int n, instruction* inst)
{
    if (n < 0) {
        inst->FLAG_N = 1;
        inst->FLAG_Z = 0;
    } else if (n == 0) {
        inst->FLAG_N = 0;
        inst->FLAG_Z = 1;
    } else {
        inst->FLAG_N = 0;
        inst->FLAG_Z = 0;
    }
    inst->flagged = true;
}
int64_t exec_R(instruction* inst) {
    int64_t temp;
    if (!inst->forwarded) {
        inst->rmVal = CURRENT_STATE.REGS[inst->rm];
        inst->rnVal = CURRENT_STATE.REGS[inst->rn];
    } else if (inst->forwarded == 1) {
        inst->rmVal = CURRENT_STATE.REGS[inst->rm];
    } else if (inst->forwarded = 2) {
        inst->rnVal = CURRENT_STATE.REGS[inst->rn];
    }

    if (strcmp(inst->name, "ADD") == 0) {
        temp = inst->rmVal + inst->rnVal;
    }
    else if (strcmp(inst->name, "ADDS") == 0) {
        temp = inst->rmVal + inst->rnVal;
        setflags(temp, inst);
    } 
    else if (strcmp(inst->name, "SUB") == 0) {
        temp = inst->rnVal - inst->rmVal;
    }
    else if ((strcmp(inst->name, "SUBS") == 0) || (strcmp(inst->name, "CMP") == 0)) {
        temp = inst->rnVal - inst->rmVal;
        setflags(temp, inst);
    } 
    else if (strcmp(inst->name, "MUL") == 0) {
        temp = inst->rnVal * inst->rmVal;
    }
    else if (strcmp(inst->name, "AND") == 0) {
        temp = inst->rnVal & inst->rmVal;
    }
    else if (strcmp(inst->name, "ANDS") == 0) {
        temp = inst->rnVal & inst->rmVal;
        setflags(temp, inst);
    }
    else if (strcmp(inst->name, "EOR") == 0) {
        temp = inst->rnVal ^ inst->rmVal;
    }
    else if (strcmp(inst->name, "ORR") == 0) {
        temp = inst->rnVal | inst->rmVal;
    }
    return temp;
}

int64_t exec_I(instruction* inst) {
    int64_t temp;

    if (inst->forwarded != 1) {
        inst->rnVal = CURRENT_STATE.REGS[inst->rn];
    }
    if (strcmp(inst->name, "ADD") == 0) {
        temp = inst->rnVal + inst->imm;
    }
    else if (strcmp(inst->name, "ADDS") == 0) {
        temp = inst->rnVal + inst->imm;
        setflags(temp, inst);
    }
    else if (strcmp(inst->name, "SUB") == 0) {
        temp = inst->rnVal - inst->imm;
    }
    else if ((strcmp(inst->name, "SUBS") == 0) || (strcmp(inst->name, "CMP") == 0)) {
        temp = inst->rnVal - inst->imm;
        setflags(temp, inst);
    } 
    else if (strcmp(inst->name, "LSL") == 0) {
        temp = inst->rnVal << (63 - (int64_t)inst->imm);
    }
    else if (strcmp(inst->name, "LSR") == 0) {
        temp = inst->rnVal >> inst->shamt;
    }
    else if (strcmp(inst->name, "MOVZ") == 0) {
        temp = inst->imm;
    }
    return temp;
}

int exec_B(instruction* inst, bool* conditional) {
    if (inst->offset == 0) {
        fprintf(stderr, "Fatal error: offset uninitialized. exec_B.\n");
        exit(1);
    }
    if (inst->forwarded != 3) {
        inst->rtVal = CURRENT_STATE.REGS[inst->rt];
    }

    //printf("BRANCH INSTRUCTION: FLAG_Z: %d, FLAG_N: %d\n", inst->FLAG_Z, inst->FLAG_N);

    *conditional = true;
    bool taken = false;
    if ((strcmp(inst->name, "CBZ") == 0) && (inst->rtVal == 0)) {
        inst->branch_address = inst->offset + inst->current_address;
        taken = true;
    }
    else if ((strcmp(inst->name, "CBNZ") == 0) && (inst->rtVal != 0)) {
        inst->branch_address = inst->offset + inst->current_address;
        taken = true;
    }
    else if ((strcmp(inst->name, "BEQ") == 0) && inst->FLAG_Z) {
        inst->branch_address = inst->offset + inst->current_address;
        taken = true;
    }
    else if ((strcmp(inst->name, "BNE") == 0) && !inst->FLAG_Z) {
        inst->branch_address = inst->offset + inst->current_address;
        taken = true;
    }
    else if ((strcmp(inst->name, "BGT") == 0) && !(inst->FLAG_N || inst->FLAG_Z)) {
        inst->branch_address = inst->offset + inst->current_address;
        taken = true;
    }
    else if ((strcmp(inst->name, "BLT") == 0) && inst->FLAG_N) {
        inst->branch_address = inst->offset + inst->current_address;
        taken = true;
    }
    else if ((strcmp(inst->name, "BGE") == 0) && !inst->FLAG_N) {
        inst->branch_address = inst->offset + inst->current_address;
        taken = true;
    }
    else if ((strcmp(inst->name, "BLE") == 0) && (inst->FLAG_N || inst->FLAG_Z)) {
        inst->branch_address = inst->offset + inst->current_address;
        taken = true;
    }

    // Unconditional Branches
    if (strcmp(inst->name, "B") == 0) {
        inst->branch_address = inst->offset + inst->current_address;
        *conditional = false;
        taken = true;
    }
    else if (strcmp(inst->name, "BR") == 0) {
        inst->branch_address = inst->rtVal+ inst->current_address;
        *conditional = false;
        taken = true;
    }
    return taken;
}
