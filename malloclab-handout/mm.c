/*
 * mm-explicit-free-list.c
 * The version implemented woth implicit free list...
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

/* Read and write 1/2 word(s) at address p */
#define GET(p) (*(size_t *)(p))
#define PUT(p, val) (*(size_t *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header, footer and prev/next ptrs
 */
#define HDRP(bp) ((char *)(bp)-3 * DSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - 4 * DSIZE)
#define PREV_PTR(bp) ((char *)(bp)-2 * DSIZE)
#define NEXT_PTR(bp) ((char *)(bp)-DSIZE)

/* Given block ptr bp, compute address of next and previous blocks (adjacent mem
 * address) */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-4 * DSIZE)))

/* Given block ptr bp, compute address of next and previous blocks (adjacent
 * link list address) */
#define NEXT_LINKNODE_PTR(bp) ((char *)(GET(NEXT_PTR(bp))))
#define PREV_LINKNODE_PTR(bp) ((char *)(GET(PREV_PTR(bp))))

static void *extend_heap(size_t words);
static void insert_to_list(void *ptr);
static void remove_node(void *ptr);
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
    if ((heap_listp = mem_sbrk(5 * DSIZE)) == (void *)-1) {
        return -1;
    }
    PUT(heap_listp, PACK(4 * DSIZE, 1)); /* Prologue header */
    PUT(heap_listp + (1 * DSIZE), NULL); /* Prologue prev ptr */
    PUT(heap_listp + (2 * DSIZE), NULL); /* Prologue next ptr */
    PUT(heap_listp + (3 * DSIZE), PACK(4 * DSIZE, 1)); /* Prologue footer */

    PUT(heap_listp + (4 * DSIZE), PACK(0, 1)); /* Epilogue header */

    heap_listp += (3 * DSIZE);

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL) {
        return -1;
    }

    // mm_check();
    return 0;
}

static void *extend_heap(size_t words) {
    char *bp, *ptr;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE; //拓展内存大小双字对齐
    if ((long)(bp = mem_sbrk(size)) == -1) return NULL;

    bp += (2 * DSIZE);
    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0)); //设置新空闲块头部
    PUT(FTRP(bp), PACK(size, 0)); //设置新空闲块脚部
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); //设置新的结尾块

    ptr = coalesce(bp); //检查是否连续空闲块需要合并
    return ptr;
}

static void insert_to_list(void *ptr) {
    char *next;
    next = NEXT_LINKNODE_PTR(heap_listp); //获取当前的头部节点
    if (next) { //如果为null，就不必做了
        PUT(PREV_PTR(next), ptr); //将原头部节点的前一个节点设置为ptr
    }
    PUT(NEXT_PTR(ptr), next); //将ptr下一个节点设置为next
    PUT(NEXT_PTR(heap_listp), ptr); //将哨兵root头节点的下一个节点设置为ptr
    PUT(PREV_PTR(ptr), heap_listp); //将ptr的上一个节点设置为root节点
}

static void remove_node(void *ptr) {
    char *prev, *next;
    prev = PREV_LINKNODE_PTR(ptr); //获取链表前节点
    next = NEXT_LINKNODE_PTR(ptr); //获取链表后节点
    
    PUT(NEXT_PTR(prev), next); //设置前节点的下一个节点为next
    if(next){ //注意如果是null就不要做了
        PUT(PREV_PTR(next), prev); //设置后节点的前一个节点为prev
    }
}

static void *coalesce(void *ptr) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr))); //获取地址前块标记
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr))); //获取地址后块标记
    size_t size = GET_SIZE(HDRP(ptr)); //初始化新空闲内存块大小

    /*coalesce the block and change the point*/
    if (prev_alloc && next_alloc) { //Case 1
        
    } else if (prev_alloc && !next_alloc) { //Case 2
        size += GET_SIZE(HDRP(NEXT_BLKP(ptr))); //更新内存块大小
        remove_node(NEXT_BLKP(ptr)); //将后块从链表中先移除
        PUT(HDRP(ptr), PACK(size, 0)); //修改新内存块的头部
        PUT(FTRP(ptr), PACK(size, 0)); //修改新内存块的脚部
    } else if (!prev_alloc && next_alloc) { //Case 3
        size += GET_SIZE(HDRP(PREV_BLKP(ptr))); //更新内存块大小
        remove_node(PREV_BLKP(ptr)); //将前块从链表中先移除
        PUT(FTRP(ptr), PACK(size, 0)); //修改新内存块的头部
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0)); //修改新内存块的脚部
        ptr = PREV_BLKP(ptr); //修改新内存块指针，指向两块中的前块
    } else {
        size += GET_SIZE(FTRP(NEXT_BLKP(ptr))) + GET_SIZE(HDRP(PREV_BLKP(ptr))); //更新内存块大小
        remove_node(PREV_BLKP(ptr)); //将三块中的前块移除出链表
        remove_node(NEXT_BLKP(ptr)); //将三块中的后块移除出链表
        PUT(FTRP(NEXT_BLKP(ptr)), PACK(size, 0)); //修改新内存块的脚部
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0)); //修改新内存块的头部
        ptr = PREV_BLKP(ptr); //修改新内存块指针，指向三块中的前块
    }
    insert_to_list(ptr); //最后将新生成的内存块插入到链表头
    return ptr;
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
    if(size == 0){
        return NULL;
    }

    /* Adjust block size to include overhead and alignment reqs. */
    if(size <= DSIZE){
        asize = 5 * DSIZE; //至少40字节
    } else {
        asize = DSIZE * ((size + (4 * DSIZE) + (DSIZE - 1)) / DSIZE); //向上与8字节对齐
    }
    /* Search the free list for a fit */
    if((ptr = find_fit(asize)) != NULL) {
        place(ptr, asize);
        // mm_check();
        return ptr;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize, CHUNKSIZE);
    if((ptr = extend_heap(extendsize/WSIZE)) == NULL) {
        return NULL;
    }
    place(ptr, asize);
    
    // mm_check();
    return ptr;
}

static void *find_fit(size_t asize) {
    char *ptr;
    ptr = NEXT_LINKNODE_PTR(heap_listp); //从头节点开始
    while (ptr) {
        if (GET_SIZE(HDRP(ptr)) >= asize) { //比较是否满足大小要求
            return ptr;
        }
        ptr = NEXT_LINKNODE_PTR(ptr); //不满足则前往下一个链表节点
    }
    return NULL; //找不到返回null
}

static void place(void *ptr, size_t asize) {
    size_t block_size;
    block_size = GET_SIZE(HDRP(ptr)); //获取当前块的大小
    remove_node(ptr); //将当前被使用的空闲块从链表中移除
    if((block_size - asize) >= (5 * DSIZE)) { //判断是否需要截断
        PUT(HDRP(ptr), PACK(asize, 1)); //需要，则设置新的头部脚部信息
        PUT(FTRP(ptr), PACK(asize, 1));
        ptr = NEXT_BLKP(ptr); //同时处理后半段截断块
        PUT(HDRP(ptr), PACK(block_size - asize, 0));  //设置截断块的头部脚部
        PUT(FTRP(ptr), PACK(block_size - asize, 0));
        insert_to_list(ptr); //并将截断块加入链表
    } else {
        PUT(HDRP(ptr), PACK(block_size, 1)); //不需要，则直接设置头部脚部信息
        PUT(FTRP(ptr), PACK(block_size, 1));
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr) {
    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0)); //修改头部标记
    PUT(FTRP(ptr), PACK(size, 0)); //修改尾部标记

    coalesce(ptr);
    // mm_check();
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
    oldsize = GET_SIZE(HDRP(ptr)) - 4 * DSIZE;
    if (size < oldsize) oldsize = size;
    memcpy(newptr, ptr, oldsize);

    /* Free the old block. */
    mm_free(ptr);
    // mm_check();

    return newptr;
}

int mm_check(void) {
    void *ptr, *prev_ptr;
    size_t prev_alloc= 1;
    size_t count = 0;
    ptr = NEXT_LINKNODE_PTR(heap_listp);  //从头节点开始
    while (ptr) {
        count++; //计数+1
        ptr = NEXT_LINKNODE_PTR(ptr); 
    }
    for (ptr = NEXT_BLKP(heap_listp); GET_SIZE(HDRP(ptr)) > 0; ptr = NEXT_BLKP(ptr)) {  //遍历内存块
        if ((size_t)(ptr)&0x7) {  //检查对齐
            printf("Block pointer %p not aligned.\n", ptr);
            return 0;
        }
        size_t header = GET(HDRP(ptr));
        size_t footer = GET(FTRP(ptr));
        if(header != footer) { //检查头部和脚部是否一致
            printf("Block %p data corrupted, header and footer don't match.\n", ptr);
            return 0;
        }
        size_t cur_alloc = GET_ALLOC(HDRP(ptr));
        if(!cur_alloc) {
            count--;
        }
        if (!prev_alloc && !cur_alloc) {  //检查是否连续空闲块未合并
            printf("Contiguous free blocks detected starting from %p.\n", prev_ptr);
            return 0;
        }
        prev_alloc = cur_alloc;
        prev_ptr = ptr;
    }
    if(count) { //检测两次遍历的数量是否一致
        printf("Blocks numbers don't match...\n");
        return 0;
    }
    if(ptr != mem_heap_hi() + 1 + (2 * DSIZE)) { //检查是否用光了申请的堆内存
        printf("Blocks not using up the memory?\n");
        printf("Epilogue ptr: %p, Heap high ptr: %p\n", ptr - (2 * DSIZE), mem_heap_hi() + 1);
        printf("Heap size: %zu\n", mem_heapsize());
        return 0;
    }
    return 1;
}
