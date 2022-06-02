/*
 * mm-implicit-free-list.c
 * The version from the book implemented with implicit free list...
 * No linked list involved, everything about ptr offset...
 */
#include "mm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* Basic constants and macros */
#define WSIZE 4             /* Word and header/footer size (bytes) */
#define DSIZE 8             /* Double word size (bytes) */
#define CHUNKSIZE (1 << 12) /* Extend heap by this amount (bytes) */

#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp)-WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE)))

static void *extend_heap(size_t words);
static void *coalesce(void *ptr);
static void *find_fit(size_t asize);
static void place(void *ptr, size_t asize);
int mm_init(void);
void *mm_malloc(size_t size);
void mm_free(void *ptr);
void *mm_realloc(void *ptr, size_t size);
int mm_check(void);

static char *heap_listp = 0;

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void) {
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1) {
        return -1;
    }
    PUT(heap_listp, 0);                            /* Alignment padding */
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); /* Prologue header */
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); /* Prologue footer */
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));     /* Epilogue header */
    heap_listp += (2 * WSIZE);

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL) {
        return -1;
    }
    // mm_check();
    return 0;
}

static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1) return NULL;

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));         /* Free block header */
    PUT(FTRP(bp), PACK(size, 0));         /* Free block footer */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header*/

    return coalesce(bp);
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size) {
    size_t asize;      /* Adjust block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    char *ptr;

    /* Ignore spurious requests */
    if (size == 0) {
        return NULL;
    }

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE) {
        asize = 2 * DSIZE;  //至少16字节
    } else {
        asize =
            DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);  //向上与8字节对齐
    }

    /* Search the free list for a fit */
    if ((ptr = find_fit(asize)) != NULL) {
        place(ptr, asize);
        return ptr;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize, CHUNKSIZE);
    if ((ptr = extend_heap(extendsize / WSIZE)) == NULL) {
        return NULL;
    }
    place(ptr, asize);
    // mm_check();
    return ptr;
}

static void *find_fit(size_t asize) {
    void *ptr;
    void *res = NULL;
    size_t min_size = 0xffffffff;
    for (ptr = heap_listp; GET_SIZE(HDRP(ptr)) > 0;
         ptr = NEXT_BLKP(ptr)) {  //遍历列表
        if (GET_ALLOC(HDRP(ptr))) {
            continue;  //跳过已分配块
        }
        size_t block_size = GET_SIZE(HDRP(ptr));  //获取当前空闲块大小
        if (block_size >= asize &&
            block_size < min_size) {  //和记录的最小值比较，若更小则替换
            min_size = block_size;
            res = ptr;
        }
    }
    return res;  //返回大于等于asize的最小块指针
}

static void place(void *ptr, size_t asize) {
    size_t block_size = GET_SIZE(HDRP(ptr));
    if ((block_size - asize) >= (2 * DSIZE)) {  //需要截断
        PUT(HDRP(ptr), PACK(asize, 1));         //修改头部
        PUT(FTRP(ptr),
            PACK(asize, 1));  //顺着刚修改的头部，找到前块已分配块的脚部修改
        ptr = NEXT_BLKP(ptr);  //顺着修改后的头部，找到后块未分配块的有效载荷
        PUT(HDRP(ptr), PACK(block_size - asize,
                            0));  //修改后块未分配块的头部尾部，大小为差值
        PUT(FTRP(ptr), PACK(block_size - asize, 0));
    } else {  //不需要截断，修改头部脚部即可
        PUT(HDRP(ptr), PACK(block_size, 1));
        PUT(FTRP(ptr), PACK(block_size, 1));
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr) {
    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);

    // mm_check();
}

static void *coalesce(void *ptr) {
    //获取前后内存块的分配标识位
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
    //初始化新空闲块的大小为当前块大小，后续视情况添加
    size_t size = GET_SIZE(HDRP(ptr));

    if (prev_alloc && next_alloc) { /* Case 1 */
        return ptr;  //前后都已分配，什么都不做，返回ptr
    }

    else if (prev_alloc && !next_alloc) { /* Case 2 */
        //后面一块空闲，前面一块不空闲
        size += GET_SIZE(HDRP(NEXT_BLKP(ptr)));  //更新新空闲块大小
        PUT(HDRP(ptr), PACK(size, 0));  //修改头部大小，作为两块的头部
        PUT(FTRP(ptr), PACK(size, 0));  //顺着刚修改的大小找到尾部，修改尾部
    }

    else if (!prev_alloc && next_alloc) { /* Case 3 */
        //前面一块空闲，后面一块不空闲
        size += GET_SIZE(HDRP(PREV_BLKP(ptr)));  //更新新空闲块大小
        PUT(FTRP(ptr), PACK(size, 0));  //修改此块的脚部，作为两块的脚部
        PUT(HDRP(PREV_BLKP(ptr)),
            PACK(size,
                 0));  //顺着本块未更新的头部信息，找到前块的头部，对其进行修改
        ptr = PREV_BLKP(ptr);  //并将返回的新空闲块指针修改为前块的
    }

    else { /* Case 4 */
        //前后块都空闲
        size += GET_SIZE(HDRP(PREV_BLKP(ptr))) +
                GET_SIZE(FTRP(NEXT_BLKP(ptr)));  //更新空闲块大小
        PUT(HDRP(PREV_BLKP(ptr)),
            PACK(size, 0));  //修改前块的头部，作为三块的头部
        PUT(FTRP(NEXT_BLKP(ptr)),
            PACK(size, 0));  //修改后块的脚部，作为三块的脚部
        ptr = PREV_BLKP(ptr);  //并将返回的新空闲块指针修改为前块的
    }
    return ptr;
}

int mm_check(void) {
    void *ptr, *prev_ptr;
    size_t prev_alloc = 1;
    for (ptr = heap_listp; GET_SIZE(HDRP(ptr)) > 0;
         ptr = NEXT_BLKP(ptr)) {  //遍历内存块
        if ((size_t)(ptr)&0x7) {  //检查对齐
            printf("Block pointer %p not aligned.\n", ptr);
            return 0;
        }
        size_t header = GET(HDRP(ptr));
        size_t footer = GET(FTRP(ptr));
        if (header != footer) {  //检查头部和脚部是否一致
            printf("Block %p data corrupted, header and footer don't match.\n",
                   ptr);
            return 0;
        }
        size_t cur_alloc = GET_ALLOC(HDRP(ptr));
        if (!prev_alloc && !cur_alloc) {  //检查是否连续空闲块未合并
            printf("Contiguous free blocks detected starting from %p.\n",
                   prev_ptr);
            return 0;
        }
        prev_alloc = cur_alloc;
        prev_ptr = ptr;
    }
    if (ptr != mem_heap_hi() + 1) {  //检查是否用光了申请的堆内存
        printf("Blocks not using up the memory?\n");
        printf("Epilogue ptr: %p, Heap high ptr: %p\n", ptr, mem_heap_hi() + 1);
        printf("Heap size: %zu\n", mem_heapsize());
        return 0;
    }
    return 1;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size) {
    size_t oldsize;
    void *newptr;

    /* If size == 0 then this is just free, and we return NULL. */
    if (size == 0) {
        mm_free(ptr);
        return 0;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if (ptr == NULL) {
        return mm_malloc(size);
    }

    newptr = mm_malloc(size);

    /* If realloc() fails the original block is left untouched  */
    if (!newptr) {
        return 0;
    }

    /* Copy the old data. */
    oldsize = GET_SIZE(HDRP(ptr)) - DSIZE;
    if (size < oldsize) oldsize = size;
    memcpy(newptr, ptr, oldsize);

    /* Free the old block. */
    mm_free(ptr);

    // mm_check();
    return newptr;
}
