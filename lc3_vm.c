#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>

/* 65536 locations, RAM is 64K * 16bit / 2 = 128KB */
uint16_t memory[UINT16_MAX];

/* whether the program is running or not */
static bool running;

/* LC-3 have 10 registers, each size is 16 bit
  * R0-R7 : general registers
  * PC : program couter register
  * COND : condition flags register */
 enum {
     R_R0 = 0,
     R_R1,
     R_R2,
     R_R3,
     R_R4,
     R_R5,
     R_R6,
     R_R7,
     R_PC,     /* program counter */
     R_COND,
     R_COUNT
 };
 uint16_t reg[R_COUNT];


/* LC-3 instruction format 
  * bit0 bit1 bit2 bit3    |    bit4 bit5 bit6 bit7 bit8 bit9 bit10 bit11 bit12 bit13 bit14 bit15
  *           opcode                                                                       parameter
  * LC-3 is RISC */
 enum {
     OP_BR = 0,    /* branch */
     OP_ADD,        /* add */
     OP_LD,           /* load */
     OP_ST,           /* store */
     OP_JSR,        /* jump register */
     OP_AND,       /* bitwise and */
     OP_LDR,        /* load register */
     OP_STR,        /* store register */
     OP_RTI,         /* unused */
     OP_NOT,       /* bitwise not */
     OP_LDI,         /* load indirect */
     OP_STI,         /* store indirect */
     OP_JMP,       /* jump */
     OP_RES,        /* reserved(unused) */
     OP_LEA,        /* load effective address */
     OP_TRAP      /* execute trap */
 };


 /* Condition flags register -- condition flag */
 enum {
     FL_POS = 1 << 0,    /* positive */
     FL_ZRO = 1 << 1,    /* zero */
     FL_NEG = 1 << 2     /* negative */
 };


/* the default program start position : 0x3000*/
 enum { PC_START = 0x3000 };

 /* trap code */
 enum {
     TRAP_GETC = 0x20,      /* get character from keyboard, not echoed onto the terminal */
     TRAP_OUT = 0x21,        /* output a character */
     TRAP_PUTS = 0x22,      /* output a word string */
     TRAP_IN = 0x23,             /* get character from keyboard, echoed onto the terminal */
     TRAP_PUTSP = 0x24,    /* output a byte string */
     TRAP_HALT = 0x25         /* halt the program */
 };

 /* device register */
 enum {
     MR_KBSR = 0xFE00,      /* Keyboard status register */
     MR_KBDR = 0xFE02       /* Keyboard data register */
 };

 static struct termios original_tio;


/*****************************************************************************/
bool check_key()
{
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(STDIN_FILENO, &read_fds);

    struct  timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &read_fds, NULL, NULL, &timeout) != 0;
}

void mem_write(uint16_t address, uint16_t val)
{
    memory[address] = val;
}

uint16_t mem_read(uint16_t address)
{
    if (address == MR_KBSR)
    {
        if (check_key())
        {
            memory[MR_KBSR] = (1 << 15);
            memory[MR_KBDR] = getchar();
        }
        else
            memory[MR_KBSR] = 0;        
    }
    return memory[address];
}

uint16_t sign_extend(uint16_t x, int bit_count)
{
    if ((x >> (bit_count - 1)) & 0x1)
        x |= 0xFFFF << bit_count;

    return x;
}

void update_flags(uint16_t reg_index)
{
    if (reg[reg_index] == 0)
        reg[R_COND] = FL_ZRO;
    else if (reg[reg_index] >> 15)
        reg[R_COND] = FL_NEG;
    else
        reg[R_COND] = FL_POS;
}

void disable_input_buffering()
{
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}


/*****************************************************************************/
/* OP_ADD */
void op_add(uint16_t instr)
{
    uint16_t dst_reg = (instr >> 9) & 0x7;
    uint16_t src_reg1 = (instr >> 6) & 0x7;
    bool imm5_flag = (instr >> 5) & 0x1;

    if (imm5_flag)
    {
        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
        reg[dst_reg] = reg[src_reg1] + imm5;
    }
    else
    {
        uint16_t src_reg2 = instr   & 0x7;
         reg[dst_reg] = reg[src_reg1] +  reg[src_reg2];
    }
    update_flags(dst_reg);
}

/* OP_AND */
void op_and(uint16_t instr)
{
    uint16_t dst_reg = (instr >> 9) & 0x7;
    uint16_t src_reg1 = (instr >> 6) & 0x7;
    bool imm5_flag = (instr >> 5) & 0x1;

    if (imm5_flag)
    {
        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
        reg[dst_reg] = reg[src_reg1] & imm5;
    }
    else
    {
        uint16_t src_reg2 = instr & 0x7;
        reg[dst_reg] = reg[src_reg1] & reg[src_reg2];
    }   
    update_flags(dst_reg);
}

/* OP_NOT */
void op_not(uint16_t instr)
{
    uint16_t dst_reg = (instr >> 9) & 0x7;
    uint16_t src_reg = (instr >> 6) & 0x7;

    reg[dst_reg] = ~reg[src_reg];
    update_flags(dst_reg);
}

/* OP_BR */
void op_br(uint16_t instr)
{
    uint16_t pc_offset9 =  sign_extend(instr & 0x1FF, 9);
    uint16_t cond_flag = (instr >> 9) & 0x7;
    if (cond_flag & reg[R_COND])
        reg[R_PC] += pc_offset9;
}

/* OP_JMP */
void op_jmp(uint16_t instr)
{
    uint16_t base_reg = (instr >> 6) & 0x7;
    reg[R_PC] = reg[base_reg];
}

/* OP_JSR */
void op_jsr(uint16_t instr)
{
    uint16_t long_flag = (instr >> 11) & 0x1;

    reg[R_R7] = reg[R_PC];
    if (long_flag)
    {
        uint16_t pc_offset11 = sign_extend(instr & 0x7FF, 11);
        reg[R_PC] += pc_offset11;
    }
    else
    {
        uint16_t base_reg = (instr >> 6) & 0x7;
        reg[R_PC] = reg[base_reg];
    }
}

/* OP_LD */
void op_ld(uint16_t instr)
{
    uint16_t dst_reg = (instr >> 9) & 0x7;
    uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);

    reg[dst_reg] = mem_read(reg[R_PC] + pc_offset9);
    update_flags(dst_reg);
}

/* OP_LDI */
void op_ldi(uint16_t instr)
{
    uint16_t dst_reg = (instr >> 9) & 0x7;
    uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);

    uint16_t address = mem_read(reg[R_PC] + pc_offset9);
    reg[dst_reg] = mem_read(address);
    update_flags(dst_reg);
}

/* OP_LDR */
void op_ldr(uint16_t instr)
{
    uint16_t dst_reg = (instr >> 9) & 0x7;
    uint16_t base_reg = (instr >> 6) & 0x7;
    uint16_t pc_offset6 = sign_extend(instr & 0x3F, 6);

    reg[dst_reg] = mem_read(reg[base_reg] + pc_offset6);
    update_flags(dst_reg);
}

/* OP_LEA */
void op_lea(uint16_t instr)
{
    uint16_t dst_reg = (instr >> 9) & 0x7;
    uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);

    reg[dst_reg] = reg[R_PC] + pc_offset9;
    update_flags(dst_reg);
}

/* OP_ST */
void op_st(uint16_t instr)
{
    uint16_t src_reg = (instr >> 9) & 0x7;
    uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);
    
    uint16_t address = reg[R_PC] + pc_offset9;
    mem_write(address, reg[src_reg]);
}

/* OP_STI */
void op_sti(uint16_t instr)
{
    uint16_t src_reg = (instr >> 9) & 0x7;
    uint16_t pc_offset9 = sign_extend(instr & 0x1FF, 9);

    uint16_t address = mem_read(reg[R_PC] + pc_offset9);
    mem_write(address, reg[src_reg]);
}

/* OP_STR */
void op_str(uint16_t instr)
{
    uint16_t src_reg = (instr >> 9) & 0x7;
    uint16_t base_reg = (instr >> 6) & 0x7;
    uint16_t pc_offset6 = sign_extend(instr & 0x3F, 6);

    uint16_t address = reg[base_reg] + pc_offset6;
    mem_write(address, reg[src_reg]);
}


/****************************** Trap Routine *********************************/
/* TRAP_GETC */
void trap_getc()
{
    reg[R_R0] = (uint16_t)getchar();
}

/* TRAP_OUT */
void trap_out()
{
    putc((char)reg[R_R0], stdout);
    fflush(stdout);
}

/* TRAP_PUTS */
void trap_puts()
{
    /* one char per word */
    uint16_t *ptr_c = memory + reg[R_R0];
    while ( *ptr_c)
    {
        putc((char)*ptr_c, stdout);
        ++ptr_c;
    }
    fflush(stdout);
}

/* TRAP_IN */
void trap_in()
{
    printf("Enter a character:\n");
    
    char ch = getchar();
    putc(ch, stdout);
    reg[R_R0] = (uint16_t)ch;
}

/* TRAP_PUTSP */
void trap_putsp()
{
    /* two char per word, here we need to swap back to big endian format */
    uint16_t *ptr_c = memory + reg[R_R0];
    while ( *ptr_c)
    {
        char ch1 = (*ptr_c) & 0xFF;
        putc(ch1, stdout);
        char ch2 = (*ptr_c) >> 8;
        if (ch2)
            putc(ch2, stdout);
        
        ++ptr_c;
    }
    fflush(stdout);
}

void trap_halt()
{
    puts("HATL");
    fflush(stdout);
    running = false;
}

/* OP_TRAP */
void op_trap(uint16_t instr)
{
    switch (instr & 0xFF)
    {
    case TRAP_GETC:
        trap_getc();
        break;
    case TRAP_OUT:
        trap_out();
        break;
    case TRAP_PUTS:
        trap_puts();
        break;
    case TRAP_IN:
        trap_in();
        break;
    case TRAP_PUTSP:
        trap_putsp();
        break;
    case TRAP_HALT:
        trap_halt();
        break;
    default:
        break;
    }
}


/*****************************************************************************/
/* swap big-endian to little-endian */
uint16_t swap16(uint16_t x)
{
    return (x << 8) | (x >> 8);
}

void read_image_file(FILE *file)
{
    /* the first 16 bit tell us where in memory to place the image */
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);

    /* we know the maximum file size so we only need  one fread */
    uint16_t max_read = UINT16_MAX - origin;
    uint16_t *p_curr = memory + origin;
    size_t actual_read = fread(p_curr, sizeof (uint16_t), max_read, file);

    while (actual_read-- > 0)
    {
        *p_curr = swap16(*p_curr);
        ++p_curr;
    }
}

bool read_image(const char *image_path)
{
    FILE *image_file = fopen(image_path, "rb");
    if (image_file == NULL)
        return false;
    read_image_file(image_file);
    fclose(image_file);

    return true;
}


/*****************************************************************************/
 int main(int argc, const char* argv[])
 {
    if (argc < 2)
        return 0;
    read_image(argv[1]);
    
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

    /* Set the PC to starting position */
    reg[R_PC] = PC_START;

    running = true;
    while (running)
    {
         uint16_t instr = mem_read(reg[R_PC]++);
         uint16_t op = instr >> 12;
         switch (op)
         {
         case OP_ADD:
             op_add(instr);
             break;
         case OP_AND:
             op_and(instr);
             break;
         case OP_NOT:
             op_not(instr);
             break;
         case OP_BR:
             op_br(instr);
             break;
         case OP_JMP:
            op_jmp(instr);
             break;
         case OP_JSR:
            op_jsr(instr);
             break;
         case OP_LD:
            op_ld(instr);
             break;
         case OP_LDI:
             op_ldi(instr);
             break;
         case OP_LDR:
            op_ldr(instr);
             break;
         case OP_LEA:
            op_lea(instr);
             break;
         case OP_ST:
             op_st(instr);
             break;
         case OP_STI:
             op_sti(instr);
             break;
         case OP_STR:
            op_str(instr);
             break;
         case OP_TRAP:
            op_trap(instr);
             break;
         case OP_RES:
         case OP_RTI:
         default:
             abort();
             break;
         }
    }
    /* Shutdown */
    restore_input_buffering();

    return 0;
 }