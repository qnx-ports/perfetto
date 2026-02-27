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

#ifndef SRC_TRACED_QNX_PROBES_KERNEL_PARSER_PAGE_CACHE_H_
#define SRC_TRACED_QNX_PROBES_KERNEL_PARSER_PAGE_CACHE_H_

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

namespace perfetto {
namespace qnx {

/**
 * A memory cache that is split across a configurable number of pages in a
 * circular buffer. The page cache is mean't to support a producer/writer thread
 * and a consumer/reader thread that access pages concurrently. This model
 * allows the two threads to coordinate where the reader can read from available
 * pages while the writer can write to other pages concurrently. The
 * implementation provides blocking read/write functions that will only return
 * when an appropriate page is available for reading.
 *
 * Data can be written to the page cache in arbitrary blocks. Reading from the
 * page cache is limited to data in size of "chuncks". In practice we use
 * sizeof(traceevent_t) -- 16 bytes as the chunk size so that the pages can be
 * read one tracevent_t at a time. For this reason we align the page sizes to
 * the chunk size so that chunks never span pages.
 *
 * When no more data is available to write the Finish() method will release any
 * partially filled page to make that data available to the reader.
 *
 * When no more data is available and Finish() has been called GetChunk() will
 * return nullptr.
 */
class PageCache {
 public:
  PageCache(std::size_t page_size_chunks, std::size_t num_pages);
  PageCache(std::size_t page_size_chunks, std::size_t num_pages, int max_pages);
  ~PageCache();

  std::size_t Write(std::byte* data, std::size_t bytes);
  void* GetChunk();
  void ReleaseChunk();
  void Finish();

 private:
  class Page {
   public:
    Page(std::size_t size);
    ~Page();
    std::byte* data_;
    std::size_t readable_bytes_;
  };

  void PutWritePage(std::unique_ptr<Page> page);
  std::unique_ptr<Page> GetWritePage();

  void PutReadPage(std::unique_ptr<Page> page);
  std::unique_ptr<Page> GetReadPage();

  static const std::size_t kChunkSizeBytes = 16;
  static const std::size_t kMinPages = 2;

  std::size_t bytes_per_page_;
  std::size_t page_size_;
  std::size_t num_pages_;
  int max_pages_;

  mutable std::mutex mutex_;
  std::condition_variable read_page_available_;
  std::condition_variable write_page_available_;
  bool is_finished_;

  std::queue<std::unique_ptr<Page>> write_pages_;
  std::unique_ptr<Page> write_page_;
  std::size_t write_page_offset_;

  std::queue<std::unique_ptr<Page>> read_pages_;
  std::unique_ptr<Page> read_page_;
  std::size_t read_page_offset_;
};

}  // namespace qnx
}  // namespace perfetto

#endif  // SRC_TRACED_QNX_PROBES_KERNEL_PARSER_PAGE_CACHE_H_
