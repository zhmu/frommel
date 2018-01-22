/*
 * Frommel 2.0
 *
 * (c) 2001, 2002 Rink Springer
 * 
 */
#include <sys/types.h>
#include <netipx/ipx.h>
#include "config.h"

#ifndef __CONSOLE_H__
#define __CONSOLE_H__

// CON_MAX_LINE_LEN is the maximum length of a single line
#define CON_MAX_LINE_LEN  1024
#define CON_MAX_CMD_LEN   32
#define CON_MAX_DESC_LEN  128

typedef struct {
    char command[CON_MAX_CMD_LEN];
    char desc[CON_MAX_DESC_LEN];
    void (*func)(char*);
} CON_COMMAND;

void	con_init();
void	con_go();
#endif
