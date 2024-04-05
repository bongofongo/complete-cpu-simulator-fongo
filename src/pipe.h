/*
 * CMSC 22200
 *
 * ARM pipeline timing simulator
 */

#ifndef _PIPE_H_
#define _PIPE_H_

#include "shell.h"
#include "stdbool.h"
#include <limits.h>

// SIM.c stuff

typedef enum {
    R_TYPE, // opcode, Rm, shamt, Rn, Rd
    B_TYPE, // opcode, offset, Rt
    D_TYPE, // opcode, offset, op2, Rn, Rt
    CB_TYPE, // opcode, offset, Rt
    I_TYPE, // opcode, imm, Rn, Rt, shamt
    NO_TYPE
} inst_type;

//instruction carries all information along the pipeline
typedef struct instruction {
    char *name; 
    inst_type type;
    //  Decode Information

    uint32_t rn, rm, rt; // rd = destination reg, rt for targeted reg
    int64_t rnVal, rmVal, rtVal;
    uint32_t opt; // option
    uint32_t imm; // immediate value for calculations
    uint32_t shamt; // shift amount OR load offset
    uint32_t BR;
    int64_t offset; // branch offsets for PC
    uint32_t condBR;
    uint32_t hw; // in MOVZ
    uint32_t op2;
    bool hltInst;
    bool hit;
    bool valid;

    //IFtoDE info
    uint32_t fetched_instruction;
    uint64_t next_address; // address
    uint64_t current_address;
    uint64_t branch_address;
    bool flagged;
    int forwarded; // 0 none, 1 rn, 2 rm, 3 rt;

    //MEM, WBinfo
    uint64_t effective_address;
	int64_t ALU_out;
    int64_t mem_out;
    bool memWrite;
    bool memRead;
    int writeBack; // 0: don't write, 1: WB ALU, 2: WB mem_val
    int loadBytes; // 0 = not a load function. 1 = Read 32, 2 = read 16, 3 = read 8 bits;

    // Cache Info

    bool FLAG_N;
    bool FLAG_Z;
} instruction;

typedef struct CPU_State {
	/* register file state */
	int64_t REGS[ARM_REGS];
	int FLAG_N;        /* flag N */
	int FLAG_Z;        /* flag Z */

	/* program counter in fetch stage */
	uint64_t PC;
	
} CPU_State;

typedef struct PIPE {
    instruction* IFtoDE;
    instruction* DEtoEX;
    instruction* EXtoMEM;
    instruction* MEMtoWB;
    int stall;
    int halt;
    int btaken;
    int flush;
    // Cache Things
    int fetch_stall;
    bool missPending;
    uint64_t missAddress;
    int lineNumber;
    int memStall;
} PIPE;

int RUN_BIT;

extern PIPE* pipe;

/* global variable -- pipeline state */
extern CPU_State CURRENT_STATE;

/* called during simulator startup */
void pipe_init();

/* this function calls the others */
void pipe_cycle();

/* each of these functions implements one stage of the pipeline */
void pipe_stage_fetch();
void pipe_stage_decode();
void pipe_stage_execute();
void pipe_stage_mem();
void pipe_stage_wb();


#endif
void exep_help(PIPE p);

void mem_hazard();
void ex_hazard();
void hazard_detection_unit();

int64_t sign_extend(uint64_t value, unsigned int n);
uint32_t takebits(uint32_t input, uint32_t a, uint32_t b);
int64_t takebits_extend(unsigned int input, unsigned int a, unsigned int b);
void setinst_ex_R(uint32_t input, instruction *inst);
void setinst_sh_R(uint32_t input, instruction *inst);
void setinst_I(uint32_t input, instruction *inst);
void setinst_D(uint32_t input, instruction *inst);
void decode(uint32_t input, instruction *instruction);

void setflags(int n, instruction* inst);
int64_t exec_R(instruction* inst);
int64_t exec_I(instruction* inst);
int exec_B(instruction* inst, bool* conditional);
