// Copyright EmulationOnline 2025
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS”
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
#include <mutex>
template <typename T, size_t CAPACITY>
class Ring {
public:
    Ring() = default;
    ~Ring() = default;
    // Attempts to enqueue 'count' items from 'src'.
    // Returns the number of values successfully written.
    size_t push(const T* src, size_t count) {
        std::lock_guard<std::mutex> g(mut_);
        size_t written = 0;
        size_t first = ptrs_.first;
        size_t next = first + ptrs_.len;
        for (size_t i = 0; i < count; i++) {
            if (ptrs_.len == CAPACITY) break;
            buffer_[(next + i) % CAPACITY] = src[i];
            // std::cout << "wrote: " << src[i] << std::endl;
            ptrs_.len++;
            written++;
        }
        return written;
    }

    // Attempts to dequeue 'count' items from 'src'.
    // Returns the number of values successfully read.
    size_t pull(T* dest, size_t count) {
        std::lock_guard<std::mutex> g(mut_);
        size_t saved = 0;
        for (size_t i = 0; i < count; i++) {
            if (ptrs_.len == 0) break;
            dest[i] = buffer_[ptrs_.first];
            // std::cout << "read: " << dest[i] << std::endl;
            ptrs_.first = (ptrs_.first+1) % CAPACITY;
            ptrs_.len--;
            saved++;
        }
        return saved;
    }

    // for testing only
    size_t size() const {
        return ptrs_.len;
        // if (first_ == next_) {
        //     return 0;
        // }
        // size_t acc = 0;
        // size_t first = first_;
        // while (first != next_) {
        //     first = (first + 1) % CAPACITY;
        // }
        // return acc;
    }

private:
    T buffer_[CAPACITY];
    // each ptr is written only by a single fn.
    struct Ptrs {
        size_t first = 0;
        size_t len = 0;
    };
    std::mutex mut_;
    Ptrs ptrs_;  // guarded_by mut_
};
