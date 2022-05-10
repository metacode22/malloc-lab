/*
 * Malloc using explicit free list with first-fit
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
#define WSIZE               4                                               // 워드 사이즈
#define DSIZE               8                                               // 더블 워드 사이즈
#define MINIMUM             16
#define CHUNKSIZE           (1<<12)                                         // 처음 4kB 할당. 초기 free 블록이다.

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
static char* heap_listp;                                                    // 항상 prologue block을 가리키는 정적 전역 변수 설정. static 변수는 함수 내부(지역)에서도 사용이 가능하고 함수 외부(전역)에서도 사용이 가능하다.
static char* free_listp;                                                    // free list의 맨 첫 블록을 가리키는 포인터이다.

/* 코드 순서상, implicit declaration of function(warning)을 피하기 위해 미리 선언해주는 부분? */
static void* extend_heap(size_t words);
static void* coalesce(void* bp);
static void* find_fit(size_t asize);
static void place(void* bp, size_t newsize);

int mm_init(void);
void *mm_malloc(size_t size);
void mm_free(void *bp);
void *mm_realloc(void *ptr, size_t size);

/*
 * mm_init
 */
int mm_init(void) {
    
    // unused padding, prologue header/prologue footer, prec, succ, epilogue_header 총 6개가 필요하다.
    if ((heap_listp = mem_sbrk(6*WSIZE)) == (void*)-1) {                    // 할당 실패 시 -1을 반환한다.
        return -1;
    }
    
    PUT(heap_listp, 0);                                                     // unused padding
    PUT(heap_listp + (1 * WSIZE), PACK(MINIMUM, 1));                        // prologue header
    PUT(heap_listp + (2 * WSIZE), NULL);                                    // prec
    PUT(heap_listp + (3 * WSIZE), NULL);                                    // succ
    PUT(heap_listp + (4 * WSIZE), PACK(MINIMUM, 1));                        // prologue footer
    PUT(heap_listp + (5 * WSIZE), PACK(0, 1));                              // epilogue header
    
    free_listp = heap_listp + 2 * WSIZE;                                    // free_listp를 탐색하는 메커니즘이다.
    
    // CHUCKSIZE만큼 힙을 확장해 초기 free 블록을 생성한다. 이 때 CHUCKSIZE는 2^12으로 4kB 정도였다.(4096 bytes)
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL) {                           // 곧바로 extend_heap이 실행된다.
        return -1;
    }
    
    return 0;
}

/*
 *  extend_heap - word 단위의 메모리를 인자로 받아 힙을 늘려준다.  
 */
static void* extend_heap(size_t words) {
    char* bp;
    size_t size;
    
    size = (words % 2 == 1) ? (words + 1) * WSIZE : (words) * WSIZE;        // words가 홀수로 들어왔다면 짝수로 바꿔준다. 짝수로 들어왔다면 그대로 WSIZE를 곱해준다. ex. 5만큼(5개의 워드 만큼) 확장하라고 하면, 6으로 만들고 24바이트로 만든다. 
                                                                            // 8바이트(2개 워드, 짝수) 정렬을 위해 짝수로 만들어줘야 한다.
    
    if ((long)(bp = mem_sbrk(size)) == -1) {                                // 변환한 사이즈만큼 메모리 확보에 실패하면 NULL이라는 주소값을 반환해 실패했음을 알린다. bp 자체의 값, 즉 주소값이 32bit이므로 long으로 캐스팅한다.
        return NULL;                                                        // 그리고 mem_sbrk 함수가 실행되므로 bp는 새로운 메모리의 첫 주소값을 가르키게 된다.
    }              
    
    // 새 free 블록의 header와 footer를 정해준다. 자연스럽게 전 epilogue 자리에는 새로운 header가 자리 잡게 된다. 그리고 epilogue는 맨 뒤로 보내지게 된다.
    PUT(HDRP(bp), PACK(size, 0));                                           // 새 free 블록의 header로, free 이므로 0을 부여
    PUT(FTRP(bp), PACK(size, 0));                                           // 새 free 블록의 footer로, free 이므로 0을 부여
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));                                   // 앞에서 현재 bp(새롭게 늘어난 메모리의 첫 주소 값으로 역시 payload이다)의 header에 값을 부여해주었다. 따라서 이 header의 사이즈 값을 참조해 다음 블록의 payload를 가르킬 수 있고, 이 payload의 직전인 header는 epilogue가 된다.
    
    return coalesce(bp);                                                    // 앞 뒤 블록이 free 블록이라면 연결하고 bp를 반환한다.
}


/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));                                       // bp가 가리키는 블록의 사이즈만 들고 온다.
    
    // header, footer 둘 다 flag를 0으로 바꿔주면 된다.
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));                      
    
    coalesce(bp);                                                           // 앞 뒤 블록이 free 블록이라면 연결한다.                   
}

/*
 * coalesce - 이전 혹은 다음 블록이 free이면 연결시키고, 경우에 따라 free 리스트에서 제거하고 새로워진 free 블록을 free 리스트에 추가한다.
 */
static void* coalesce(void* bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));                     // 이전 블록의 free 여부
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));                     // 다음 블록의 free 여부
    size_t size = GET_SIZE(HDRP(bp));                                       // 현재 블록의 사이즈
    
    // 경우 1. 이전 블록 할당, 다음 블록 할당 - 연결시킬 수 없으니 그대로 bp를 반환한다.
    if (prev_alloc && next_alloc) {
        putFreeBlock(bp);
        return bp;
    }
    
    else if (prev_alloc && !next_alloc) {
        removeBlock(NEXT_BLKP(bp));                                         // free 상태였던 다음 블록을 free 리스트에서 제거한다.
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    
    else if (!prev_alloc && next_alloc) {
        removeBlock(PREV_BLKP(bp));                                         // free 상태였던 이전 블록을 free 리스트에서 제거한다.
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    
    else {
        removeBlock(PREV_BLKP(bp));                                         // free 상태였던 이전 블록을 free 리스트에서 제거한다.
        removeBlock(NEXT_BLKP(bp));                                         // free 상태였던 다음 블록을 free 리스트에서 제거한다.
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    
    // 연결되어진 새로운 free 블록을 free 리스트에 추가한다.
    putFreeBlock(bp);
    
    return bp;
}

/*
 * mm_malloc
 */
void *mm_malloc(size_t size) {
    size_t asize;                                                           // 수정된 블록의 크기
    size_t extendsize;                                                      // 알맞은 크기의 free 블록이 없을 시 확장하는 사이즈
    char *bp;
    
    if (size == 0) {
        return NULL;                                                        // 가짜 요청은 무시한다.
    }
    
    asize = ALIGN(size + SIZE_T_SIZE);                                      // header와 footer를 위한 메모리, 즉 word 2개가 필요하므로 SIZE_T_SIZE만큼의 메모리가 필요하다. 여기에 현재 할당하려는 size를 더하면, header와 footer가 포함되면서 할당하려는 블록의 크기가 된다.
    
    // 적절한 공간을 가진 블록을 찾으면 할당(혹은 분할까지) 진행한다.
    // bp는 계속 free 블록을 가리킬 수 있도록 한다.
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }
    
    // 적절한 공간을 찾지 못했다면 힙을 늘려주고, 그 늘어난 공간에 할당시켜야 한다.
    extendsize = MAX(asize, CHUNKSIZE);                                     // 둘 중 더 큰 값을 선택한다.
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL) {                   // 실패 시 bp로는 NULL을 반환한다.
        return NULL;
    }
    
    // 힙을 늘리는 데에 성공했다면, 그 늘어난 공간에 할당시킨다.
    place(bp, asize);
    return bp;
}

/*
 * find_fit - first-fit, free 리스트의 맨 처음부터 탐색하여 요구하는 메모리 공간보다 큰 free 블록의 주소를 반환한다.
 */
static void* find_fit(size_t asize) {
    void* bp;
    
    // free 리스트의 맨 마지막은 할당되어진 prologue 블록(정확히는 payload를 가리키는, free 블록이었으면 prev이었을 워드를 가리키고 있다)이다.
    for (bp = free_listp; GET_ALLOC(HDRP(bp)) != 1; bp = SUCC_FREEP(bp)) {
        if (asize <= GET_SIZE(HDRP(bp))) {
            return bp;
        }
    }
    
    return NULL;
}

/*
 * place - 요구 메모리를 할당할 수 있는 가용 블록을 할당한다.(즉 실제로 할당하는 부분이다) 이 때 분할이 가능하다면 분할한다.
 */
static void place(void* bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));                                      // 현재 할당할 수 있는 후보, 즉 실제로 할당할 free 블록의 사이즈
    
    // 해당 블록을 할당해야 하므로 free 리스트에서 제거한다.
    removeBlock(bp);
    
    // 분할이 가능한 경우
    // 할당하고 남은 메모리가 free 블록을 만들 수 있는 4개의 word가 되느냐
    // header/footer/prec/next가 필요하니 최소 4개의 word는 필요하다.
    if ((csize - asize) >= (2 * DSIZE)) {
        // 앞의 블록은 할당시킨다.
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        
        // 뒤의 블록은 free시킨다.
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
        
        // free 리스트의 첫 번째에 분할된, 즉 새롭게 수정된 free 블록이 추가된다.
        putFreeBlock(bp);
        
    // 분할이 불가능한 경우
    // csize - asize가 2 * DSIZE보다 작다는 것은 할당되고 남은 공간에 header/footer/prec/next가 들어갈 자리가 충분치 않음을 의미한다. 최소한의 크기를 가지는 free 블록을 만들 수 없으므로 어쩔 수 없이 주소 정렬을 위해 내부 단편화를 진행한다.
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/*
 * removeBlock - 할당되거나, 이전 혹은 다음 블록과 연결되어지는 free 블록은 free 리스트에서 제거해야 한다.
 */
void removeBlock(void *bp) {
    // free 리스트의 첫 번째 블록을 없앨 때
    // ex. (참고로 PREC, SUCC word 안에는 주소값, 즉 포인터가 들어있다는 것을 유심해야 한다.)
    // 0x72 <-> 0x24 <-> 0x08   맨 처음, bp(free_listp)가 0x72를 가리키고 있다고 가정.
    // 0x72 -> 0x24 <-> 0x08    PREC_FREEP(SUCC_FREEP(bp)) = NULL;
    // 0x24 <-> 0x08            free_listp는 0x24가 되면 앞의 0x72는 이제 완전히 날라가게 된다.
    if (bp == free_listp) {                                                 // bp가 free_listp라는 말은 free 리스트의 처음이라는 뜻이다.
        PREC_FREEP(SUCC_FREEP(bp)) = NULL;                                  // bp가 가리키는 free 블록의 바로 다음 블록에서 이전 블록을 잇는 prec 블록의 값을 NULL로 수정하면 끊어지게 된다.
        free_listp = SUCC_FREEP(bp);                                        // bp가 가리키는 free 블록의 바로 다음 블록이 free_listp, 즉 free 리스트의 맨 처음이 되도록 한다.
        
    // bp가 free 리스트의 맨 처음을 가리키는 것이 아니라, free 리스트 안의 블록을 가리키고 있을 때, 해당 블록을 없앴다고 가정하고 (free 리스트 안에서) 앞 뒤의 블록을 이어주면 된다.
    // ex. (참고로 PREC, SUCC word 안에는 주소값, 즉 포인터가 들어있다는 것을 유심해야 한다.)
    // 0x72 <-> 0x24 <-> 0x08   bp가 free_listp가 아닌 0x24를 가리키고 있다고 가정.
    // 0x72 -> 0x08             SUCC_FREEP(PREC_FREEP(bp)) = SUCC_FREEP(bp);
    // 0x72 <-> 0x08            PREC_FREEP(SUCC_FREEP(bp)) = PREC_FREEP(bp);
    } else {
        SUCC_FREEP(PREC_FREEP(bp)) = SUCC_FREEP(bp);
        PREC_FREEP(SUCC_FREEP(bp)) = PREC_FREEP(bp);
    }
}

/*
 * putFreeBlock - free 되거나, 연결되어 새롭게 수정된 free 블록을 free 리스트의 맨 처음에 넣는다.
 */
void putFreeBlock(void* bp) {
    SUCC_FREEP(bp) = free_listp;                                            // 이제 bp 블록의 다음은 free_listp가 되게 된다.
    PREC_FREEP(bp) = NULL;                                                  // free 리스트의 맨 처음 블록의 이전 블록은 당연히 NULL이어야 한다.
    PREC_FREEP(free_listp) = bp;                                            // free_listp, 즉 bp의 다음 블록의 이전(PREC)이 bp를 향하도록 한다. free_listp가 밀려난 셈이니까.
    free_listp = bp;                                                        // 이제 free 리스트의 맨 처음을 가리키는 포인터인 free_listp를 bp로 바꿔준다. 이제 bp는 완벽히 free 리스트의 맨 처음이 되었다.
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size) {
    // 블록의 크기를 줄이는 것이면 줄이려는 size만큼으로 줄인다.
    // 블록의 크기를 늘리는 것이면 
    // 핵심은, 이미 할당된 블록의 사이즈를 직접 건드리는 것이 아니라, 요청한 사이즈 만큼의 블록을 새로 메모리 공간에 만들고 현재의 블록을 반환하는 것이다.
    // 해당 블록의 사이즈가 이 정도로 변경되었으면 좋겠다는 것이 size, 
    void *oldptr = ptr;                                                     // 크기를 조절하고 싶은 힙의 시작 포인터
    void *newptr;                                                           // 크기 조절 뒤의 새 힙의 시작 포인터
    size_t copySize;                                                        // 복사할 힙의 크기
    
    newptr = mm_malloc(size);                                               // place를 통해 header, footer가 배정된다.
    if (newptr == NULL) {
        return NULL;
    }
    
    copySize = GET_SIZE(HDRP(oldptr));                                      // 원래 블록의 사이즈
    
    if (size < copySize) {                                                  // 만약 블록의 크기를 줄이는 것이라면 size만큼으로 줄이면 된다. copySize - size 공간의 데이터는 잘리게 된다. 밑의 memcpy에서 잘린 만큼의 데이터는 복사되지 않는다.
        copySize = size;
    }
    
    memcpy(newptr, oldptr, copySize);                                       // oldptr부터 copySize까지의 데이터를, newptr부터 심겠다.
    mm_free(oldptr);                                                        // 기존 oldptr은 반환한다.
    return newptr;
}