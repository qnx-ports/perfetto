/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/traced/qnx_probes/kernel/parser/page_cache.h"

#include <algorithm>

namespace perfetto {
namespace qnx {

const std::size_t PageCache::kChunkSizeBytes;
const std::size_t PageCache::kMinPages;

PageCache::Page::Page(std::size_t size) : data_(), readable_bytes_(0) {
  if (size == 0) {
    return;
  }

  // Calculate the closest, bigger multiple of 16.
  // The formula is: size + (alignment - 1) & ~(alignment - 1)
  std::size_t aligned_size = (size + 15) & (~15);

  data_ = new (std::nothrow) std::byte[aligned_size];
}

PageCache::Page::~Page() {
  if (data_) {
    delete[] data_;
    data_ = nullptr;
  }
}

PageCache::PageCache(std::size_t page_size_chunks, std::size_t num_pages)
    : PageCache(page_size_chunks, num_pages, static_cast<int>(num_pages)) {}

PageCache::PageCache(std::size_t page_size_chunks,
                     std::size_t num_pages,
                     int max_pages)
    : bytes_per_page_(kChunkSizeBytes * page_size_chunks),
      page_size_(page_size_chunks * kChunkSizeBytes),
      num_pages_(num_pages),
      max_pages_(max_pages),
      mutex_(),
      read_page_available_(),
      write_page_available_(),
      is_finished_(false),
      write_pages_(),
      write_page_(nullptr),
      write_page_offset_(),
      read_pages_(),
      read_page_(nullptr),
      read_page_offset_() {
  // The cache MUST have at least two pages.
  num_pages_ = std::max(kMinPages, num_pages_);

  // If a limit is set on the number of pages ensure it is at least as big as
  // the specified number of initial pages.
  if (max_pages_ >= 0) {
    max_pages_ = std::max(static_cast<int>(num_pages_), max_pages_);
  }

  // Allocate the initial pages and add them to the write_pages_ queue.
  for (std::size_t i = 0; i < num_pages_; i++) {
    std::unique_ptr<Page> page = std::make_unique<Page>(bytes_per_page_);
    if (page->data_) {
      // NOTE: if page->data_ failed to allocated we skip the page. This can
      // lead to different situations. You might just have fewer pages, if there
      // is only one page you will contend for the page but if NO pages are
      // allocated then you will deadlock on the cache.
      write_pages_.push(std::move(page));
    }
  }
}

PageCache::~PageCache() {}

std::size_t PageCache::Write(std::byte* data, std::size_t bytes) {
  std::byte* data_to_write = data;
  std::size_t remaining_bytes_to_write = bytes;

  // While we still have data to write to pages get the next page and write it.
  while (remaining_bytes_to_write > 0 && !is_finished_) {
    if (!write_page_) {
      write_page_ = std::move(GetWritePage());
    }

    if (write_page_) {
      // Calculate how much we can write into the page without overflowing it.
      std::size_t write_page_free_bytes = page_size_ - write_page_offset_;
      std::size_t write_size = write_page_free_bytes;
      if (write_page_free_bytes > remaining_bytes_to_write) {
        write_size = remaining_bytes_to_write;
      }
      void* page_data = write_page_->data_ + write_page_offset_;
      memcpy(page_data, data_to_write, write_size);
      write_page_->readable_bytes_ += write_size;
      write_page_offset_ += write_size;
      remaining_bytes_to_write -= write_size;
      data_to_write = data_to_write + write_size;

      // If the page is full then make it available for read.
      if (write_page_offset_ >= page_size_) {
        PutReadPage(std::move(write_page_));
        write_page_offset_ = 0;
      }
    }
  }

  return remaining_bytes_to_write;
}

void* PageCache::GetChunk() {
  if (!read_page_) {
    read_page_ = std::move(GetReadPage());
  }

  if (read_page_) {
    if (read_page_->readable_bytes_ < kChunkSizeBytes) {
      return nullptr;
    }
    void* chunk = (void*)(read_page_->data_ + read_page_offset_);
    return chunk;
  }

  return nullptr;
}

void PageCache::ReleaseChunk() {
  if (read_page_) {
    std::size_t step_size = kChunkSizeBytes;
    if (read_page_->readable_bytes_ < kChunkSizeBytes) {
      step_size = read_page_->readable_bytes_;
    }

    // Move the cursor ahead one chunk.
    read_page_offset_ += step_size;
    read_page_->readable_bytes_ -= step_size;

    // If the whole page is read then put it in the write queue.
    if (read_page_->readable_bytes_ == 0) {
      PutWritePage(std::move(read_page_));
      read_page_offset_ = 0;
    }
  }
}

void PageCache::Finish() {
  // If there is a partially completed write_page move it to read_queue.
  // Note this function assumes the writer is no longer active.
  is_finished_ = true;
  if (write_page_) {
    PutReadPage(std::move(write_page_));
    write_page_offset_ = 0;
  } else {
    // We still need to notify the reader we have finished
    read_page_available_.notify_one();
  }
}

void PageCache::PutWritePage(std::unique_ptr<Page> page) {
  std::unique_lock<std::mutex> lock(mutex_);

  write_pages_.push(std::move(page));

  write_page_available_.notify_one();
}

std::unique_ptr<PageCache::Page> PageCache::GetWritePage() {
  if (is_finished_) {
    return nullptr;
  }

  std::unique_lock<std::mutex> lock(mutex_);
  if (write_pages_.empty()) {
    if (max_pages_ == -1 || num_pages_ < (std::size_t)max_pages_) {
      std::unique_ptr<Page> page = std::make_unique<Page>(bytes_per_page_);
      if (page) {
        if (page->data_ != nullptr) {
          num_pages_++;
          return page;
        }
      }
    }
    write_page_available_.wait(
        lock, [this] { return !write_pages_.empty() || is_finished_; });
    if (is_finished_) {
      return nullptr;
    }
  }

  std::unique_ptr<Page> page = std::move(write_pages_.front());
  write_pages_.pop();

  return page;
}

void PageCache::PutReadPage(std::unique_ptr<Page> page) {
  std::unique_lock<std::mutex> lock(mutex_);

  read_pages_.push(std::move(page));

  read_page_available_.notify_one();
}

std::unique_ptr<PageCache::Page> PageCache::GetReadPage() {
  std::unique_lock<std::mutex> lock(mutex_);
  // If the queue is "finished" then we don't want to block for new pages.
  if (is_finished_) {
    if (!read_pages_.empty()) {
      std::unique_ptr<Page> page = std::move(read_pages_.front());
      read_pages_.pop();
      return page;
    }
    return nullptr;
  }

  read_page_available_.wait(
      lock, [this] { return !read_pages_.empty() || is_finished_; });
  if (is_finished_ && read_pages_.empty()) {
    return nullptr;
  }

  std::unique_ptr<Page> page = std::move(read_pages_.front());
  read_pages_.pop();

  return page;
}

}  // namespace qnx
}  // namespace perfetto
