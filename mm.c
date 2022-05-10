/*
 * Malloc using implicit free list with first-fit or next-fit
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

/* define searching method for find suitable free blocks to allocate */
#define NEXT_FIT                                                            // define하면 next_fit, 안하면 first_fit으로 탐색한다.

/* global variable & functions */
static char* heap_listp;                                                    // 항상 prologue block을 가리키는 정적 전역 변수 설정
                                                                            // static 변수는 함수 내부(지역)에서도 사용이 가능하고 함수 외부(전역)에서도 사용이 가능하다.
        
                                                                    
#ifdef NEXT_FIT                                                             // #ifdef ~ #endif를 통해 조건부로 컴파일이 가능하다. NEXT_FIT이 선언되어 있다면 밑의 변수를 컴파일 할 것이다.
    static void* last_freep;                                                // next_fit 사용 시 마지막으로 탐색한 free 블록을 가리키는 포인터이다.
#endif

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
 * mm_init - initialize the malloc package.
 */
int mm_init(void) {
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)                   // memlib.c를 살펴보면 할당 실패시 (void *)-1을 반환하고 있다. 정상 포인터를 반환하는 것과는 달리, 오류 시 이와 구분 짓기 위해 mem_sbrk는 (void *)-1을 반환하고 있다.
        return -1;                                                          // 할당에 실패하면 -1을 리턴한다.
        
    PUT(heap_listp, 0);                                                     // Alignment padding으로 unused word이다. 맨 처음 메모리를 8바이트 정렬(더블 워드)을 위해 사용하는 미사용 패딩이다.
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));                          // prologue header로, 맨 처음에서 4바이트 뒤에 header가 온다. 이 header에 사이즈(프롤로그는 8바이트)와 allocated 1(프롤로그는 사용하지 말라는 의미)을 통합한 값을 부여한다.
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));                          // prologue footer로, 값은 header와 동일해야 한다.
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));                              // epilogue header
    
    heap_listp += (2 * WSIZE);                                              // heap_listp는 prologue footer를 가르키도록 만든다.
    
    #ifdef NEXT_FIT
        last_freep = heap_listp;                                            // next_fit 사용 시 마지막으로 탐색한 free 블록을 가리키는 포인터이다.
    #endif
    
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
 * coalesce - 앞 혹은 뒤 블록이 free 블록이고, 현재 블록도 free 블록이라면 연결시키고 연결된 free 블록의 주소를 반환한다.
 */ 
static void* coalesce(void* bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));                     // 이전 블록의 free 여부
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));                     // 다음 블록의 free 여부
    size_t size = GET_SIZE(HDRP(bp));                                       // 현재 블록의 사이즈
    
    // 경우 1. 이전 블록 할당, 다음 블록 할당 - 연결시킬 수 없으니 그대로 bp를 반환한다.
    if (prev_alloc && next_alloc) {
        return bp;
    }
    
    // 경우 2. 이전 블록 할당, 다음 블록 free - 다음 블록과 연결시키고 현재 bp를 반환하면 된다.
    else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));                              // 다음 블록의 header에 저장된 다음 블록의 사이즈를 더해주면 연결된 만큼의 사이즈가 나온다.
        PUT(HDRP(bp), PACK(size, 0));                                       // 현재 블록의 header에 새로운 header가 부여된다.
        PUT(FTRP(bp), PACK(size, 0));                                       // 다음 블록의 footer에 새로운 footer가 부여된다.
    }
    
    // 경우 3. 이전 블록 free, 다음 블록 할당 - 이전 블록과 연결시키고 이전 블록을 가리키도록 bp를 바꾼다.
    else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));                                       // 현재 블록의 footer에 새로운 footer를 부여한다.
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));                            // 이전 블록의 header에 새로운 header를 부여한다.
        bp = PREV_BLKP(bp);                                                 // bp가 이전 블록을 가리키도록 한다.
    }
    
    // 경우 4. 이전 블록 free, 다음 블록 free - 모두 연결한 후 이전 블록을 가리키도록 bp를 바꾼다.
    else {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));  // 이전 블록의 header에서 사이즈를, 다음 블록의 footer에서 사이즈를 읽어와 size를 더한다.
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));                            // 이전 블록의 header에 새로운 header를 부여한다.
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));                            // 다음 블록의 footer에 새로운 footer를 부여한다.
        bp = PREV_BLKP(bp);
    }
    
    #ifdef NEXT_FIT
        last_freep = bp;
    #endif
    
    return bp;
}


/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
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
 * find_fit - 힙을 탐색하여 요구하는 메모리 공간보다 큰 가용 블록의 주소를 반환한다.
 */
static void* find_fit(size_t asize) {
    // next-fit
    #ifdef NEXT_FIT
        void* bp;
        void* old_last_freep = last_freep;
        
        // 이전 탐색이 종료된 시점에서부터 다시 시작한다.
        for (bp = last_freep; GET_SIZE(HDRP(bp)); bp = NEXT_BLKP(bp)) {     
            if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
                return bp;
            }
        }
        
        // last_freep부터 찾았는데도 없으면 처음부터 찾아본다. 이 구문이 없으면 앞에 free 블록이 있음에도 extend_heap을 하게 되니 메모리 낭비가 된다.
        for (bp = heap_listp; bp < old_last_freep; bp = NEXT_BLKP(bp)) {
            if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
                return bp;
            }
        }
        
        // 탐색을 마쳤으니 last_freep도 수정해준다.
        last_freep = bp;
        
        return NULL;                                                        // 못 찾으면 NULL을 리턴한다.
        
    // first-fit
    #else
        void* bp;
        
        for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) { // heap_listp, 즉 prologue부터 탐색한다. 전에 우리는 heap_listp += (2 * WSIZE)를 해두었다.
            if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {    // 할당된 상태가 아니면서 해당 블록의 사이즈가 우리가 할당시키려는 asize보다 크다면 해당 블록에 할당이 가능하므로 곧바로 bp를 반환한다.
                return bp;
            }
        }
        
        return NULL;                                                        // 못 찾으면 NULL을 리턴한다.
        
    #endif
}

/*
 * place - 요구 메모리를 할당할 수 있는 가용 블록을 할당한다. 이 때 분할이 가능하다면 분할한다.
 */
static void place(void* bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));                                      // 현재 할당할 수 있는 후보 free 블록의 사이즈
    
    // 분할이 가능한 경우
    // 남은 메모리가 최소한의 free 블록을 만들 수 있는 4 word가 되느냐
    // -> header/footer/payload/정렬을 위한 padding까지 총 4 word 이상이어야 한다.
    if ((csize - asize) >= (2 * DSIZE)) {                                   // 2 * DSIZE는 총 4개의 word인 셈이다. csize - asize 부분에 header, footer, payload, 정렬을 위한 padding까지 총 4개가 들어갈 수 있어야 free 블록이 된다.
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
    
    // 분할이 불가능한 경우
    // csize - asize가 2 * DSIZE보다 작다는 것은 header, footer, payload, 정렬을 위한 padding 각 1개씩 총 4개로 이루어진 free 블록이 들어갈 공간이 없으므로 어쩔 수 없이 내부 단편화가 될 수 밖에 없다.
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    // 블록의 크기를 줄이는 것이면 줄이려는 size만큼으로 줄인다.
    // 블록의 크기를 늘리는 것이면 
    // 핵심은, 이미 할당된 블록의 사이즈를 직접 건드리는 것이 아니라, 임시로 요청한 사이즈 만큼의 블록을 만들고 현재의 블록을 반환하는 것이다.
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













