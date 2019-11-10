/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FOLLY_ARENA_H_
#error This file may only be included from Arena.h
#endif

// Implementation of Arena.h functions

namespace folly {

template <class Alloc>
std::pair<typename Arena<Alloc>::Block*, size_t>
Arena<Alloc>::Block::allocate(Alloc& alloc, size_t size, bool allowSlack) {
  size_t allocSize = sizeof(Block) + size;
  if (allowSlack) {
    allocSize = ArenaAllocatorTraits<Alloc>::goodSize(alloc, allocSize);
  }

  void* mem = std::allocator_traits<Alloc>::allocate(alloc, allocSize);
  return std::make_pair(new (mem) Block(), allocSize - sizeof(Block));
}
/* The struct Block is actually a pointer in slist. This function allocate
a block of memory, consists of a Block pointer and a chunk of memory.
So sizeof(Block) is the slist pointer size. The actual data should be store in the 
memory following the pointer, which is the Block().start() points to.
alloate finally return the address of Block and the length of actual usable memory, 
which is allocSize - sizeof(Block) and should be equal to the parameter `size`.
In other word, actually parameter `size` plus sizeof(Block) is allocated.
The return statement constructs a Block with pointer mem.
*/

template <class Alloc>
void Arena<Alloc>::Block::deallocate(Alloc& alloc) {
  this->~Block();
  std::allocator_traits<Alloc>::deallocate(alloc, this, 1);
}
/* I have never see calling dtor directly like this. TOBE understood.
*/

template <class Alloc>
void* Arena<Alloc>::allocateSlow(size_t size) {
  std::pair<Block*, size_t> p;
  char* start;

  size_t allocSize;
  if (!checked_add(&allocSize, std::max(size, minBlockSize()), sizeof(Block))) {
    throw_exception<std::bad_alloc>();
  }
  if (sizeLimit_ != kNoSizeLimit &&
      allocSize > sizeLimit_ - totalAllocatedSize_) {
    throw_exception(std::bad_alloc());
  }

  if (size > minBlockSize()) {
    // Allocate a large block for this chunk only, put it at the back of the
    // list so it doesn't get used for small allocations; don't change ptr_
    // and end_, let them point into a normal block (or none, if they're
    // null)
    p = Block::allocate(alloc(), size, false);
    start = p.first->start();
    blocks_.push_back(*p.first);
  } else {
    // Allocate a normal sized block and carve out size bytes from it
    p = Block::allocate(alloc(), minBlockSize(), true);
    start = p.first->start();
    blocks_.push_front(*p.first);
    ptr_ = start + size;
    end_ = start + p.second;
  }

  assert(p.second >= size);
  totalAllocatedSize_ += p.second + sizeof(Block);
  return start;
}
/* checked_add is a overflow-safe adding function.
allocSize = max(size, minBlockSize()) + sizeof(Block).
We will actually allocate memory_size + sizeof(Block) bytes of memory (see commment
on Block::allocate()). The data size is equal to max(size, minBlockSize()).
If size is greater, we will allocate a big chunk; Otherwise, a minBlockSize() chunk
is allocated. ptr_ points to the end of memory requested. end_ points the the end of
memory actually allocated.
The Block pointer returned by Block::allocate() is pushed back to the blocks_ slist.
## Questions:
*/

template <class Alloc>
void Arena<Alloc>::merge(Arena<Alloc>&& other) {
  blocks_.splice_after(blocks_.before_begin(), other.blocks_);
  other.blocks_.clear();
  other.ptr_ = other.end_ = nullptr;
  totalAllocatedSize_ += other.totalAllocatedSize_;
  other.totalAllocatedSize_ = 0;
}
/* See file:///Users/ouakira/Documents/boost_1_66_0/doc/html/boost/intrusive/slist.html.
`other` is inserted before `blocks_`.
*/

template <class Alloc>
Arena<Alloc>::~Arena() {
  auto disposer = [this](Block* b) { b->deallocate(this->alloc()); };
  while (!blocks_.empty()) {
    blocks_.pop_front_and_dispose(disposer);
  }
}

} // namespace folly
