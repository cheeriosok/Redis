#ifndef HASH_TABLE_HPP
#define HASH_TABLE_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <functional>
#include <type_traits>
#include <vector>
#include <optional>
#include <bit>
#include <cassert>

// To-Do: 
/*
1. Testing each Method - likely some inconsistencies in passing by ref/ptr (particularly in insert) 
2. Multithreading/Concurrency applications & guardrails - MAJOR undertaking probably. Let's see if this shit runs first
*/

// FNV-1a Hashing Algorithm with SIMD.
// *** Some SIMD - Integrate with NEON later if you want. ***

[[nodiscard]] inline std::uint64_t hash_string(const std::uint8_t* data, size_t len) noexcept {
    constexpr uint32_t INITIAL = 0x811C9DC5;
    constexpr uint32_t MULTIPLIER = 0x01000193;

    uint32_t hash = INITIAL;
    size_t i = 0;

    for (; i + 4 <= len; i += 4) { 
        hash = (hash ^ data[i]) * MULTIPLIER;
        hash = (hash ^ data[i + 1]) * MULTIPLIER;
        hash = (hash ^ data[i + 2]) * MULTIPLIER;
        hash = (hash ^ data[i + 3]) * MULTIPLIER;
    }

    for (; i < len; i++) {
        hash = (hash ^ data[i]) * MULTIPLIER;
    }

    return hash;
}

// Basic RAII - Delete copy-consructors, allow transfer of ownership only.
// Our Node contains its HashCode (generated by FNV-1a) and next_ (pointing to a unique_ptr).

template<typename T>
class HNode {
public:
    HNode() noexcept = default; 
    ~HNode() = default; 
    
    HNode(const HNode&) = delete; 
    HNode& operator=(const HNode&) = delete; 
    
    HNode(HNode&&) noexcept = default; 
    HNode& operator=(HNode&&) noexcept = default; 

protected:
    std::unique_ptr<HNode> next_; 
    std::uint64_t hcode_{0}; 
    // Provide access to our 'parent modifiers'
    friend class HTable<T>; 
    friend class HMap<T>; 
};



template<typename T>
class HTable {
public:
    // Constructor to roof initial_size to the nearest exponent of 2 and call initialize. (in private)
    explicit HTable(size_t initial_size = 0) {
        if (initial_size > 0) {
            initialize(std::bit_ceil(initial_size));
        }
    }
    // RAII - Delete Copy Construction & Allow ownership transfer of unique_pointer nodes
    HTable(const HTable&) = delete; 
    HTable& operator=(const HTable&) = delete; 

    HTable(HTable&&) noexcept = default; 
    HTable& operator=(HTable&&) noexcept = default; 

    // Insert function with safety check (initialize on k_min_cap of 4 if empty) and move semantics.
    void insert(std::unique_ptr<HNode<T>> node) {
        if (buckets_.empty()) {
            initialize(k_min_cap);
        }
        /
        size_t pos = node->hcode_ & mask_;
        node->next = std::move(buckets_[pos]);
        buckets_[pos] = std::move(node);
        size_++;
    }
    // Accept anonymous comparator function and key, given the mask_ and position, get index current with raw ptr
    // and iterate until comparator is true to return a raw ptr to our requested data. Else, return nullptr.
    HNode<T>* lookup(
        const HNode<T>* key, 
        const std::function<bool(const HNode<T>*, const HNode<T>*)>& comparator
    ) const {
        if (buckets_.empty()) {
            return nullptr;
        }

        size_t pos = key->hcode_ & mask_;
        HNode<T>* current = buckets_[pos].get();

        while (current) {
            if (comparator(current, key)) { 
                return current;  
            }
            current = current->next_.get();
        }
        return nullptr;
    }

    // Accept anonymous comparator function and key, given the mask_ and position, get index current with raw ptr
    // and iterate until comparator is true, we return the unique_pointer after rechaining (avoid dangling ptrs).
    std::unique_ptr<HNode<T>> remove(
        const HNode<T>* key, 
        const std::function<bool(const HNode<T>&, const HNode<T>&)>& comparator;
    ) {
            if (buckets.empty()) {
                return nullptr;
            }

            size_t pos = key->hashcode & mask;
            const HNode<T>* current = buckets[pos].get();
            const HNode<T>* prev = nullptr;

            while (current) {
                if (comparator(current, key)) { 

                    // if !first node
                    if (prev) {
                        auto node = std::move(prev->next); 
                        prev->next = std::move(node->next);
                        return node;
                    // if first node
                    } else {
                        auto node = std::move(buckets[pos]);
                        buckets[pos] = std::move(node->next);
                        return node;
                    }
                }
                prev = current;
                current = current->next_.get();
            }
        return nullptr;
    }
    
    [[nodiscard]] size_t size() const noexcept { return size_; } 
    [[nodiscard]] size_t capacity() const noexcept { return mask_ + 1; } 
    bool empty() const noexcept { return size_ == 0; } 

private:
    std::vector<std::unique_ptr<HNode<T>>> buckets_; 
    size_t mask_{0}; 
    size_t size_{0}; 

    static constexpr size_t k_min_cap = 4;

    // Helper function for our constructor, 
    // A. Check if capacity is exponent of 2, if true, continue
    // B. Ensure capacity is at least 4, and resize it
    // C. Initilize mask_ as capacity - 1 (bitmasking operation for XOR replacement) and init size_ to 0. 
    void initialize(size_t capacity) { 
        assert(std::has_single_bit(capacity));  
        capacity = std::max(capacity, k_min_cap); 
        
        buckets_.resize(capacity);
        mask_ = capacity - 1; 
        size_ = 0; 
    }
};

template<typename T>
class HMap {
public:
    HMap() = default;
    ~HMap() = default;

    // RAII - Delete Copy Construction & Allow ownership transfer of unique_pointer nodes
    HMap(const HMap&) = delete; 
    HMap& operator=(const HMap&) = delete; 

    HMap(HMap&&) noexcept = default;
    HMap& operator=(HMap&&) noexcept = default; 

    // If our primary HTable is empty, we 
    void insert(std::unique_ptr<HNode<T>> node) { 
        // Initialize our primary_table to k_min_cap if empty. Ensuring capacity is at least 4. 
        if (primary_table_.empty()) { 
            primary_table_ = HTable<T>(k_min_cap);
        }
        primary_table_.insert(std::move(node)); // Then we'll insert our node on our HTable object!
        
        // Here, we want to check if a resizing_table operation is taking place. If true, we skip starting a new resize operation.
        if (!temporary_table_) {
            size_t load_factor = primary_table_.size() / primary_table_.capacity(); // We calculate our load factor here as size/capacity 
            if (load_factor >= k_max_load_factor) { // If our ratio exceeds our maximum tolerable load factor, then we commence a resize op.
                start_resize();
            }
        }
        help_resize(); // On every function call we'll find this method - it essentially offloads 15 items from our primary_table object
        // As to not overwhelm our system when it does eventually get full! We do this to prevent HTable thrashing.
    }

    // Pass in a key and comparator function. We call help_resize before to ensure our node isn't lost after the 15 swaps!
    const HNode<T>* find(
        const HNode<T>* key,
        const std::function<bool(const HNode<T>*, const HNode<T>*)>& comparator)
    {
        help_resize();
        // Call the lookup method to find our key in our primary table.
        if (auto node = primary_table_.lookup(key, comparator)) {
            return node.get();
        }
        // Remember we're also splitting our data into a resizing table of a larger magnitude, we need to search for nodes that were moved also!
        if (temporary_table_) {
            if (auto node = temporary_table_->lookup(key, comparator)) {
                return node.get();
            }
        }
        return nullptr;
    }

    // Once again, similar to find - We pass in a key and comparator function, and we call help_resize. We should call it earlier here as 
    // this improves our performance as keys are migrated to primary_DB, this increases our chances of finding the key in the first check.
    std::unique_ptr<HNode<T>> remove(
        const HNode<T>* key,
        const std::function<bool(const HNode<T>*, const HNode<T>*)>& comparator)
    {
        help_resize();
        
        if (auto node = primary_table_.remove(key, comparator)) {
            return node;
        }
        if (temporary_table_) {
            if (auto node = temporary_table_->remove(key, comparator)) {
                return node;
            }
        }
        return nullptr;
    }

    [[nodiscard]] size_t size() const noexcept {
        return primary_table_.size() + 
               (temporary_table_ ? temporary_table_->size() : 0); // Get the size of both primary and resizing table (if exists).
    }
    
    bool empty() const noexcept { return size() == 0; } // method for checking if the above size()==0, for convenience.

private:
    static constexpr size_t max_work = 15; // 15 transfers per help_resize call 
    static constexpr size_t k_max_load_factor = 8; // Max Average of 8 nodes per bucket before start_resize is called!
    static constexpr size_t k_min_cap = 4; // If constructor is called we want the minimum capacity to be 4 buckets!

    HTable<T> primary_table_;
    std::optional<HTable<T>> temporary_table_;
    size_t resizing_pos_{0};

    void help_resize() {
        // Sanity check! Do not proceed if resizing table does not exist
        if (!temporary_table_) {
            return;
        }
        // we set work_done to be 15 meaning - if work_done > 15, or there are no more nodes to process we exit the loop
        // If w
        size_t work_done = 0;
        while (work_done < max_work && !temporary_table_->empty()) {
            // 'defensive programming' but probably unnecessary as we only increment when moving through empty buckets.
            // Will leave it as it wont cause problems... probably
            if (resizing_pos_ >= temporary_table_->capacity()) {
                resizing_pos_ = 0;
            }

            // current bucket to process is indicating by resizing_pos_
            auto& bucket = temporary_table_->buckets_[resizing_pos_];
            if (bucket) {
                auto node = std::move(bucket); // Let's take the bucket unique_ptr and give it to node. 
                bucket = std::move(node->next_);// Now let's make the bucket's unique_ptr point to the node after. 
                node->next_ = nullptr; // Then let's made node->next_ unique pointer (prev dangling) point to nullptr (probably redundant)
                primary_table_.insert(std::move(node)); // Then we insert this node (see insert in HTable func) with its hashcode 
                temporary_table_->size_--; // reduce size by 1!
                work_done++; // increase work_done by 1!
            } else {
                resizing_pos_++; // if bucket does not exist (i.e empty) then continue to the next bucket. Works when we finish processing bucket to!
            }
        }

        if (temporary_table_->empty()) { // If empty, we reset resizing_table (all buckets) and reinit resizing_pos to 0. Once LOAD FACTOR / CAPACITY > 8 it'll hold primary_table contents again via enplace node transfers.
            temporary_table_.reset();
            resizing_pos_ = 0;
        }
    }
    void start_resize() {
        assert(!temporary_table_); //  first a sanity check that resizing table doesn't already exist!
        size_t new_capacity = primary_table_.capacity() * 2; // new_capacity will be 2x previous, this is fairly standard.
        temporary_table_.emplace(std::move(primary_table_)); // Mark primary_table as an rvalue, and then transfer the contents emplace to resizing_table
        primary_table_ = HTable<T>(new_capacity); // Create a new primary_table with the new doubled capacity 
        resizing_pos_ = 0; // Pointer indicating which bucket we're putting into primary
    }
};

#endif // HASH_TABLE_HPP