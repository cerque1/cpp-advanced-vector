#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <memory>
#include <utility>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    RawMemory(const RawMemory& other) = delete;
    RawMemory& operator=(const RawMemory& other) = delete;
    RawMemory(RawMemory&& other) noexcept {
        Swap(other);
    }
    RawMemory& operator=(RawMemory&& other) noexcept{
        if(this != &other){
            Swap(other);
        }
        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    static void DestroyN(T* buffer, size_t size) noexcept{
        for(size_t i = 0; i < size; i++){
            Destroy(buffer + i);
        }
    }

    static void CopyConstruct(T* buffer, const T& elem){
        new (buffer) T(elem);
    }

    static void CopyOrMoveNElem(T* buffer, size_t count, T* new_buffer){
        if constexpr(std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>){
            std::uninitialized_move_n(buffer, count, new_buffer);
        }
        else{
            std::uninitialized_copy_n(buffer, count, new_buffer);
        }
    }
    
    static void Destroy(T* buffer){
        buffer->~T();
    }

    Vector() = default;

    Vector(size_t size) : data_(size), size_(size){
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }
    Vector(const Vector& other) : data_(other.size_), size_(other.size_){
        std::uninitialized_copy_n(other.data_.GetAddress(), other.size_, data_.GetAddress());
    }
    Vector(Vector&& other) noexcept {
        Swap(other);
    }

    Vector& operator=(const Vector& other){
        if(this != &other){
            if(data_.Capacity() < other.size_){
                Vector copy(other);
                Swap(copy);
            }
            else{
                std::copy(other.data_.GetAddress(), other.data_ + std::min(size_, other.size_), data_.GetAddress());
                if(size_ < other.size_){
                    std::uninitialized_copy_n(other.data_ + size_, other.size_ - size_, data_ + size_);
                }
                else{
                    std::destroy_n(data_.GetAddress() + other.size_, size_ - other.size_);
                }
                size_ = other.size_;
            }
        }
        return *this;
    }
    Vector& operator=(Vector&& other) noexcept {
        if(this != &other){
            Swap(other);
        }
        return *this;
    }

    ~Vector(){
        std::destroy_n(data_.GetAddress(), size_);
    }

    void Resize(size_t new_size){
        if(new_size == size_){
            return;
        }
        if(new_size < size_){
            std::destroy_n(data_ + new_size, size_ - new_size);
        }
        else{
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_ + size_, new_size - size_);
        }
        size_ = new_size;
    }

    template<typename... Args>
    T& EmplaceBack(Args&&... args){
        return *Emplace(cend(), std::forward<Args>(args)...);
    }

    void PushBack(const T& value){ 
        EmplaceBack(value);
    }
    void PushBack(T&& value){ 
        EmplaceBack(std::move(value));
    }

    void PopBack(){
        if(size_ > 0){
            (data_ + size_ - 1)->~T();
            --size_;
        }
    }

    iterator begin() noexcept{
        return data_.GetAddress();
    }
    iterator end() noexcept{
        return data_ + size_;
    }
    const_iterator begin() const noexcept{
        return data_.GetAddress();
    }
    const_iterator end() const noexcept{
        return data_ + size_;
    }
    const_iterator cbegin() const noexcept{
        return data_.GetAddress();
    }
    const_iterator cend() const noexcept{
        return data_ + size_;
    }

    template<typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args){
        assert(begin() <= pos && pos <= end());

        size_t index = static_cast<size_t>(pos - data_.GetAddress());
        if(data_.Capacity() >= size_ + 1){
            if(pos == end()){
                new (data_ + size_) T(std::forward<Args>(args)...);
                size_++;
                return data_ + size_ - 1;
            }
            T* value = new T(std::forward<Args>(args)...);
            CopyOrMoveNElem(data_ + size_ - 1, 1, data_ + size_);
            std::move_backward(data_ + index, data_ + size_ - 1, data_ + size_);
            *(data_ + index) = std::forward<T>(*value);
            delete value;
        }
        else{
            RawMemory<T> new_data_(size_ == 0 ? 1 : size_ * 2);
            new (new_data_ + index) T(std::forward<Args>(args)...);
            try{
                CopyOrMoveNElem(data_.GetAddress(), index, new_data_.GetAddress());
            }catch(...){
                (new_data_ + index)->~T();
                throw;
            }
            try{
                CopyOrMoveNElem(data_ + index, size_ - index, new_data_ + (index + 1));
            }catch(...){
                std::destroy_n(new_data_.GetAddress(), index + 1);
                throw;
            }
            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data_);
        }
        size_++;
        return data_ + index;
    }

    iterator Insert(const_iterator pos, const T& value){
        return Emplace(pos, value);
    }
    iterator Insert(const_iterator pos, T&& value){
        return Emplace(pos, std::move(value));
    }

    iterator Erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<T>){
        assert(begin() <= pos && pos < end());
        size_t index = static_cast<size_t>(pos - data_.GetAddress());
        std::move(data_ + index + 1, data_ + size_, data_ + index);
        (data_ + size_ - 1)->~T();
        --size_;
        return data_ + index;
    }

    void Swap(Vector& other) noexcept{
        std::swap(size_, other.size_);
        data_.Swap(other.data_);
    }

    void Reserve(size_t new_capacity){
        if(data_.Capacity() < new_capacity){
            RawMemory<T> new_data(new_capacity);
            CopyOrMoveNElem(data_.GetAddress(), size_, new_data.GetAddress());
            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
        }
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;
};