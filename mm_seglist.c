/*
 * seglist
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * Information of my team
 ********************************************************/
team_t team = {
    /* Team name */
    "jungle",
    /* First member's full name */
    "Shin Seung Jun",
    /* First member's email address */
    "alohajune22@gmail.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

#define ALIGNMENT 8

#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1<<12)
#define INITCHUNKSIZE (1<<6)
#define LISTLIMIT 20
#define REALLOC_BUFFER (1<<7)