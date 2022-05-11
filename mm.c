/*
 * segregated free list, LISTLIMIT, segregation_list 포인터
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

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
// size(변수)보다 크면서 가장 가까운 8의 배수로 만들어주는 것이 Align이다. -> 정렬
// size = 7 : (00000111 + 00000111) & 11111000 = 00001110 & 11111000 = 00001000 = 8
// size = 13 : (00001101 + 00000111) & 11111000 = 00010000 = 16
// 즉 1 ~ 7 바이트 -> 8 바이트
// 8 ~ 16 바이트 -> 16 바이트
// 7 ~ 24 바이트 -> 24 바이트
// 여기서 ~는 not 연산자로, 원래 0x7은 0000 0111이고 ~0x7은 1111 1000이 된다.
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

/* 메모리 할당 시 */
// size_t는 '부호 없는 32비트 정수'로 unsigned int이다. 따라서 4바이트이다.
// 따라서 메모리 할당 시 기본적으로 header와 footer가 필요하므로 더블워드만큼의 메모리가 필요하다. size_t이므로 4바이트이니 ALIGN을 거치면 8바이트가 된다.
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* 기본 상수와 매크로 */
#define WSIZE       4                                                       // 워드 사이즈
#define DSIZE       8                                                       // 더블 워드 사이즈
#define CHUNKSIZE   (1<<12)                                                 // 처음 4kB 할당. 초기 free 블록이다.
#define LISTLIMIT   20

#define MAX(x, y) ((x) > (y) ? (x) : (y))                                   // 최댓값을 구하는 함수 매크로

#define PACK(size, alloc) ((size) | (alloc))                                // free 리스트에서 header와 footer를 조작하는 데에는, 많은 양의 캐스팅 변환과 포인터 연산이 사용되기에 애초에 매크로로 만든다.
                                                                            // size 와 alloc을 or 비트 연산시킨다.
                                                                            // 애초에 size의 오른쪽 3자리는 000으로 비어져 있다.
                                                                            // 왜? -> 메모리가 더블 워드 크기로 정렬되어 있다고 전제하기 때문이다. 따라서 size는 무조건 8바이트보다 큰 셈이다.

/* 포인터 p가 가르키는 워드의 값을 읽거나, p가 가르키는 워드에 값을 적는 매크로 */
#define GET(p)          (*(unsigned int *)(p))                              // 보통 p는 void 포인터라고 한다면 곧바로 *p(* 자체가 역참조)를 써서 참조할 수 없기 때문에, 그리고 우리는 4바이트(1워드)씩 주소 연산을 한다고 전제하기에 unsigned int로 캐스팅 변환을 한다. p가 가르키는 곳의 값을 불러온다.
#define PUT(p, val)     (*(unsigned int *)(p) = (val))                      // p가 가르키는 곳에 val를 넣는다.

/* header 혹은 footer의 값인 size or allocated 여부를 가져오는 매크로 */
#define GET_SIZE(p)     (GET(p) & ~0x7)                                     // 블록의 사이즈만 가지고 온다. ex. 1011 0111 & 1111 1000 = 1011 0000으로 사이즈만 읽어옴을 알 수 있다.
#define GET_ALLOC(p)    (GET(p) & 0x1)                                      // 블록이 할당되었는지 free인지를 나타내는 flag를 읽어온다. ex. 1011 0111 & 0000 0001 = 0000 0001로 allocated임을 알 수 있다.

/* 블록 포인터 bp(payload를 가르키고 있는 포인터)를 바탕으로 블록의 header와 footer의 주소를 반환하는 매크로 */
#define HDRP(bp)        ((char *)(bp) - WSIZE)                              // header는 payload보다 앞에 있으므로 4바이트(워드)만큼 빼줘서 앞으로 1칸 전진하게 한다.
#define FTRP(bp)        ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)         // footer는 payload에서 블록의 크기만큼 뒤로 간 다음 8바이트(더블 워드)만큼 빼줘서 앞으로 2칸 전진하게 해주면 footer가 나온다.
                                                                            // 이 때 포인터는 char형이어야 4 혹은 8바이트, 즉 정확히 1바이트씩 움직일 수 있다. 만약 int형으로 캐스팅 해주면 - WSIZE 했을 때 16바이트 만큼 움직일 수도 있다.

/* 블록 포인터 bp를 바탕으로, 이전과 다음 블록의 payload를 가르키는 주소를 반환하는 매크로 */
#define NEXT_BLKP(bp)   ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))   // 지금 블록의 payload를 가르키는 bp에, 지금 블록의 header 사이즈를 읽어서 더하면(word 만큼) 다음 블록의 payload를 가르키게 된다.
#define PREV_BLKP(bp)   ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))   // 지금 블록의 payload를 가르키는 bp에, 이전 블록의 footer 사이즈를 읽어서 뺴면(double word 만큼) 이전 블록의 payload를 가르키게 된다.

/* 블록 포인터 bp가 가리키고 있는 free 블록 안의 prec(predecessor)과 succ(successor)을 반환해주는 매크로 */
// bp가 가리키고 있는 prec 혹은 succ칸에는 또 다른 주소값(포인터)가 담겨져 있다. 따라서 bp는 이중 포인터라고 할 수 있다. 그렇기에 **로 캐스팅해줘야 한다.
// 결국엔 *(bp)인 셈으로 bp가 가리키고 있는 칸의 값이 나오게 되는데, 이 때 주소값이 나오게 된다.(prec 혹은 succ)
#define PREC_FREEP(bp)      (*(void**)(bp))         
#define SUCC_FREEP(bp)      (*(void**)(bp + WSIZE))

/* 
 * global variable & functions
 */
static void* heap_listp;
static void* segregation_list[LISTLIMIT];

static void* extend_heap(size_t words);
static void* coalesce(void *bp);
static void* find_fit(size_t asize);
static void place(void* bp, size_t asize);
static void remove_block(void *bp);
static void insert_block(void *bp, size_t size);

/*
 * mm_init
 */
int mm_init(void) {
    int list;
    
    for (list = 0; list < LISTLIMIT; list++) {
        segregation_list[list] = NULL;
    }
    
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1) {
        return -1;
    }
    
    PUT(heap_listp, 0);
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));
    
    heap_listp += (2 * WSIZE);
    
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL) {
        return -1;
    }
    
    return 0;
}

/*
 * mm_malloc
 */
void *mm_malloc(size_t size) {
    size_t asize;
    size_t extendsize;
    char *bp;
    
    if (size == 0) {
        return NULL;
    }
    
    asize = ALIGN(size + SIZE_T_SIZE);
    
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }
    
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL) {
        return NULL;
    }
    
    place(bp, asize);
    return bp;
}

/*
 * mm_free
 */
void mm_free(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    
    coalesce(bp);
}

/*
 * mm_realloc
 */
void *mm_realloc(void *ptr, size_t size) {
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    newptr = mm_malloc(size);
    if (newptr == NULL) {
        return NULL;
    }
    
    copySize = GET_SIZE(HDRP(oldptr));
    
    if (size < copySize) {
        copySize = size;
    }
    
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

static void *extend_heap(size_t words) {
    char *bp;
    size_t size;
    
    size = (words % 2 == 1) ? (words + 1) * WSIZE : (words) * WSIZE;
    
    if ((long)(bp = mem_sbrk(size)) == -1) {
        return NULL;
    }
    
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    
    return coalesce(bp);
}

static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));
    
    if (prev_alloc && next_alloc) {
        insert_block(bp, size);
        return bp;
    }
    
    else if (prev_alloc && !next_alloc) {
        remove_block(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    
    else if (!prev_alloc && next_alloc) {
        remove_block(PREV_BLKP(bp));
        
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size , 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    
    else if (!prev_alloc && !next_alloc) {
        remove_block(PREV_BLKP(bp));
        remove_block(NEXT_BLKP(bp));
        
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    
    insert_block(bp, size);
    return bp;
}

static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));
    remove_block(bp);
    
    if ((csize - asize) >= (2 * DSIZE)) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize, - asize, 0));
        coalesce(bp);
    }
    
    else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

static void *find_fit(size_t asize) {
    void* bp;
    
    int list = 0;
    size_t searchsize = asize;
    
    while (list < LISTLIMIT) {
        if 
    }
}

