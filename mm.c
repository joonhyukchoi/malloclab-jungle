/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "team 8",
    /* First member's full name */
    "허원영 오인규 최준혁",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) ((size) & ~0x7)


#define SIZE_T_SIZE(x) (ALIGN((*(unsigned int *)x)))

/* 매크로를 사용해서 포인터연산의 실수로 인한 잘못된 메모리 사용을 방지하기 위함 */
/* Basic constants and macros */

#define WSIZE   4   /* Word and header/footer size (bytes) */
#define DSIZE   8   /* Double word size (bytes) */
#define CHUNKSIZE   (1<<12) /* Extend heap by this amount (bytes) */
#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Pack은 크기와 할당비트를 OR연산해서 4바이트짜리 헤더를 만드는 매크로 */
#define PACK(size, alloc)   ((size) | (alloc))
/* 주소 p로부터 1 word (책에선 32비트를 사용하므로 4 바이트)의 메모리를 읽고 쓰는 매크로 */
#define GET(p)  (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* 주소 p로부터 앞의 3비트를 제외한 사이즈 크기를 읽는 매크로 */
#define GET_SIZE(p) (GET(p) & ~0x7)
/* 주소 p로부터 맨 앞 1비트를 읽어 메모리가 할당 되었는지 확인하는 매크로 */
/* 만약 효율적인 메모리 사용을 위해 특정 상황에서 푸터를 제외하고 싶으면 이전 블록과 다음 블록의 메모리 할당을 저장하는 매크로가 필요함*/
#define GET_ALLOC(p) (GET(p) & 0x1)

/* 주어진 블록포인터 bp로부터 헤더와 풋터의 주소를 계산하는 매크로 */
#define HDRP(bp)    ((char *)(bp) - WSIZE)
#define FTRP(bp)    ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* 주어진 블록포인터 bp로부터 다음 블록의 주소를 계산하는, 이전 블록의 주소를 계산하는 매크로 */
#define NEXT_BLKP(bp)   ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)   ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

static void *heap_listp;
static void *root_freep;

static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    // 둘다 이미 할당되어 있는 경우
    if (prev_alloc && next_alloc)
    {
        return bp;
    } 
    // 다음 블록만 free인 경우
    else if (prev_alloc && !next_alloc)
    {
        // 해당블록크기 + 다음블록크기 사이즈로 헤더와 풋터를 수정
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    // 이전 블록만 free인 경우
    else if (!prev_alloc && next_alloc)
    {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        // 블록 포인터를 이전 블록위치로 옮겨야 함
        bp = PREV_BLKP(bp);
    }
    // 이전 블록, 다음 블록 둘 다 free인 경우
    else
    {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    return bp;
}

static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    // 최소 8바이트 크기만큼의 단위로 잘라야하기 때문에 사이즈를 짝수로 만듦
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    // mem_sbrk (in memlib.c) 함수에서 size가 0보다 작거나 메모리 추가를 가정했을때 최대허용 크기를 벗어나는 경우 -1을 반환 
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;
    
    // 순서대로 free 상태의 헤더와 풋터 그리고 에필로그 헤더를 초기화 함
    // 힙영역 전체(1기가)가 free상태의 힙영역 1개로 되어있고 맨앞에는 헤더, 뒤에는 풋터 그리고 에필로그 헤더가 있음.
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    // 이전 블록이 free상태면 coalesce(연합이라는 뜻) 함수 호출
    return coalesce(bp);
}
/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    // 비어있는 초기 힙 생성
    // 왜 heap_listp 선언은 안하는걸까? 생략인가?
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
        return -1;
    // alignment padding, heap_listp가 가리키는 곳은 4바이트 크기의 0값을 가짐
    PUT(heap_listp, 0);
    // prologue header, 8바이트인 이유는 헤더와 풋터밖에 없기 때문
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));
    // prologue footer, 8바이트인 이유는 헤더와 풋터밖에 없기 때문
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));
    // epilogue header, 실제로 헤더크기가 4바이트인데 크기를 0으로 표기함. 왜?
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));

    // heap_listp 즉 brk가 가리키는 곳은 prologue footer의 메모리 주소를 가리킴  
    heap_listp += (2 * WSIZE);
    
    // 초기화하면 root free pointer가 전체 블록(free 상태)을 가리킴 
    root_freep = heap_listp;

    // CHUNKSIZE 바이트 만큼의 비어있는 힙으로 확장함
    // 워드 사이즈 만큼의 크기로 블록을 구성하므로 전체 크기를 워드크기로 나눠서 필요한 워드의 수를 함수의 인자로 정함
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;

    return 0;
}

static void *find_fit(size_t asize)
{
    void *bp;

    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp))
    {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp))))
        {
            return bp;
        }
    }
    return NULL;
}

static void place(void *bp, size_t asize)
{
    // int newsize = ((asize + 1) >> 1) << 1;
    // int oldsize = *(int *)bp & -2;
    // *(int *)bp = newsize | 1;
    // if (newsize < oldsize)
    //     *((int *)bp + newsize) = oldsize - newsize;
    size_t csize = GET_SIZE(HDRP(bp));

    if ((csize - asize) >= (2 * DSIZE))
    {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
    }
    else
    {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    // 이전거 다 블록처리
    // int newsize = ALIGN(size + SIZE_T_SIZE);
    // void *p = mem_sbrk(newsize);
    // if (p == (void *)-1)
	// return NULL;
    // else {
    //     *(size_t *)p = size;
    //     return (void *)((char *)p + SIZE_T_SIZE);
    // }
    size_t asize;
    size_t extendsize;
    char *bp;

    if (size == 0)
        return NULL;
    
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    // 헤드와 풋터의 오버헤드 크기(DSIZE), 인접 8배수 반올림(+ DSIZE - 1)
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
    
    // 해당 size의 free 힙 영역을 찾아서 반환하는 부분
    if ((bp = find_fit(asize)) != NULL)
    {
        // 초과부분을 분할하는 place 함수
        place(bp, asize);
        return bp;
    }

    // 해당 size의 free 힙 영역이 없을 경우 더 큰영역의 힙을 새로 할당함
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    // 현재 포인터가 가리키는 블록의 헤더에 접근하여 해당 블록의 사이즈크기 도출
    size_t size = GET_SIZE(HDRP(ptr));

    // 헤더, 풋터의 마지막 비트를 0으로 바꿔서 free 상태로 전환
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));

    // 이전, 이후 블록이 free상태이면 병합하는 coalesce 함수 호출
    coalesce(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;
    copySize = GET_SIZE(HDRP(oldptr));
    // copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE(oldptr));
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}














