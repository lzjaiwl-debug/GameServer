#pragma once

namespace AssetLib {
    // 新增：提供一个与 U++ 风格兼容的 Cout()，内部用 std::cout
    static inline std::ostream& Cout()
    {
        return std::cout;
    }
    static inline void ThrowRuntime(const char* msg)
    {
        throw std::runtime_error(msg);
    }
#ifdef _MEMDEBUG
#if defined(PLATFORM_POSIX)
#include <execinfo.h>
#endif
#include <thread>
#include <mutex>
#include <deque>
#include <string>
#include <chrono>

    static std::mutex dbg_hist_mutex;
    static std::deque<std::string> dbg_hist;
    static const size_t DBG_HIST_MAX = 2048;

    // 记录一条诊断事件（环形缓冲）
    static void RecordDbgEvent(const std::string& s) {
        std::lock_guard<std::mutex> g(dbg_hist_mutex);
        dbg_hist.push_back(s);
        if (dbg_hist.size() > DBG_HIST_MAX)
            dbg_hist.pop_front();
    }
    static void DebugPrintStack()
    {
#if defined(PLATFORM_POSIX)
        void* bt[32];
        int n = backtrace(bt, 32);
        char** s = backtrace_symbols(bt, n);
        if (s) {
            for (int i = 0; i < n; ++i)
                Cout() << "    " << s[i] << "\n";
            free(s);
        }
#elif defined(PLATFORM_WIN32)
        // Lightweight backtrace on Windows (addresses only)
        void* bt[62];
        USHORT n = CaptureStackBackTrace(0, 62, bt, NULL);
        for (USHORT i = 0; i < n; ++i)
            Cout() << "    " << bt[i] << "\n";
        // For symbol resolution use SymFromAddr when debugging interactively.
#endif
    }
#endif
    struct MemoryBlock {
        size_t       size{ 0 };
        char* start{ nullptr };
        char* end{ nullptr };
        bool         is_free{ true };
        MemoryBlock* last{ nullptr };
        MemoryBlock* next{ nullptr };

#if defined(PLATFORM_WIN32)
        WSABUF _wsaBuf{ 0 };
#endif

        MemoryBlock() = default;

        MemoryBlock(size_t block_size, char* block_start)
            : size(block_size)
            , start(block_start)
            , end(block_start + block_size)
            , is_free(true)
        {
        }

        int           box_index{ -1 };     // index in bockBox array
        MemoryBlock* bin_next{ nullptr }; // bin linked list pointer for free bins
        int           bin_index{ -1 };     // which bin this block is currently in; -1 means not in any bin

        // reference count for safe delayed reclamation / reuse
        std::atomic<int> ref_count{ 0 };

        bool operator==(const MemoryBlock* block) const
        {
            if (!block)
                return false;
            return block->start == start && block->end == end && block->size == size;
        }

        // 本体解链（从双向链表中摘除自己）
        MemoryBlock& operator--()
        {
            if (last)
                last->next = next;
            if (next)
                next->last = last;
            last = nullptr;
            next = nullptr;
            return *this;
        }

        // 安全地将 block 插入到当前节点之前（前插）
        MemoryBlock& operator<<=(MemoryBlock* block)
        {
            if (!block || block == this)
                return *this;

            if (block->last)
                block->last->next = block->next;
            if (block->next)
                block->next->last = block->last;
            block->last = nullptr;
            block->next = nullptr;

            block->next = this;
            block->last = last;
            if (last)
                last->next = block;
            last = block;

            return *this;
        }

        // 安全地将 block 插入到当前节点之后（后插）
        MemoryBlock& operator>>=(MemoryBlock* block)
        {
            if (!block || block == this)
                return *this;

            if (block->last)
                block->last->next = block->next;
            if (block->next)
                block->next->last = block->last;
            block->last = nullptr;
            block->next = nullptr;

            block->last = this;
            block->next = next;
            if (next)
                next->last = block;
            next = block;

            return *this;
        }

        operator char* ()
        {
            return start;
        }

#if defined(PLATFORM_WIN32)
        operator WSABUF& ()
        {
            _wsaBuf.len = static_cast<ULONG>(size);
            _wsaBuf.buf = start;
            return _wsaBuf;
        }
#endif

        // 向前合并内存（将前一个相邻 free block 吞并到当前）
        MemoryBlock* operator<<(MemoryBlock* block)
        {
            if (!block || !block->is_free)
                return this;
            if (block->end != start)
                return this;

            start = block->start;
            size += block->size;

            MemoryBlock* prev = block->last;
            if (prev)
                prev->next = block->next;
            if (block->next)
                block->next->last = prev;
            block->last = block->next = nullptr;
            return this;
        }

        std::string tostring() const
        {
            if (!start || size == 0)
                return {};
            return std::string(start, size);
        }

        std::string tostring(int offset) const
        {
            if (!start || size <= static_cast<size_t>(offset))
                return {};
            return std::string(start + offset, size - offset);
        }
    };

    class MemoryPool {
    private:
        size_t total_size_{ 0 };          // 内存池大小
        char* memory_block_{ nullptr };  // 实际内存缓冲区
        bool releasing_ = false;
        // 元数据槽：std::vector< unique_ptr<MemoryBlock> >
        std::vector<std::unique_ptr<MemoryBlock>> bockBox;
        std::vector<int>                          free_slot_stack;
        std::mutex                                free_slot_mutex_;
        size_t max_free_size_ = 0;
        std::atomic<uint64_t> alloc_counter_{ 0 };
        // 延迟回收槽位索引
        std::vector<int> delayed_free_slots_;
        // 回调：每次 deallocate 后汇报当前最大空闲块
        std::function<void(size_t)> whendeallocate;
        static const int BIN_COUNT = 16;
        std::vector<MemoryBlock*> bins; // bins[b] 为该桶链表头

        MemoryBlock* free_list_{ nullptr };  // 空闲链表
        MemoryBlock* used_list_{ nullptr };  // 已分配链表
        std::mutex   mutex_;

        // 邻接映射
        std::unordered_map<char*, MemoryBlock*> start_map;
        std::unordered_map<char*, MemoryBlock*> end_map;

        struct Stats {
            size_t total_allocations{ 0 };
            size_t total_frees{ 0 };
            size_t allocation_failures{ 0 };
            size_t current_usage{ 0 };
            size_t peak_usage{ 0 };
        } stats_;

#ifdef _MEMDEBUG
        std::atomic<uint64_t> dbg_inc_count{ 0 };
        std::atomic<uint64_t> dbg_dec_count{ 0 };
#endif

        static constexpr size_t MIN_ALLOC_SIZE = 0;
        static inline size_t RoundUpToPowerOfTwo(size_t n) { return n; }

        bool is_in_free_list(MemoryBlock* mb)
        {
            if (!mb) return false;
            MemoryBlock* cur = free_list_;
            size_t guard = 0;
            const size_t limit = (bockBox.size() > 0 ? bockBox.size() * 4 : 100000);
            while (cur && ++guard <= limit) {
                if (cur == mb) return true;
                cur = cur->next;

            }
            return false;
        }
        bool is_in_used_list(MemoryBlock* mb)
        {
            if (!mb) return false;
            MemoryBlock* cur = used_list_;
            size_t guard = 0;
            const size_t limit = (bockBox.size() > 0 ? bockBox.size() * 4 : 100000);
            while (cur && ++guard <= limit) {
                if (cur == mb) return true;
                cur = cur->next;
            }
            return false;
        }
        bool IsValidBlockPtr(MemoryBlock* p) {
            if (!p) return false;
            // quick sanity: pointer not small/near-null
            if ((uintptr_t)p < 0x1000) return false;

            // 优化：优先使用 box_index 快速验证
            if (p->box_index >= 0 && p->box_index < static_cast<int>(bockBox.size())) {
                auto& one = bockBox[p->box_index];
                if (!one) return false;
                if (one.get() == p) return true;
            }

            // 在 bockBox 中查找该指针（确保该指针是由 bockBox 管理的元数据）
            int cnt = static_cast<int>(bockBox.size());
            for (int i = 0; i < cnt; ++i) {
                auto& one = bockBox[i];
                if (!one) continue;
                if (one.get() == p) return true;
            }
            return false;
        }

        MemoryBlock* merge_adjacent(MemoryBlock* ptr) {
            if (!ptr) return nullptr;

            // 调用方已持有 mutex_。
            bool merged = true;
            while (merged) {
                merged = false;

                // --- 左合并：end_map 中以 ptr->start 为 end 的块 ---
                auto itL = end_map.find(ptr->start);
                if (itL != end_map.end()) {
                    MemoryBlock* left = itL->second;
                    if (left && left != ptr && left->is_free && left->end == ptr->start) {
                        // 1) 从所有结构中摘除 left

                        // 先从映射移除
                        remove_start_map(left);
                        remove_end_map(left);

                        // 从 bins 中移除
                        if (left->bin_index >= 0)
                            remove_from_bins(left);

                        // 从 free_list_ 物理链表中摘除 left
                        if (left->last)
                            left->last->next = left->next;
                        else if (free_list_ == left)
                            free_list_ = left->next;
                        if (left->next)
                            left->next->last = left->last;
                        left->last = nullptr;
                        left->next = nullptr;
                        // free_list 对 left 的引用计数减一
                        dec_block_ref(left);

                        // 2) 把 ptr 合并到 left 上，形成新的大块
                        ptr->is_free = true;

                        // 合并结果以 left 为基
                        left->size += ptr->size;
                        left->end = left->start + left->size;

                        // 3) 从 free_list_ 上摘除 ptr（如果还在）
                        if (ptr->last)
                            ptr->last->next = ptr->next;
                        else if (free_list_ == ptr)
                            free_list_ = ptr->next;
                        if (ptr->next)
                            ptr->next->last = ptr->last;
                        ptr->last = nullptr;
                        ptr->next = nullptr;
                        // free_list 对 ptr 的引用计数减一
                        dec_block_ref(ptr);

                        // 4) 释放 ptr 的元数据槽
                        if (ptr->box_index >= 0
                            && ptr->box_index < static_cast<int>(bockBox.size())
                            && bockBox[ptr->box_index].get() == ptr) {
                            bockBox[ptr->box_index].reset();
                        }

                        // 5) 合并后用 left 作为新的 ptr 继续尝试
                        ptr = left;

                        // 注意：此处不重建映射和 bin，统一放在循环结束后处理
                        merged = true;
                        continue;
                    }
                }

                // --- 右合并：start_map 中以 ptr->end 为 start 的块 ---
                auto itR = start_map.find(ptr->end);
                if (itR != start_map.end()) {
                    MemoryBlock* right = itR->second;
                    if (right && right != ptr && right->is_free && right->start == ptr->end) {
                        // 1) 从所有结构中摘除 right

                        remove_start_map(right);
                        remove_end_map(right);

                        if (right->bin_index >= 0)
                            remove_from_bins(right);

                        if (right->last)
                            right->last->next = right->next;
                        else if (free_list_ == right)
                            free_list_ = right->next;
                        if (right->next)
                            right->next->last = right->last;
                        right->last = nullptr;
                        right->next = nullptr;
                        dec_block_ref(right);

                        // 2) 合并 right 到 ptr 上
                        ptr->size += right->size;
                        ptr->end = ptr->start + ptr->size;

                        // 3) 释放 right 的元数据槽
                        if (right->box_index >= 0
                            && right->box_index < static_cast<int>(bockBox.size())
                            && bockBox[right->box_index].get() == right) {
                            bockBox[right->box_index].reset();
                        }

                        // 同样，这里暂不重建映射/bin，统一在循环结束后处理
                        merged = true;
                        continue;
                    }
                }
            }

            // 合并完毕：确保 ptr 处于一致状态

            // 1) 确保在 free_list_ 上（按地址排序插入）
            if (!is_in_free_list(ptr)) {
                ptr->last = nullptr;
                ptr->next = nullptr;
                insert_free_list_sorted(ptr);
            }

            // 2) 统一重建映射与 bins
            remove_start_map(ptr);
            remove_end_map(ptr);
            if (ptr->bin_index >= 0)
                remove_from_bins(ptr);
            add_start_map(ptr);
            add_end_map(ptr);
            insert_bin(ptr);
            // 更新最大空闲块
            if (ptr->is_free && ptr->size > max_free_size_)
                max_free_size_ = ptr->size;
            try_clean_delayed_slots();

            return ptr;
        }
            void add_start_map(MemoryBlock* mb) {
#ifdef _MEMDEBUG
                if (mb && mb->box_index >= 0 && mb->box_index < bockBox.size()) {
                    MemoryBlock* slotPtr = bockBox[mb->box_index].get();
                    if (slotPtr && slotPtr != mb) {
                        Cout() << "DIAG: add_start_map: box_index mismatch on add. box_index=" << mb->box_index
                            << " mb=" << (void*)mb << " bockBox[" << mb->box_index << "]=" << (void*)slotPtr << "\n";
                        DebugPrintStack();
                    }
                }
#endif
                if (!mb) return;
                auto it = start_map.find(mb->start);
                if (it == start_map.end() || it->second != mb) {
                    start_map[mb->start] = mb;
                    inc_block_ref(mb);
                }
            }
            void remove_start_map(MemoryBlock* mb) {
#ifdef _MEMDEBUG
                if (mb && mb->box_index >= 0 && mb->box_index < bockBox.size()) {
                    MemoryBlock* slotPtr = bockBox[mb->box_index].get();
                    if (slotPtr && slotPtr != mb) {
                        Cout() << "DIAG: remove_start_map: box_index mismatch on remove. box_index=" << mb->box_index
                            << " mb=" << (void*)mb << " bockBox[" << mb->box_index << "]=" << (void*)slotPtr << "\n";
                        DebugPrintStack();
                    }
                }
#endif
                if (!mb) return;
                auto it = start_map.find(mb->start);
                if (it != start_map.end() && it->second == mb) {
                    //Cout() << "remove_start_map: removing start_map for box_index=" << mb->box_index
                    //    << " start=" << (void*)mb->start << " size=" << mb->size << "\n";
                    start_map.erase(it);
                    dec_block_ref(mb);
                    // 合适位置尝试清理 delayed slots（caller 通常持有 mutex_）
                    try_clean_delayed_slots();
                    //debug_check_ref_balance(mb);
                }
            }
            void add_end_map(MemoryBlock* mb) {
                if (!mb) return;
                auto it = end_map.find(mb->end);
                if (it == end_map.end() || it->second != mb) {
                    //Cout() << "add_end_map: adding end_map for box_index=" << mb->box_index
                    //    << " end=" << (void*)mb->end << " size=" << mb->size << "\n";
                    end_map[mb->end] = mb;
                    inc_block_ref(mb);
                    //debug_check_ref_balance(mb);
                }
            }
            void remove_end_map(MemoryBlock* mb) {
                if (!mb) return;
                auto it = end_map.find(mb->end);
                if (it != end_map.end() && it->second == mb) {
                    // Cout() << "remove_end_map: removing end_map for box_index=" << mb->box_index
                    //     << " end=" << (void*)mb->end << " size=" << mb->size << "\n";
                    end_map.erase(it);
                    dec_block_ref(mb);
                    // 合适位置尝试清理 delayed slots（caller 通常持有 mutex_）
                    try_clean_delayed_slots();
                    //debug_check_ref_balance(mb);
                }
            }

            MemoryBlock* allocate_new_block(size_t block_size, char* block_start) {
                int slot = -1;

                // NOTE: this function assumes caller holds main mutex_.
                // 为减少死锁风险，移除了内部对 mutex_ 的重复加锁。
                // 清理 delayed slots 的责任由 caller 在持锁时完成（必要时在调用前调用 try_clean_delayed_slots）。

                // 现在尝试从 free_slot_stack 取出可复用槽（需要 free_slot_mutex_）
                {
                    std::lock_guard<std::mutex> __free__(free_slot_mutex_);
#if !defined(_MEMDEBUG)
                    // 循环弹出直到找到一个真正可复用的槽，或栈为空
                    while (!free_slot_stack.empty()) {
                        int cand = free_slot_stack.back();
                        free_slot_stack.pop_back();
                        if (cand < 0 || cand >= bockBox.size()) continue;
                        auto& ref = bockBox[cand];
                        MemoryBlock* existing = ref.get();
                        if (!existing) {
                            slot = cand;
                            break;
                        }
                        // 若 existing 已脱链且无引用，也可复用（防御性检查）
                        bool still_linked = false;
                        if (existing->last || existing->next || existing->bin_index >= 0)
                            still_linked = true;
                        else {
                            auto itS = start_map.find(existing->start);
                            if (itS != start_map.end() && itS->second == existing) still_linked = true;
                            auto itE = end_map.find(existing->end);
                            if (itE != end_map.end() && itE->second == existing) still_linked = true;
                        }
                        if (!still_linked && existing->ref_count.load(std::memory_order_acquire) == 0) {
                            slot = cand;
                            break;
                        }
                        // 否则跳过该槽（可能短期内仍在使用）
                    }
#else
                    // Debug-mode: 禁止立即复用已释放槽，改为总是创建新条目以避免并发下的 use-after-free
                    // 这会使 bockBox 增长更快，但能稳定复现并定位并发缺陷。
                    if (!free_slot_stack.empty()) {
                        Cout() << "DEBUG: _MEMDEBUG active - skipping reuse of free_slot_stack (size="
                            << free_slot_stack.size() << ")\n";

                    }
#endif
                }

                if (slot >= 0 && static_cast<size_t>(slot) < bockBox.size()) {
                    auto& ref = bockBox[slot];
                    // 防御性检查：若槽仍非空或被引用，则不要盲目复用
                    MemoryBlock* existing = ref.get();
                    if (existing && existing->ref_count.load(std::memory_order_acquire) != 0) {
                        // 槽仍被引用，跳过复用，创建新条目
                        Cout() << "allocate_new_block: slot " << slot << " still in use (ref_count=" << existing->ref_count.load() << "), skipping reuse\n";
                    }
                    else {
                        // 槽为空或可复用
                        ref.reset(new MemoryBlock(block_size, block_start));
                        MemoryBlock* mb = ref.get();
                        mb->box_index = slot;
                        return mb;
                    }
                }

                // 无可复用槽或复用被跳过：创建新条目
                auto& ref = bockBox.emplace_back();
                ref.reset(new MemoryBlock(block_size, block_start));
                MemoryBlock* mb = ref.get();
                mb->box_index = bockBox.size() - 1;
#ifdef _MEMDEBUG
                // 诊断：如果 bockBox 过大，吐出警告以便排查元数据泄漏
                const int BOX_WARN = 16384;
                if (bockBox.size() > BOX_WARN) {
                    Cout() << "WARNING: bockBox count = " << bockBox.size() << " (created new slot " << mb->box_index << ")\n";
                }
#endif // _MEMDEBUG
                return mb;
            }
#ifdef _MEMDEBUG
            void remove_bockbox_index(int idx)
            {
                if (idx < 0 || idx >= bockBox.size()) return;

                // 在主锁下标记或回收槽位
                std::lock_guard<std::mutex> __(mutex_);
                auto& slot = bockBox[idx];
                MemoryBlock* mb = slot.get();
                if (!mb) return;

                // 尝试主动解除所有容器引用，尽可能把元数据完全脱链，从而可以立即回收。
                // 这些操作都在持有 mutex_ 的上下文中执行，安全且可见性良好。

                // 1) 从 bins 中移除（如果在）
                if (mb->bin_index >= 0)
                    remove_from_bins(mb); // 内部会 dec_block_ref 并尝试清理 delayed slots

                // 2) 从映射中移除（如果在）
                remove_start_map(mb); // 会 dec_block_ref 并尝试清理 delayed slots
                remove_end_map(mb);   // 会 dec_block_ref 并尝试清理 delayed slots

                // 3) 从 free_list / used_list 中移除（任一）
                // 尝试从 free_list 中安全移除（如果存在）
                safe_remove_from_free_list(free_list_, mb);
                // 尝试从 used_list 中移除（如果存在）
                remove_used_list(mb);

                // 重新判断是否仍被链/映射引用
                bool still_linked = false;
                if (mb->last || mb->next || mb->bin_index >= 0)
                    still_linked = true;
                else {
                    auto itS = start_map.find(mb->start);
                    if (itS != start_map.end() && itS->second == mb) still_linked = true;
                    auto itE = end_map.find(mb->end);
                    if (itE != end_map.end() && itE->second == mb) still_linked = true;
                }

                // 如果完全脱链并且无引用计数，直接回收槽位
                if (!still_linked && mb->ref_count.load(std::memory_order_acquire) == 0) {
                    // 最终再一次保险检查：确保没有任何容器或链表仍引用该块（防止竞态下的悬挂指针）
                    int holders_final = estimate_holders(mb);
                    if (holders_final > 0) {
                        // 发生了竞态：尽管我们认为已脱链，但仍检测到持有者，转为延迟回收
#ifdef _MEMDEBUG
                        Cout() << "remove_bockbox_index: deferred clearing slot=" << idx
                            << " box_index=" << mb->box_index << " holders_final=" << holders_final << "\n";
                        debug_check_ref_balance(mb);
#endif
                        bool already = false;
                        for (int j = 0; j < delayed_free_slots_.size(); ++j) {
                            if (delayed_free_slots_[j] == idx) { already = true; break; }

                        }
                        if (!already) delayed_free_slots_.push_back(idx);
                        // 尝试清理其它可能已就绪的延迟槽
                        try_clean_delayed_slots();
                        return;
                    }
#ifdef _MEMDEBUG
                    // 诊断输出：在清槽前打印映射/引用上下文
                    Cout() << "DIAG: remove_bockbox_index: clearing slot=" << idx
                        << " mb=" << (void*)mb << " start=" << (void*)mb->start << " end=" << (void*)mb->end
                        << " ref=" << mb->ref_count.load() << " holders_final=" << holders_final << "\n";
                    Cout() << "  start_map contains:";
                    for (auto& p : start_map) Cout() << " [" << (void*)p.first << "=" << (void*)p.second << "]";
                    Cout() << "\n  end_map contains:";
                    for (auto& p : end_map) Cout() << " [" << (void*)p.first << "=" << (void*)p.second << "]";
                    Cout() << "\n";
#endif
                    // 直接清槽并放回 free stack（去重）
                    slot.reset();
#if !defined(_MEMDEBUG)
                    std::lock_guard<std::mutex> __free__(free_slot_mutex_);
                    bool exists = false;
                    for (int j = 0; j < free_slot_stack.size(); ++j) {
                        if (free_slot_stack[j] == idx) { exists = true; break; }
                    }
                    if (!exists)
                        free_slot_stack.push_back(idx);
#else
                    // Debug: skip immediate reuse — leave empty slot to be reclaimed by try_clean_delayed_slots
                    Cout() << "SLOT-CLEAR: idx=" << idx
                        << " mb=" << (void*)mb
                        << " start=" << (void*)mb->start
                        << " size=" << mb->size << "\n";
#endif
                    // 同时从 delayed_free_slots_ 中移除（若存在）
                    for (int i = delayed_free_slots_.size() - 1; i >= 0; --i) {
                        if (delayed_free_slots_[i] == idx)
                            delayed_free_slots_.erase(delayed_free_slots_.begin() + i);
                    }
                    // 尝试清理其它可能已就绪的延迟槽
                    try_clean_delayed_slots();
                    return;
                }
                else {
                    // 仍被引用或仍关联结构：加入延迟回收队列（若尚未加入）
                    bool already = false;
                    for (int j = 0; j < delayed_free_slots_.size(); ++j) {
                        if (delayed_free_slots_[j] == idx) { already = true; break; }
                    }
                    if (!already)
                        delayed_free_slots_.push_back(idx);
#ifdef _MEMDEBUG
                    // 诊断：若延迟队列快速增长则打印状态帮助定位
                    const int WARN_THRESHOLD = 1024;
                    if (delayed_free_slots_.size() > WARN_THRESHOLD) {
                        Cout() << "WARNING: delayed_free_slots_ size = " << delayed_free_slots_.size()
                            << " (idx delayed=" << idx << ") -- dumping internal state\n";
                    }
#endif // _MEMDEBUG
                    // 尝试立即清理（当前持有 mutex_，所以安全）
                    try_clean_delayed_slots();
                }
            }
#else
            // Release / 非调试构建：直接清理并立即把 idx 放入 free_slot_stack，避免复杂延迟逻辑
            void remove_bockbox_index(int idx)
            {
                if (idx < 0 || idx >= (int)bockBox.size())
                    return;

                std::lock_guard<std::mutex> __(mutex_);
                auto& slot = bockBox[idx];
                MemoryBlock* mb = slot.get();
                if (!mb)
                    return;

                // 防御性脱链：从 bins / maps / lists 中摘除，避免悬挂指针
                if (mb->bin_index >= 0)
                    remove_from_bins(mb);
                remove_start_map(mb);
                remove_end_map(mb);
                safe_remove_from_free_list(free_list_, mb);
                remove_used_list(mb);

                // 清空槽位
                slot.reset();

                // 将 idx 放回 free_slot_stack，供 allocate_new_block 复用
                std::lock_guard<std::mutex> __free__(free_slot_mutex_);
                free_slot_stack.push_back(idx);
            }
#endif

            void remove_from_bins(MemoryBlock* block) {
                if (!block) return;
                if (bins.size() != BIN_COUNT) return;
                if (block->bin_index < 0) return;

                int b = block->bin_index;
                MemoryBlock* cur = bins[b];
                MemoryBlock* prev = nullptr;
                while (cur) {
                    if (cur == block) {
                        if (prev)
                            prev->bin_next = cur->bin_next;
                        else
                            bins[b] = cur->bin_next;
                        cur->bin_next = NULL;
                        block->bin_index = -1;
                        // 诊断：记录 remove_from_bins 意图
                        //Cout() << "remove_from_bins: removing box_index=" << cur->box_index << " from bin=" << b << "\n";
                        // 减少引用
                        dec_block_ref(cur);
                        // 合适位置尝试清理 delayed slots（caller 通常持有 mutex_）
                        try_clean_delayed_slots();
                        //debug_check_ref_balance(cur);
                        return;
                    }
                    prev = cur;
                    cur = cur->bin_next;
                }
                // 如果未找到，不要更改 block->bin_index（保留原值以便诊断）
            }
            int  choose_bin(size_t size) {
                static const size_t classes[BIN_COUNT] = {
        16, 32, 64, 128, 256, 512, 1024, 2048,
        4096, 8192, 16384, 32768,
        65536, 131072, 262144, 524288
                };
                for (int i = 0; i < BIN_COUNT; i++) {
                    if (size <= classes[i]) return i;
                }
                return BIN_COUNT - 1;
            }
            void insert_bin(MemoryBlock* block) {
                if (!block) return;
                if (bins.size() != BIN_COUNT) {
                    bins.resize(BIN_COUNT);
                    for (int i = 0; i < BIN_COUNT; i++)
                        bins[i] = nullptr;
                }
                if (!block->is_free)
                    return;
                if (block->bin_index >= 0)
                    remove_from_bins(block);

                int bin = choose_bin(block->size);
                // 把新的空闲块插入到 bin 头，确保 bin_next 不含旧值
                block->bin_next = bins[bin];
                bins[bin] = block;
                block->bin_index = bin;
                // 诊断：记录 insert_bin 意图
                //Cout() << "insert_bin: inserting box_index=" << block->box_index << " size=" << block->size << "\n";
                // 增加引用：分配给 bin 持有一个引用
                inc_block_ref(block);
                //debug_check_ref_balance(block);
            }
            MemoryBlock* find_bin(size_t size) {
                if (bins.size() != BIN_COUNT)
                    return nullptr;

                int bin = choose_bin(size);
                for (int b = bin; b < BIN_COUNT; ++b) {
                    MemoryBlock* cur = bins[b];
                    MemoryBlock* prev = nullptr;
                    while (cur) {
                        MemoryBlock* next = cur->bin_next;
                        if (cur->is_free && cur->size >= size) {
                            // 从 bin 链中摘除
                            if (prev) prev->bin_next = next;
                            else      bins[b] = next;
                            cur->bin_next = nullptr;
                            cur->bin_index = -1;
                            // 减少 bin 对该块的引用
                            dec_block_ref(cur);

                            return cur;
                        }
                        prev = cur;
                        cur = next;
                    }
                }
                return nullptr;
            }

            void safe_remove_from_free_list(MemoryBlock*& free_list, MemoryBlock* block) {
                if (!block || !free_list) return;
                if (free_list == block) {
                    free_list = block->next;
                    if (free_list) free_list->last = nullptr;
                    block->last = nullptr;
                    block->next = nullptr;
                    // 诊断
                    //Cout() << "safe_remove_from_free_list: removing head box_index=" << block->box_index << "\n";
                    // 从 free_list 摘除，减少引用
                    dec_block_ref(block);
                    // 合适位置尝试清理 delayed slots（caller 通常持有 mutex_）
                    try_clean_delayed_slots();
                    //debug_check_ref_balance(block);
                    return;
                }
                MemoryBlock* cur = free_list;
                size_t guard = 0;
                const size_t limit = 100000;
                while (cur) {
                    if (!releasing_ && !IsValidBlockPtr(cur)) {
                        Cout() << "MemoryPool: detected invalid free_list node at " << (void*)cur
                            << " (box_index=" << (cur ? cur->box_index : -1) << "). Aborting traversal.\n";
                        Cout() << "start_map size: " << start_map.size() << ", end_map size: " << end_map.size() << "\n";
                        int tot = bockBox.size();
                        for (int i = 0; i < tot && i < 8; ++i) {
                            auto& one = bockBox[i];
                            MemoryBlock* mb = one.get();
                            Cout() << "bockBox[" << i << "] = " << (void*)mb;
                            if (mb) Cout() << " start=" << (void*)mb->start << " size=" << mb->size << " box_index=" << mb->box_index;
                            Cout() << "\n";
                        }
                        // 额外诊断：打印待移除 block 的链接信息
                        Cout() << "safe_remove_from_free_list: attempted remove block=" << (void*)block
                            << " box_index=" << (block ? block->box_index : -1) << " last=" << (void*)(block ? block->last : nullptr)
                            << " next=" << (void*)(block ? block->next : nullptr) << "\n";
#ifdef _MEMDEBUG
                        DebugPrintStack();
#endif // _MEMDEBUG
                        return;
                    }

                    if (cur == block) {
                        if (cur->last) cur->last->next = cur->next;
                        if (cur->next) cur->next->last = cur->last;
                        cur->last = nullptr;
                        cur->next = nullptr;
                        //Cout() << "safe_remove_from_free_list: removing box_index=" << cur->box_index << " from free list\n";
                        // 从 free_list 摘除，减少引用
                        dec_block_ref(cur);
                        // 合适位置尝试清理 delayed slots（caller 通常持有 mutex_）
                        try_clean_delayed_slots();
                        //debug_check_ref_balance(cur);
                        return;
                    }
                    cur = cur->next;
                    if (++guard > limit) break;
                }
                // 如果未找到，忽略（块可能已摘除）
            }
            MemoryBlock* split_block(MemoryBlock* block, size_t allocated_size) {
                if (!block || !block->is_free || block->size <= allocated_size)
                    ThrowRuntime("Invalid split request");

                // Caller must hold mutex_. 尝试清理 delayed slots（保持与 allocate_new_block 的原来语义）
                try_clean_delayed_slots();

                // 移除旧边界映射 -> dec 映射引用
                remove_start_map(block);
                remove_end_map(block);
                if (block->bin_index >= 0)
                    remove_from_bins(block);
                // 创建已分配的新块元数据（位于原块起始处）
                MemoryBlock* new_block = allocate_new_block(allocated_size, block->start);
                if (!new_block)
                    ThrowRuntime("Failed to allocate metadata slot");

                // 设置 new_block 的元信息
                new_block->is_free = false;
                new_block->last = nullptr;
                new_block->next = nullptr;
                new_block->start = block->start;
                new_block->size = allocated_size;
                new_block->end = new_block->start + allocated_size;
                new_block->bin_index = -1;
                new_block->bin_next = nullptr;

                // 调整原空闲块范围：移除前面 allocated_size 大小的区域
                block->start += allocated_size;
                block->size -= allocated_size;
                block->end = block->start + block->size;

                if (block->size == 0) {
                    // 剩余空间为 0：block 不再有效，直接清槽
                    if (block->box_index >= 0
                        && block->box_index < static_cast<int>(bockBox.size())
                        && bockBox[block->box_index].get() == block) {
                        bockBox[block->box_index].reset();
                    }
                    // 注意：调用处在调用 split_block 前，已经把 block 从 free_list_ 摘除了
                }
                else {
                    // 剩余 free 部分仍然作为一个空闲块回到 free_list_/bins/map
                    block->is_free = true;
                    block->last = nullptr;
                    block->next = nullptr;
                    insert_free_list_sorted(block);
                    add_start_map(block);
                    add_end_map(block);
                    insert_bin(block);
                    // 更新最大空闲块
                    if (block->size > max_free_size_)
                        max_free_size_ = block->size;
                }

                return new_block;
            }
            MemoryBlock* findadjacentblock(MemoryBlock* target, MemoryBlock* listblock, bool isnext = true) {
                if (!listblock || !target) return nullptr;
                MemoryBlock* bestFit = nullptr;
                size_t minGap = SIZE_MAX;
                MemoryBlock* current = listblock;
                do {
                    if (isnext) {
                        if (current->start >= target->end) {
                            size_t gap = current->start - target->end;
                            if (gap < minGap && gap >= target->size) {
                                minGap = gap;
                                bestFit = current;
                            }
                        }
                    }
                    else {
                        if (current->end <= target->start) {
                            size_t gap = target->start - current->end;
                            if (gap < minGap && gap >= target->size) {
                                minGap = gap;
                                bestFit = current;
                            }
                        }
                    }
                } while ((current = current->next));
                return bestFit;
            }

            void initialize_pool(size_t size) {
                size_t pool_size = size;
#if defined(PLATFORM_POSIX)
                size_t align = 64;
                if (size % align != 0)
                    pool_size = ((size + align - 1) / align) * align;
#else
                size_t align = 64;
                if (size % align != 0)
                    pool_size = ((size + align - 1) / align) * align;
#endif
#if defined(PLATFORM_POSIX)
                memory_block_ = static_cast<char*>(aligned_alloc(64, pool_size));
                if (!memory_block_)
                    ThrowRuntime("Failed to allocate memory pool");
#else
                memory_block_ = static_cast<char*>(_aligned_malloc(pool_size, 64));
                if (!memory_block_)
                    ThrowRuntime("Failed to allocate memory pool");
#endif
                // memset 返回值不应作为错误判定条件；仅在 memory_block_ 为 NULL 时会失败
                ::memset(memory_block_, 0, pool_size);

                // Create initial metadata and set free_list_ / bins / maps under main mutex_ to ensure consistency
                MemoryBlock* mb = nullptr;
                {
                    std::lock_guard<std::mutex> __(mutex_);
                    auto& new_block_ref = bockBox.emplace_back(std::make_unique<MemoryBlock>(pool_size, memory_block_));
                    mb = new_block_ref.get();
                    mb->box_index = static_cast<int>(bockBox.size()) - 1;

                    push_free_list_head(mb);
                    // 插入 bins（insert_bin 会对 block 再 inc）
                    insert_bin(mb);
                    // 维护邻接映射（在建立映射时增加引用）
                    add_start_map(mb);
                    add_end_map(mb);

                    // 初始化最大空闲块
                    max_free_size_ = pool_size;
                }
            }
            void initialize_stats() {
                stats_.total_allocations = 0;
                stats_.total_frees = 0;
                stats_.allocation_failures = 0;
                stats_.current_usage = 0;
                stats_.peak_usage = 0;
                max_free_size_ = 0;
                alloc_counter_ = 0;
            }
            uint64_t getAvailableSystemMemory() {
#if defined(PLATFORM_POSIX)
                auto file = ::FileIn("/proc/meminfo");
                if (!file.IsOK()) {
                    throw ::NetException(ExcType::MEMERR, -1, (int64)this, "The system does not exist (/proc/meminfo)");
                }
                String line;
                unsigned long long available_kB = 0;
                while (!(line = file.GetLine()).IsEmpty()) {
                    if (line.Find("MemAvailable:") != -1) {
                        sscanf(line, "MemAvailable: %llu kB", &available_kB);
                        break;
                    }
                }
                file.Close();
                if (available_kB == 0) {
                    throw ::NetException(ExcType::MEMERR, -1, (int64)this, "Incorrect memory size detection or insufficient memory");
                }
                return static_cast<uint64_t>(available_kB) * 1024ULL;
#else
                MEMORYSTATUSEX statex;
                ::ZeroMemory(&statex, sizeof(statex));
                statex.dwLength = sizeof(statex);
                if (!::GlobalMemoryStatusEx(&statex)) {
                    ThrowRuntime("Failed to query system memory (GlobalMemoryStatusEx)");
                }
                if (statex.ullAvailPhys == 0) {
                    ThrowRuntime("Incorrect memory size detection or insufficient memory");
                }
                return static_cast<uint64_t>(statex.ullAvailPhys);
#endif
            }
            size_t   calculateRecommendedBufferSize() {
                uint64_t available = getAvailableSystemMemory();
                if (available == 0) {
                    return 16 * 1024 * 1024;
                }
                uint64_t one_eighth = available / 8;
                uint64_t upper_limit = 256 * 1024 * 1024;
                size_t recommended_size = static_cast<size_t>((((one_eighth) < (upper_limit)) ? (one_eighth) : (upper_limit)));
                size_t lower_limit = 1 * 1024 * 1024;
                recommended_size = (((recommended_size) > (lower_limit)) ? (recommended_size) : (lower_limit));
                return recommended_size;
            }
            void     update_stats_deallocation(size_t size) {
                stats_.total_frees++;
                stats_.current_usage -= size;
            }
            void     update_stats_allocation(size_t size) {
                stats_.total_allocations++;
                stats_.current_usage += size;
                if (stats_.current_usage > stats_.peak_usage) {
                    stats_.peak_usage = stats_.current_usage;
                }
            }
            bool     remove_used_list(MemoryBlock* block) {
                if (!block) return false;
#ifdef _MEMDEBUG
                if (!releasing_ && !IsValidBlockPtr(this, block)) {
                    Cout() << "ASSERT: remove_used_list called with invalid block ptr=" << (void*)block
                        << " box_index=" << (block ? block->box_index : -1) << "\n";
                    DebugPrintStack();
                    assert(false);
                }
#endif
                if (used_list_ == block) {
                    //Cout() << "remove_used_list: removing head used box_index=" << block->box_index << "\n";
                    used_list_ = block->next;
                    if (used_list_) used_list_->last = nullptr;

                    block->last = nullptr;
                    block->next = nullptr;
                    // 从 used_list 移除，减少引用
                    dec_block_ref(block);
                    // 合适位置尝试清理 delayed slots（caller 通常持有 mutex_）
                    try_clean_delayed_slots();
                    //debug_check_ref_balance(block);
                    return true;
                }
                MemoryBlock* current = used_list_;
                size_t guard = 0;
                const size_t limit = (bockBox.size() > 0 ? bockBox.size() * 4 : 100000);
                while (current) {
                    if (current == block) {
                        //Cout() << "remove_used_list: removing box_index=" << current->box_index << "\n";

                        if (current->last) current->last->next = current->next;
                        if (current->next) current->next->last = current->last;
                        current->last = nullptr;
                        current->next = nullptr;
                        // 从 used_list 移除，减少引用
                        dec_block_ref(current);
                        // 合适位置尝试清理 delayed slots（caller 通常持有 mutex_）
                        try_clean_delayed_slots();
                        //debug_check_ref_balance(current);
                        return true;
                    }
                    current = current->next;
                    if (++guard > limit) break;
                }
                // 未找到 —— 这在正常流程中不应该发生
#ifdef _MEMDEBUG
                if (!releasing_)
                {
                    Cout() << "ASSERT: remove_used_list could not find block in used_list_. ptr="
                        << (void*)block << " box_index=" << block->box_index << "\n";
                    bool inFree = is_in_free_list(this, block);
                    bool inUsed = is_in_used_list(this, block);
                    Cout() << "  reachability: in_free_list=" << (inFree ? "YES" : "NO")
                        << " in_used_list=" << (inUsed ? "YES" : "NO") << "\n";
                    DebugPrintStack();
                    assert(false);
                }
#endif
                return false;
            }
#ifdef _MEMDEBUG
            void try_clean_delayed_slots()
            {
                // Caller MUST hold mutex_ when calling this function.
                if (delayed_free_slots_.empty())
                    return;

                // 我们需要在修改 free_slot_stack 时持 free_slot_mutex_。
                // 但访问 bockBox/start_map/end_map 必须在 caller 持有的 main mutex_ 下进行（已假定）。
                std::lock_guard<std::mutex> __free__(free_slot_mutex_);

                // 逐项检查 delayed_free_slots_，删除可安全回收的槽位。
                for (int i = 0; i < delayed_free_slots_.size(); ) {
                    int idx = delayed_free_slots_[i];
                    // 无效索引直接移除
                    if (idx < 0 || idx >= bockBox.size()) {
                        delayed_free_slots_.erase(delayed_free_slots_.begin() + i);
                        continue;
                    }

                    auto& slot = bockBox[idx];
                    MemoryBlock* mb = slot.get();

                    // 槽已被清空：把索引放回可用栈（去重）
                    if (!mb) {
                        bool exists = false;
                        for (int j = 0; j < free_slot_stack.size(); ++j) {
                            if (free_slot_stack[j] == idx) { exists = true; break; }
                        }
                        if (!exists)
                            free_slot_stack.push_back(idx);
                        delayed_free_slots_.erase(delayed_free_slots_.begin() + i);
                        continue;
                    }

                    // 只有在元数据完全脱链且不被映射引用时才可回收
                    bool still_linked = false;
                    if (mb->last || mb->next || mb->bin_index >= 0)
                        still_linked = true;
                    else {
                        auto itS = start_map.find(mb->start);
                        if (itS != start_map.end() && itS->second == mb) still_linked = true;
                        auto itE = end_map.find(mb->end);
                        if (itE != end_map.end() && itE->second == mb) still_linked = true;
                    }

                    if (!still_linked) {
                        int refv = mb->ref_count.load(std::memory_order_acquire);
                        if (refv == 0) {
                            // 安全回收：清空槽位（会析构 One 内对象），并把 idx 放入 free_slot_stack（去重）
                            slot = nullptr;
                            bool exists = false;
                            for (int j = 0; j < free_slot_stack.size(); ++j) {
                                if (free_slot_stack[j] == idx) { exists = true; break; }
                            }
                            if (!exists)
                                free_slot_stack.push_back(idx);
                            delayed_free_slots_.erase(delayed_free_slots_.begin() + i);
                            continue;
                        }

                        // 诊断与补救：若 ref_count>0 且结构上看不到任何持有者，则可能是引用计数不一致导致的“幽灵”引用。
                        // 在这种情况下基于持有者估算并在确认 holders==0 时，将 ref_count 纠正为 0 以允许回收。
                        int holders = estimate_holders(mb);
                        if (holders == 0) {
#ifdef _MEMDEBUG
                            Cout() << "FORCE-CLEAR-REF: box_index=" << mb->box_index
                                << " ref_count=" << refv << " holders_est=" << holders
                                << " start=" << (void*)mb->start << " end=" << (void*)mb->end << "\n";
                            Cout() << "try_clean_delayed_slots: correcting ghost-ref by force-dec to zero. box_index="
                                << mb->box_index << "\n";
#endif
                            // 此时结构上已完全脱链（still_linked == false 且 holders == 0），
                            // 仅剩 ref_count > 0 的“幽灵引用”，可以安全地将其减到 0。
                            int v = mb->ref_count.load(std::memory_order_acquire);
                            while (v > 0) {
                                dec_block_ref(mb);
                                v = mb->ref_count.load(std::memory_order_acquire);
                            }

                            // 现在 ref_count 已为 0，按正常 0 引用路径回收槽位
                            slot.reset();
#ifdef _MEMDEBUG
                            Cout() << "SLOT-CLEAR: idx=" << idx
                                << " mb=" << (void*)mb
                                << " start=" << (void*)mb->start
                                << " size=" << mb->size << "\n";
#endif
                            bool exists = false;
                            for (int j = 0; j < free_slot_stack.size(); ++j) {
                                if (free_slot_stack[j] == idx) { exists = true; break; }
                            }
                            if (!exists)
                                free_slot_stack.push_back(idx);
                            delayed_free_slots_.erase(delayed_free_slots_.begin() + i);
                            continue;

                        }
                    }

                    // 还不能回收，跳到下一个
                    ++i;
                }
#ifdef _MEMDEBUG
                // 诊断：若仍有延迟槽，逐项打印并调用 ref-balance 检查，便于定位来源
                if (!delayed_free_slots_.empty()) {
                    Cout() << "DEBUG: delayed_free_slots_ remain: " << delayed_free_slots_.size() << "\n";
                    for (int k = 0; k < delayed_free_slots_.size(); ++k) {
                        int idx = delayed_free_slots_[k];
                        if (idx < 0 || idx >= bockBox.size()) {
                            Cout() << "  delayed slot invalid idx=" << idx << "\n";
                            continue;
                        }
                        auto& slot = bockBox[idx];
                        MemoryBlock* mb = slot.get();
                        if (!mb) {
                            Cout() << "  delayed slot[" << idx << "] empty\n";
                            continue;
                        }
                        Cout() << "  delayed slot[" << idx << "] ptr=" << (void*)mb
                            << " box_index=" << mb->box_index
                            << " ref=" << mb->ref_count.load()
                            << " is_free=" << mb->is_free
                            << " bin_index=" << mb->bin_index
                            << " start=" << (void*)mb->start << " size=" << mb->size
                            << " last=" << (void*)mb->last << " next=" << (void*)mb->next << "\n";
                        // 调用已有的诊断函数，打印 holders 估算并在不一致时输出上下文
                        debug_check_ref_balance(mb);
                    }
                }
#endif
            }
#else
            // Release / 非调试构建：不使用 delayed 机制，空实现即可
            void try_clean_delayed_slots()
            {
                // no-op in release
            }
#endif
            void     debug_check_ref_balance(MemoryBlock* mb) {
                if (!mb) return;
                // Only run in debug/diagnostic runs
#ifdef _MEMDEBUG
    // Count holders by inspecting structures (caller should hold mutex_)
                int holders = 0;
                // in free_list?
                {
                    MemoryBlock* cur = free_list_;
                    size_t guard = 0;
                    const size_t limit = (bockBox.size() > 0 ? bockBox.size() * 4 : 100000);
                    while (cur && ++guard <= limit) {
                        if (!IsValidBlockPtr(cur)) {
                            Cout() << "INVALID NODE IN free_list_ detected at " << (void*)cur
                                << " (box_index=" << (cur ? cur->box_index : -1) << ")\n";
                            DebugPrintStack();
                            assert(false);
                            break;
                        }
                        if (cur == mb) { holders++; break; }
                        // 预检 next 指针
                        if (cur->next && !IsValidBlockPtr(cur->next)) {
                            Cout() << "INVALID next pointer in free_list_ node " << (void*)cur
                                << " -> next=" << (void*)cur->next << "\n";
                            DebugPrintStack();
                            assert(false);
                            break;
                        }
                        cur = cur->next;
                    }
                }
                // in used_list?
                {
                    MemoryBlock* cur = used_list_;
                    size_t guard = 0;
                    const size_t limit = (bockBox.size() > 0 ? bockBox.size() * 4 : 100000);
                    while (cur && ++guard <= limit) {
                        if (cur == mb) { holders++; break; }
                        cur = cur->next;
                    }
                }
                // in any bin?
                if (mb->bin_index >= 0) {
                    // count one holder for being in bin
                    holders++;
                }
                else {
                    // also scan bins to be safe (rare)
                    for (int b = 0; b < BIN_COUNT; ++b) {
                        MemoryBlock* cur = bins[b];
                        int guard = 0;
                        while (cur && ++guard < 10000) {
                            if (cur == mb) { holders++; goto after_bins; }
                            cur = cur->bin_next;
                        }
                    }
                }
            after_bins:;

                // in start_map?
                for (auto& p : start_map) {
                    if (p.second == mb) { holders++; break; }
                }
                // in end_map?
                for (auto& p : end_map) {
                    if (p.second == mb) { holders++; break; }
                }

                int refv = mb->ref_count.load(std::memory_order_acquire);
                if (refv != holders) {
                    Cout() << "REF-MISMATCH: box_index=" << mb->box_index
                        << " ref_count=" << refv << " holders_est=" << holders
                        << " start=" << (void*)mb->start << " end=" << (void*)mb->end
                        << " bin_index=" << mb->bin_index
                        << " last=" << (void*)mb->last << " next=" << (void*)mb->next
                        << "\n";
                    // 显示 bockBox 指向，若不同则说明 slot 被重分配或已清空
                    if (mb->box_index >= 0 && mb->box_index < bockBox.size()) {
                        Cout() << "  bockBox[" << mb->box_index << "] = " << (void*)bockBox[mb->box_index].get() << "\n";
                    }
                    // 额外的 reachability 检查，帮助定位链表与映射的不一致
                    bool inFree = is_in_free_list(this, mb);
                    bool inUsed = is_in_used_list(this, mb);
                    Cout() << "  reachability: in_free_list=" << (inFree ? "YES" : "NO")
                        << " in_used_list=" << (inUsed ? "YES" : "NO") << "\n";
                    DebugPrintStack();
                    // Print more context to help locate path
                    Cout() << "  start_map contains:";
                    for (auto& p : start_map) Cout() << " [" << (void*)p.first << "=" << (void*)p.second << "]";
                    Cout() << "\n  end_map contains:";
                    for (auto& p : end_map) Cout() << " [" << (void*)p.first << "=" << (void*)p.second << "]";
                    Cout() << "\n  bins heads:";
                    for (int b = 0; b < BIN_COUNT; ++b) Cout() << " bin[" << b << "]=" << (void*)bins[b];
                    Cout() << "\n";
                    // existing diagnostics...
                    // 新增：打印最近若干条 inc/dec 事件
                    {
                        std::lock_guard<std::mutex> g(dbg_hist_mutex);
                        Cout() << "=== RECENT INC/DEC EVENTS (most recent last) ===\n";
                        int start = (int)std::max<int64_t>(0, (int64_t)dbg_hist.size() - 50);
                        for (int i = start; i < (int)dbg_hist.size(); ++i)
                            Cout() << "  " << dbg_hist[i].c_str() << "\n";
                        Cout() << "=== END RECENT EVENTS ===\n";
                    }
                    //ASSERT(false);
                }
#endif
            }

            void inc_block_ref(MemoryBlock* mb) {
                if (!mb) return;

                // 最大允许引用数（安全上限，可根据需要调整）
                // 该值应大于合法持有者数量，但远小于异常膨胀时的值。
                static const int MAX_REF = 64;

                // 乐观地尝试增加引用；如果超过上限，回退并记录诊断
                int newv = mb->ref_count.fetch_add(1, std::memory_order_acq_rel) + 1;
#ifdef _MEMDEBUG
                ++dbg_inc_count;
                // 追加诊断日志：线程 id + box_index + 新引用值
                {
                    size_t tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
                    Cout() << "inc_block_ref: thread=" << tid
                        << " box_index=" << mb->box_index
                        << " new_ref=" << newv
                        << " start=" << (void*)mb->start << " size=" << mb->size << "\n";
                }
                // 额外诊断：当从0->1，或对已标记为 free 的块做 inc 时打印堆栈
                if (newv == 1 || mb->is_free) {
                    Cout() << "DIAG: inc_block_ref suspicious event: box_index=" << mb->box_index
                        << " new_ref=" << newv << " is_free=" << mb->is_free << " start=" << (void*)mb->start << "\n";
                    DebugPrintStack();
                }
                // 记录到事件缓冲区（便于 later trace）
                {
                    char buf[512];
                    auto now = std::chrono::system_clock::now();
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                    snprintf(buf, sizeof(buf), "INC  t=%llu tid=%llu box=%d new=%d start=%p size=%zu",
                        (unsigned long long)ms,
                        (unsigned long long)std::hash<std::thread::id>{}(std::this_thread::get_id()),
                        mb->box_index, newv, (void*)mb->start, mb->size);
                    RecordDbgEvent(buf);
                }
#endif
                if (newv > MAX_REF) {
                    // 回退这次增加，防止不受控膨胀
                    int prev = mb->ref_count.fetch_sub(1, std::memory_order_acq_rel);
#ifdef _MEMDEBUG
                    Cout() << "inc_block_ref: prevented overflow on box_index=" << mb->box_index
                        << " current_ref=" << prev << " attempted_new=" << newv
                        << " start=" << (void*)mb->start << " size=" << mb->size << "\n";
                    Cout() << "Stack trace for inc_block_ref overflow:\n";
                    DebugPrintStack();
#endif
                    return;
                }
            }
            void dec_block_ref(MemoryBlock* mb) {
                if (!mb) return;

                // 使用 CAS 循环，避免在并发下出现 transient negative 或 underflow
                int expected = mb->ref_count.load(std::memory_order_acquire);
                for (;;) {
                    if (expected <= 0) {
#ifdef _MEMDEBUG
                        size_t tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
                        Cout() << "dec_block_ref: prevented underflow on box_index=" << mb->box_index
                            << " thread=" << tid
                            << " prev=" << expected << " start=" << (void*)mb->start << " size=" << mb->size << "\n";
                        DebugPrintStack();
                        //ASSERT(false);
#endif
                        return;

                    }
                    int desired = expected - 1;
                    if (mb->ref_count.compare_exchange_weak(expected, desired,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire)) {
#ifdef _MEMDEBUG
                        ++dbg_dec_count;
                        // 追加诊断日志：线程 id + box_index + 新引用值
                        {
                            size_t tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
                            Cout() << "dec_block_ref: thread=" << tid
                                << " box_index=" << mb->box_index
                                << " new_ref=" << desired
                                << " start=" << (void*)mb->start << " size=" << mb->size << "\n";
                        }
                        {
                            char buf[512];
                            auto now = std::chrono::system_clock::now();
                            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                            snprintf(buf, sizeof(buf), "DEC  t=%llu tid=%llu box=%d new=%d start=%p size=%zu",
                                (unsigned long long)ms,
                                (unsigned long long)std::hash<std::thread::id>{}(std::this_thread::get_id()),
                                mb->box_index, desired, (void*)mb->start, mb->size);
                            RecordDbgEvent(buf);
                        }
                        // 若引用降为0，打印栈以便追踪最后持有者路径
                        if (desired == 0) {
                            Cout() << "dec_block_ref: ref reached 0 for box_index=" << mb->box_index
                                << " — stack trace:\n";
                            DebugPrintStack();
                        }
#endif
                        return;
                    }
                }
            }
            int  estimate_holders(MemoryBlock* mb) {
                if (!mb) return 0;

                int holders = 0;

                // 在调用处应持有 main mutex_，此处不再次加锁以避免死锁。
                // 1) 在 free_list?
                {
                    MemoryBlock* cur = free_list_;
                    size_t guard = 0;
                    const size_t limit = (bockBox.size() > 0 ? bockBox.size() * 4 : 100000);
                    while (cur && ++guard <= limit) {
                        // 环检测 / 指针合法性检查
                        if (!IsValidBlockPtr(cur)) {
#ifdef _MEMDEBUG
                            Cout() << "estimate_holders: invalid node in free_list_ at " << (void*)cur
                                << " (box_index=" << (cur ? cur->box_index : -1) << ")\n";
                            DebugPrintStack();
                            assert(false);
#endif
                            assert(false);
                            break;
                        }

                        if (++guard > limit) {
#ifdef _MEMDEBUG
                            Cout() << "estimate_holders: free_list_ traversal exceeded guard limit=" << limit
                                << " starting from head=" << (void*)free_list_
                                << " while searching for mb=" << (void*)mb
                                << " box_index=" << (mb ? mb->box_index : -1) << "\n";
                            DebugPrintStack();
                            assert(false);
#endif
                            assert(false);
                            break;
                        }
                        if (cur == mb) { holders++; break; }
                        cur = cur->next;
                    }
                }

                // 2) 在 used_list?
                {
                    MemoryBlock* cur = used_list_;
                    size_t guard = 0;
                    const size_t limit = (bockBox.size() > 0 ? bockBox.size() * 4 : 100000);
                    while (cur && ++guard <= limit) {
                        if (!IsValidBlockPtr(cur)) {
#ifdef _MEMDEBUG
                            Cout() << "estimate_holders: invalid node in used_list_ at " << (void*)cur
                                << " (box_index=" << (cur ? cur->box_index : -1) << ")\n";
                            DebugPrintStack();
                            assert(false);
#endif
                            assert(false);
                            break;
                        }

                        if (++guard > limit) {
#ifdef _MEMDEBUG
                            Cout() << "estimate_holders: used_list_ traversal exceeded guard limit=" << limit
                                << " starting from head=" << (void*)used_list_
                                << " while searching for mb=" << (void*)mb
                                << " box_index=" << (mb ? mb->box_index : -1) << "\n";
                            DebugPrintStack();
                            assert(false);
#endif
                            assert(false);
                            break;
                        }
                        if (cur == mb) { holders++; break; }
                        cur = cur->next;
                    }
                }

                // 3) 在 bins?
                if (mb->bin_index >= 0) {
                    holders++;
                }
                else {
                    for (int b = 0; b < BIN_COUNT; ++b) {
                        MemoryBlock* cur = bins[b];
                        int guard = 0;
                        while (cur && ++guard < 10000) {
                            if (cur == mb) { holders++; goto after_bins; }
                            cur = cur->bin_next;
                        }
                    }
                }
            after_bins:;

                // 4) 在 start_map?
                for (auto& p : start_map) {
                    if (p.second == mb) { holders++; break; }
                }
                // 5) 在 end_map?
                for (auto& p : end_map) {
                    if (p.second == mb) { holders++; break; }
                }

                return holders;
            }

            void push_free_list_head(MemoryBlock* mb) {
                if (!mb) return;
#ifdef _MEMDEBUG
                if (mb->last || mb->next) {
                    Cout() << "ASSERT: push_free_list_head called with already-linked block. box_index=" << mb->box_index
                        << " last=" << (void*)mb->last << " next=" << (void*)mb->next << "\n";
                    dump_internal_state();
                    DebugPrintStack();
                    assert(false);
                }
#endif
                // insert at head
                mb->last = nullptr;
                mb->next = free_list_;
                if (free_list_) free_list_->last = mb;
                free_list_ = mb;
                // free_list 持有引用
                inc_block_ref(mb);
                if (mb->is_free && mb->size > max_free_size_)
                    max_free_size_ = mb->size;
            }
            void insert_free_list_sorted(MemoryBlock* mb) {
#ifdef _MEMDEBUG
                if (mb->last || mb->next || is_in_free_list(this, mb)) {
                    Cout() << "ASSERT: insert_free_list_sorted called with already-linked block. box_index=" << mb->box_index
                        << " last=" << (void*)mb->last << " next=" << (void*)mb->next << "\n";
                    DebugPrintStack();
                    assert(false);
                }
#endif
                if (!mb) return;
                if (!free_list_) {
                    push_free_list_head(mb);
                    // 更新最大空闲块
                    if (mb->is_free && mb->size > max_free_size_)
                        max_free_size_ = mb->size;
                    return;
                }
                MemoryBlock* it = free_list_;
                MemoryBlock* prev = nullptr;
                size_t guard = 0;
                const size_t limit = (bockBox.size() > 0 ? bockBox.size() * 4 : 100000);
                while (it && it->start < mb->start) {
                    prev = it;
                    it = it->next;
                    if (++guard > limit) break;
                }
                mb->next = it;
                mb->last = prev;
                if (it) it->last = mb;
                if (prev) prev->next = mb;
                else free_list_ = mb;
                // free_list 持有引用
                inc_block_ref(mb);
                // 更新最大空闲块
                if (mb->is_free && mb->size > max_free_size_)
                    max_free_size_ = mb->size;
            }
            bool remove_free_list(MemoryBlock* mb) {
                if (!mb || !free_list_) return false;
                // head?
                if (free_list_ == mb) {
                    free_list_ = mb->next;
                    if (free_list_) free_list_->last = nullptr;
                    mb->last = nullptr;
                    mb->next = nullptr;
                    // 从 free_list 摘除，减少引用
                    dec_block_ref(mb);
                    // 可能触发延迟槽清理
                    try_clean_delayed_slots();
                    return true;
                }
                // scan and remove
                MemoryBlock* cur = free_list_;
                size_t guard = 0;
                const size_t limit = 100000;
                while (cur) {
                    if (cur == mb) {
                        if (cur->last) cur->last->next = cur->next;
                        if (cur->next) cur->next->last = cur->last;
                        cur->last = nullptr;
                        cur->next = nullptr;
                        // 从 free_list 摘除，减少引用
                        dec_block_ref(cur);
                        try_clean_delayed_slots();
                        return true;
                    }
                    cur = cur->next;
                    if (++guard > limit) break;
                }
                return false;
            }
            void push_used_list(MemoryBlock* mb) {
                if (!mb) return;
                mb->last = NULL;
                mb->next = used_list_;
                if (used_list_) used_list_->last = mb;
                used_list_ = mb;
                mb->is_free = false;
                // used_list 持有引用
                inc_block_ref(mb);
            }

    public:
        MemoryPool()
            : memory_block_(nullptr)
            , free_list_(nullptr)
            , used_list_(nullptr)
        {
            stats_.total_allocations = 0;
            stats_.total_frees = 0;
            stats_.allocation_failures = 0;
            stats_.current_usage = 0;
            stats_.peak_usage = 0;
            max_free_size_ = 0;
            alloc_counter_ = 0;
            start_map.clear();
            end_map.clear();
            bins.resize(BIN_COUNT);
            for (int i = 0; i < BIN_COUNT; i++)
                bins[i] = nullptr;
        }
        explicit MemoryPool(size_t pool_size)
            : total_size_(pool_size)
            , memory_block_(nullptr)
            , free_list_(nullptr)
            , used_list_(nullptr)
        {
            bins.resize(BIN_COUNT);
            for (int i = 0; i < BIN_COUNT; i++) bins[i] = nullptr;
            start_map.clear();
            end_map.clear();
            initialize(pool_size);
        }
        ~MemoryPool()
        {
            release();
        }
        // 新增：注册回调（普通函数 / lambda / std::function）
        void SetDeallocateCallback(const std::function<void(size_t)>& cb)
        {
            whendeallocate = cb;
        }

        // 新增：注册成员函数回调
        template <typename T>
        void SetDeallocateMemberCallback(T* obj, void (T::* method)(size_t))
        {
            if (!obj || !method) {
                whendeallocate = nullptr;
                return;
            }
            whendeallocate = [obj, method](size_t free_max) {
                (obj->*method)(free_max);
            };
        }
        MemoryBlock* allocate(size_t size) {
            std::lock_guard<std::mutex> __(mutex_);
            // 尝试回收 delayed slots（必须在持主锁下）
#ifdef _MEMDEBUG
// 调试下每次都清，便于发现问题
            try_clean_delayed_slots();
#else
    // Release 下：按频率清理，减少锁内遍历开销
            constexpr uint64_t CLEAN_INTERVAL = 1024;
            uint64_t c = ++alloc_counter_;
            if ((c & (CLEAN_INTERVAL - 1)) == 0) {
                try_clean_delayed_slots();
            }
#endif
            if (size == 0 || !memory_block_)
                ThrowRuntime("Memory not successfully created");

            // 优先从 bins 中查找（find_bin 会尝试同时从 free_list 中摘除）
            MemoryBlock* from_bin = find_bin(size);
            if (from_bin) {
                // from_bin 已经从 bin 中摘除，safe_remove_from_free_list 做二次保险以保证链表一致性
                safe_remove_from_free_list(free_list_, from_bin);

                if (from_bin->size > size) {
                    // 分裂出精确大小的块
                    MemoryBlock* allocated = split_block(from_bin, size);
                    if (!allocated) {
                        ThrowRuntime("Split failed on bin block");
                    }

                    // 将切出的块挂入 used_list_
                    push_used_list(allocated);
                    update_stats_allocation(size);
                    return allocated;
                }
                else {
                    // 完全匹配：直接作为已分配块挂入 used_list_
                    push_used_list(from_bin);
                    from_bin->bin_index = -1;
                    from_bin->bin_next = nullptr;
                    update_stats_allocation(size);
                    return from_bin;
                }
            }

            // 遍历 free_list_ 精确匹配/分裂
            MemoryBlock* current = free_list_;
            size_t iter_guard = 0;
            const size_t iter_limit = (bockBox.size() > 0 ? bockBox.size() * 4 : 100000);
            while (current) {
                // Validate node before use
                if (!IsValidBlockPtr(current)) {
                    Cout() << "MemoryPool::allocate: invalid free_list node detected at " << (void*)current
                        << " (box_index=" << (current ? current->box_index : -1) << "). Aborting allocate walk.\n";
                    print_status();
                    stats_.allocation_failures++;
                    return nullptr;
                }

                if (++iter_guard > iter_limit) {
                    stats_.allocation_failures++;
                    return nullptr;
                }
                if (current->is_free && current->size >= size) {
                    if (current->size > size) {
                        // 从 free_list_ 中安全摘除 current
                        // remove_free_list 会 dec_block_ref 并解绑指针
                        if (!remove_free_list(current)) {
                            // 如果未能从 free_list 中移除（并发已改动），跳过此节点
                            current = current->next;
                            continue;
                        }

                        MemoryBlock* allocated = split_block(current, size);
                        if (allocated) {
                            // 将已分配块挂入 used_list_
                            push_used_list(allocated);
                            update_stats_allocation(size);
                            return allocated;
                        }
                        else {
                            // 失败时将 current 重新放回 free_list_ 头部，避免丢失
                            push_free_list_head(current);
                        }
                    }
                    else {
                        // 完全匹配：从 free_list_ 摘除并挂入 used_list_
                        if (!remove_free_list(current)) {
                            // 并发已改变，跳过节点
                            current = current->next;
                            continue;
                        }
                        push_used_list(current);
                        if (current->bin_index >= 0) {
                            remove_from_bins(current);
                        }
                        current->bin_index = -1;
                        current->bin_next = nullptr;
                        update_stats_allocation(size);
                        return current;
                    }
                }
                current = current->next;
            }

            stats_.allocation_failures++;
            return nullptr;
        }

        void deallocate(MemoryBlock* ptr) {
#ifdef _MEMDEBUG
            // 防御性验证：确保传入 ptr 是有效的元数据且字段合理
            if (!IsValidBlockPtr(this, ptr)) {
                Cout() << "DEALLOC INVALID: ptr=" << (void*)ptr
                    << " box_index=" << (ptr ? ptr->box_index : -1) << "\n";
                // 打印该 box_index 当前槽内容
                if (ptr && ptr->box_index >= 0 && ptr->box_index < (int)bockBox.size()) {
                    MemoryBlock* slotMb = bockBox[ptr->box_index].get();
                    Cout() << "  bockBox[" << ptr->box_index << "] = "
                        << (void*)slotMb << "\n";
                }
                DebugPrintStack();
                assert(false);
            }
            // 检查 start/end/size 是否在 pool 范围内并合理
            if (ptr->start < memory_block_ || ptr->end > memory_block_ + total_size_ || ptr->size == 0 || ptr->size > total_size_) {
                Cout() << "ASSERT: deallocate called with corrupted fields: ptr=" << (void*)ptr
                    << " start=" << (void*)ptr->start << " end=" << (void*)ptr->end << " size=" << ptr->size << "\n";
                DebugPrintStack();
                assert(false);
            }
#endif
            size_t free_max = 0;
            {
                std::lock_guard<std::mutex> __(mutex_);
                if (!ptr || !memory_block_) {
                    // 调用者传了空指针或池未初始化，直接认为是 API 级错误
                    ThrowRuntime("deallocate: null pointer or memory pool not initialized");
                }
                if (ptr->is_free) {
                    return;
                }

                size_t freed_size = ptr->size;

                if (!remove_used_list(ptr)) {
#ifdef _MEMDEBUG
                    // remove_used_list 已在 Debug 下 assert，这里仅作额外保护
                    Cout() << "deallocate: remove_used_list failed for ptr=" << (void*)ptr
                        << " box_index=" << ptr->box_index << "\n";
                    DebugPrintStack();
                    assert(false);
#else
                    ThrowRuntime("deallocate: block not found in used_list");
#endif
                }
                if (ptr->bin_index >= 0)
                    remove_from_bins(ptr);

                // 设为空闲并按地址插入链表（保持按 start 地址有序）
                ptr->is_free = true;
                ptr->last = nullptr;
                ptr->next = nullptr;

                // 统一使用 helper 完成插入与引用平衡（insert_free_list_sorted 会 inc_block_ref）
                insert_free_list_sorted(ptr);

                // 合并：反复合并左右相邻空闲块（在 merge_adjacent 内完成）
                ptr = merge_adjacent(ptr);

                update_stats_deallocation(freed_size);

                // 重新计算最大空闲块（通过 free_list 遍历即可）
               // 使用字段维护的最大空闲块大小，避免每次 O(n) 遍历
                free_max = max_free_size_;
            }
            if (whendeallocate)
                whendeallocate(free_max);
        }

        void initialize(size_t size = 0) {
            if (bins.size() != BIN_COUNT) {
                bins.resize(BIN_COUNT);
                for (int i = 0; i < BIN_COUNT; i++)
                    bins[i] = nullptr;
            }
            start_map.clear();
            end_map.clear();
            initialize_stats();
            size_t buffer_size = size == 0 ? calculateRecommendedBufferSize() : size;
            total_size_ = buffer_size;
            initialize_pool(buffer_size);
        }
        void   release() {
            std::lock_guard<std::mutex> __(mutex_);
            releasing_ = true;
            // 逐个清理 bockBox 中尚未完全释放的元数据，解除容器引用并平衡 ref_count。
            for (int i = 0; i < bockBox.size(); ++i) {
                auto& slot = bockBox[i];
                MemoryBlock* mb = slot.get();
                if (!mb) continue;

                // 在持有 main mutex_ 的前提下，解除所有容器持有
                if (mb->bin_index >= 0) {
                    remove_from_bins(mb);       // 会 dec_bin 引用
                }
                remove_start_map(mb);          // 会 dec_map 引用
                remove_end_map(mb);            // 会 dec_map 引用

                // 从 free/used 链表中摘除（safe_* / remove_* 已会尝试 dec）
                safe_remove_from_free_list(free_list_, mb);
                remove_used_list(mb);

                // 如果仍有残留的引用计数（防御性处理），用 dec 直到为 0
                int v = mb->ref_count.load(std::memory_order_acquire);
                while (v > 0) {
                    dec_block_ref(mb);
                    v = mb->ref_count.load(std::memory_order_acquire);
                }

                // 最后清空槽位
                slot.reset();
            }

            // 清理辅助结构
            start_map.clear();
            end_map.clear();
            free_list_ = used_list_ = NULL;

            if (memory_block_) {
#if defined(PLATFORM_POSIX)
                free(memory_block_);
#else
                _aligned_free(memory_block_);
#endif
                memory_block_ = NULL;
            }

            // 清空 bockBox（现在应该都是空槽）
            if (!bockBox.empty())
                bockBox.clear();

            free_slot_stack.clear();
            delayed_free_slots_.clear();

            if (bins.size() == BIN_COUNT) {
                for (int i = 0; i < BIN_COUNT; i++)
                    bins[i] = nullptr;
            }
            // 重置统计字段
            max_free_size_ = 0;
            alloc_counter_ = 0;
        }
        void   print_status() {
            std::lock_guard<std::mutex> __(mutex_);
            Cout() << "=== Memory Pool Status ===\n";
            Cout() << "Total size: " << total_size_ << "\n";
            Cout() << "Free list: ";
            MemoryBlock* curr = free_list_;
            while (curr) {
                Cout() << "[" << (void*)curr->start << "-" << (void*)curr->end << ":"
                    << curr->size << (curr->is_free ? "F" : "U") << "] ";
                curr = curr->next;
            }
            Cout() << "\n";
        }
        bool   IsAny(size_t size) {
            std::lock_guard<std::mutex> __(mutex_);
            if (size == 0 || !memory_block_)
                ThrowRuntime("Memory not successfully created");

            // 不再放大到2次幂，用真实尺寸检查
            // 先尝试分桶命中 (只检查，不修改结构)
            for (int b = choose_bin(size); b < BIN_COUNT; ++b) {
                MemoryBlock* cb = bins[b];
                while (cb) {
                    if (cb->is_free && cb->size >= size) return true;
                    cb = cb->bin_next;
                }
            }

            // 退回遍历 free_list_
            MemoryBlock* current = free_list_;
            size_t guard = 0;
            const size_t limit = (bockBox.size() > 0 ? bockBox.size() * 4 : 100000);
            while (current) {
                if (current->is_free && current->size >= size)
                    return true;
                current = current->next;
                if (++guard > limit) break;
            }
            return false;
        }
        size_t Get_Free_Maxnum() {
            std::lock_guard<std::mutex> __(mutex_);
            return max_free_size_;
        }
        bool   IsNull() const { return memory_block_ == nullptr; }

        void dump_internal_state() {
            std::lock_guard<std::mutex> __(mutex_);
            Cout() << "=== MemoryPool INTERNAL STATE DUMP ===\n";
            Cout() << "total_size_: " << total_size_ << "\n";
            Cout() << "bockBox count: " << bockBox.size() << "\n";
            for (int i = 0; i < bockBox.size(); ++i) {
                auto& one = bockBox[i];
                MemoryBlock* mb = one.get();
                Cout() << "bockBox[" << i << "] = " << (void*)mb;
                if (mb) {
                    Cout() << " start=" << (void*)mb->start << " size=" << mb->size
                        << " is_free=" << mb->is_free
                        << " box_index=" << mb->box_index
                        << " bin_index=" << mb->bin_index
                        << " ref=" << mb->ref_count.load()
                        << " last=" << (void*)mb->last << " next=" << (void*)mb->next;
                }
                Cout() << "\n";
            }
            Cout() << "free_slot_stack size: " << free_slot_stack.size() << " contents: ";
            for (int i = 0; i < free_slot_stack.size(); ++i) Cout() << free_slot_stack[i] << " ";
            Cout() << "\n";
            Cout() << "delayed_free_slots_ size: " << delayed_free_slots_.size() << " contents: ";
            for (int i = 0; i < delayed_free_slots_.size(); ++i) Cout() << delayed_free_slots_[i] << " ";
            Cout() << "\n";

            Cout() << "Bins summary:\n";
            for (int b = 0; b < BIN_COUNT; ++b) {
                int cnt = 0;
                MemoryBlock* cur = bins[b];
                while (cur && ++cnt < 1000) cur = cur->bin_next;
                Cout() << " bin[" << b << "] head=" << (void*)bins[b] << " count_est=" << cnt << "\n";
            }

            Cout() << "Free list head: " << (void*)free_list_ << "\n";
            Cout() << "Used list head: " << (void*)used_list_ << "\n";
            Cout() << "start_map size: " << start_map.size() << ", end_map size: " << end_map.size() << "\n";
#ifdef _MEMDEBUG
            Cout() << "dbg_inc_count=" << dbg_inc_count.load() << " dbg_dec_count=" << dbg_dec_count.load() << "\n";
#endif
            Cout() << "=== END DUMP ===\n";
        }
#ifdef _MEMDEBUG
        void dump_refcount_stats() {
            std::lock_guard<std::mutex> __(mutex_);
            Cout() << "MemoryPool refcount stats: dbg_inc_count=" << dbg_inc_count.load()
                << " dbg_dec_count=" << dbg_dec_count.load()
                << " bockBox_count=" << bockBox.size()
                << " delayed_slots=" << delayed_free_slots_.size()
                << " free_slots=" << free_slot_stack.size()
                << "\n";
        }
#endif
    };
}; // namespace AssetLib