/* 
Reference:
https://sourceware.org/glibc/wiki/MallocInternals#Platform-specific_Thresholds_and_Constants
https://github.com/RAGUL1902/Dynamic-Memory-Allocation-in-C/tree/master
Memory Allocation for Long-Running Server Applications, P. Larson and M. Krishnan
Hoard: A Scalable Memory Allocator for Multithreaded Applications, E. D. Berger, K. S. McKinley, R. D. Blumofe, and P. R. Wilson
*/

#include <cassert>
#include <cstring>
#include <unistd.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <cstdint>
#include <cstddef>

typedef struct block{
  size_t size;
  struct block* next;
} block;

std::mutex heap_mutex;
std::mutex global_mutex;

block* global_head = nullptr;
thread_local block* local_head = nullptr;

constexpr size_t chunk_size = 32768;
constexpr size_t meta_size = sizeof(block);

size_t size_align(size_t s){
  return (s + 7) & ~7;
}

void free(void *ptr) {
  if (ptr == nullptr) return;

  block* mid = (block*)((char*)ptr - meta_size);

  // address ascending ordered insert
  block* cur = local_head;
  block* pre = nullptr;
  while (cur != nullptr){
    if ((uintptr_t)cur > (uintptr_t)mid) break;
    pre = cur;
    cur = cur->next;
  }
  mid->next = cur;
  if (pre != nullptr) pre->next = mid;
  else local_head = mid;

  // merge with right
  if (mid->next != nullptr){
    if ((uintptr_t)((char*)mid + meta_size + mid->size) == (uintptr_t)cur){
      mid->size += (meta_size + cur->size);
      mid->next = cur->next;
      cur->next = nullptr;
    }
  }

  // merge with left
  if (mid != local_head){
    if ((uintptr_t)((char*)pre + meta_size + pre->size) == (uintptr_t)mid){
      pre->size += (meta_size + mid->size);
      pre->next = mid->next;
      mid->next = nullptr;
    }
  }

}

void *malloc(size_t size) {
  if (size == 0) return nullptr;
  
  size = size_align(size);

  // traverse local list
  block* cur = local_head;
  block* pre = nullptr;
  block* prepre = nullptr;
  while (cur != nullptr){
    if (cur->size >= size) break;
    prepre = pre;
    pre = cur;
    cur = cur->next;
  }

  // 1. not found in local list
  if (cur == nullptr){

    // 1.0 try global list
    if (global_mutex.try_lock()){
      block* curr = global_head;
      block* prev = nullptr;
      while (curr != nullptr){
        if (curr->size >= size) break;
        prev = curr;
        curr = curr->next;
      }

      // found in global list
      if (curr != nullptr){
        if (prev != nullptr) prev->next = curr->next;
        else global_head = curr->next;
        global_mutex.unlock();

        // unlock and free to local list
        free((char*)curr + meta_size);

        // malloc from local list
        block* c = local_head;
        block* p = nullptr;
        while (c != nullptr){
          if (c->size >= size) break;
          p = c;
          c = c->next;
        }
        
        // try split in local list
        if (c != nullptr){
          if (c->size > size + meta_size){
            block* s = (block*)((char*)c + size + meta_size);
            s->size = c->size - size - meta_size;
            s->next = c->next;
            if (p != nullptr) p->next = s;
            else local_head = s;
            c->size = size;
            c->next = nullptr;
            return ((char*)c + meta_size);
          }
          else{
            if (p != nullptr) p->next = c->next;
            else local_head = c->next;
            c->next = nullptr;
            return ((char*)c + meta_size);
          }
        }
      }

      // not found in global list
      else global_mutex.unlock();
    }

    // 1.1 directly allocate for large block (double chunks)
    if (size > chunk_size - meta_size){
      heap_mutex.lock();
      void* tmp = sbrk(size * 2 + meta_size * 2);
      heap_mutex.unlock();
      assert(tmp != (void*)-1);

      // if possible, also transfer a chunk to global list 
      cur = (block*)tmp;
      block* half = (block*)((char*)cur + meta_size + size);
      half->size = size;
      if (global_mutex.try_lock()){
        half->next = global_head;
        global_head = half;
        global_mutex.unlock();
      }
      else free((char*)half + meta_size);

      cur->size = size;
      cur->next = nullptr;

      return ((char*)cur + meta_size);
    }

    // 1.2 always allocate two chunks for small block
    else{
      heap_mutex.lock();
      void* tmp = sbrk(chunk_size * 2);
      heap_mutex.unlock();
      assert(tmp != (void*)-1);

      // if possible, also transfer a chunk to global list 
      cur = (block*)tmp;
      block* half = (block*)((char*)cur + chunk_size);
      half->size = chunk_size - meta_size;
      if (global_mutex.try_lock()){
        half->next = global_head;
        global_head = half;
        global_mutex.unlock();
        cur->size = chunk_size - meta_size;
      }
      else cur->size = chunk_size * 2 - meta_size;

      // insert to free list
      cur->next = nullptr;
      if (pre != nullptr) pre->next = cur;
      else local_head = cur;

      // 1.2.1 at tail
      if (cur != local_head){

        // try merge with left
        if ((uintptr_t)((char*)pre + meta_size + pre->size) == (uintptr_t)cur){
          pre->size += (meta_size + cur->size);
          pre->next = cur->next;

          // try split
          if (pre->size > size + meta_size){
            block* sub = (block*)((char*)pre + size + meta_size);
            sub->size = pre->size - size - meta_size;
            sub->next = pre->next;
            pre->size = size;
            pre->next = nullptr;
            if (prepre != nullptr) prepre->next = sub;
            else local_head = sub;
            return ((char*)pre + meta_size);
          }
          else{
            if (prepre != nullptr) prepre->next = pre->next;
            else local_head = pre->next;
            pre->next = nullptr;
            return ((char*)pre + meta_size);
          }
        }

        // cannot merge, then try split
        else{
          if (cur->size > size + meta_size){
            block* sub = (block*)((char*)cur + size + meta_size);
            sub->size = cur->size - size - meta_size;
            sub->next = cur->next;
            pre->next = sub;
            cur->size = size;
            cur->next = nullptr;
            return ((char*)cur + meta_size);
          }
          else{
            pre->next = cur->next;
            cur->next = nullptr;
            return ((char*)cur + meta_size);
          }
        }
      }

      // 1.2.2 at head, try split
      else{
        if (cur->size > size + meta_size){
          block* sub = (block*)((char*)cur + size + meta_size);
          sub->size = cur->size - size - meta_size;
          sub->next = cur->next;
          local_head = sub;
          cur->size = size;
          cur->next = nullptr;
          return ((char*)cur + meta_size);
        }
        else{
          local_head = cur->next;
          cur->next = nullptr;
          return ((char*)cur + meta_size);
        }
      }
    }
  }
  
  // 2. found in local list and try split
  else if(cur->size > size + meta_size){
    block* sub = (block*)((char*)cur + size + meta_size);
    sub->size = cur->size - size - meta_size;
    sub->next = cur->next;
    if (cur == local_head) local_head = sub;
    else pre->next = sub;
    cur->size = size;
    cur->next = nullptr;
    return ((char*)cur + meta_size);
  }

  // 3. found in local list and cannot split (no space for meta data)
  else {
    if (cur == local_head) local_head = cur->next;
    else pre->next = cur->next;
    cur->next = nullptr;
    return ((char*)cur + meta_size);
  }

}

void *calloc(size_t nitems, size_t nsize) {
  if (nitems == 0 || nsize == 0) return nullptr;
  
  size_t sum_size = nitems * nsize;
  sum_size = size_align(sum_size);
  
  void* p = malloc(sum_size);
  assert(p != nullptr);
  memset(p, 0, sum_size);

  return p;
}

void *realloc(void *ptr, size_t size) {
  if (ptr == nullptr) return malloc(size);
  if (size == 0) {
    free(ptr);
    return nullptr;
  }
  
  size = size_align(size);

  block* old_block = (block*)((char*)ptr - meta_size);

  if (old_block->size == size) return ptr;

  // shrink block, if possibe, split
  else if (old_block->size > size){
    if (old_block->size > size + meta_size){
      block* sub = (block*)((char*)old_block + size + meta_size);
      sub->size = old_block->size - size - meta_size;
      free((char*)sub + meta_size);
      old_block->size = size;
    }
    return ptr;
  }

  // grow or malloc and copy
  else{
    
    // pseudo free
    block* cur = local_head;
    block* pre = nullptr;
    while (cur != nullptr){
      if ((uintptr_t)cur > (uintptr_t)old_block) break;
      pre = cur;
      cur = cur->next;
    }
    
    // if right is large enough and adjacent, merge with right
    if (cur != nullptr){
      if ((uintptr_t)((char*)old_block + meta_size + old_block->size) == (uintptr_t)cur){
        if (old_block->size + meta_size + cur->size >= size){
          old_block->size += (meta_size + cur->size);
          old_block->next = cur->next;
          cur->next = nullptr;
          
          // try split
          if (old_block->size > meta_size + size){
            block* sub = (block*)((char*)old_block + size + meta_size);
            sub->size = old_block->size - size - meta_size;
            sub->next = old_block->next;
            if (pre != nullptr) pre->next = sub;
            else local_head = sub;
            old_block->size = size;
            old_block->next = nullptr;
            return ((char*)old_block + meta_size);
          }
          else{
            if (pre != nullptr) pre->next = old_block->next;
            else local_head = old_block->next;
            old_block->next = nullptr;
            return ((char*)old_block + meta_size);
          }
        }
      }
    }

    void* new_ptr = malloc(size);
    assert(new_ptr != nullptr);
    memcpy(new_ptr, ptr, old_block->size);
    free(ptr);
    return new_ptr;
  }
}