#include "core/conversation.h"
#include <limits>
#include <stdexcept>
#include <utility>

Conversation::Conversation() {
    data_ = nullptr;
    size_ = 0;
    capacity_ = 0;
}

Conversation::~Conversation() {
    delete[] data_;
}

// A copy needs its own array so the two objects do not share ownership.
Conversation::Conversation(const Conversation& other) {
    if (other.capacity_ == 0) {
        return;
    }

    Message* new_data = new Message[other.capacity_];
    try {
        for (std::size_t i = 0; i < other.size_; i++) {
            new_data[i] = other.data_[i];
        }
    } catch (...) {
        // A string copy could fail. Free the array before passing on the error.
        delete[] new_data;
        throw;
    }

    data_ = new_data;
    size_ = other.size_;
    capacity_ = other.capacity_;
}

Conversation& Conversation::operator=(const Conversation& other) {
    if (this == &other) {
        return *this;
    }

    Message* new_data = nullptr;
    if (other.capacity_ > 0) {
        new_data = new Message[other.capacity_];
        try {
            for (std::size_t i = 0; i < other.size_; i++) {
                new_data[i] = other.data_[i];
            }
        } catch (...) {
            delete[] new_data;
            throw;
        }
    }

    // Keep our original array until the replacement has been copied safely.
    delete[] data_;
    data_ = new_data;
    size_ = other.size_;
    capacity_ = other.capacity_;
    return *this;
}

// Moving transfers the array itself instead of copying the messages.
Conversation::Conversation(Conversation&& other) noexcept {
    data_ = other.data_;
    size_ = other.size_;
    capacity_ = other.capacity_;

    // The old owner must not delete the array that now belongs to us.
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

Conversation& Conversation::operator=(Conversation&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    delete[] data_;
    data_ = other.data_;
    size_ = other.size_;
    capacity_ = other.capacity_;

    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
    return *this;
}

void Conversation::append(Message m) {
    if (size_ == capacity_) {
        std::size_t new_capacity;
        if (capacity_ == 0) {
            new_capacity = 1;
        } else {
            // Check before doubling so the capacity cannot wrap around.
            std::size_t max_slots = std::numeric_limits<std::size_t>::max() / sizeof(Message);
            if (capacity_ > max_slots / 2) {
                throw std::length_error("Conversation too large");
            }
            new_capacity = capacity_ * 2;
        }

        Message* new_data = new Message[new_capacity];
        for (std::size_t i = 0; i < size_; i++) {
            // std::move lets the string transfer its storage rather than copy it.
            // Message contains only an enum and std::string; these moves do not throw.
            new_data[i] = std::move(data_[i]);
        }

        delete[] data_;
        data_ = new_data;
        capacity_ = new_capacity;
    }

    data_[size_] = std::move(m);
    size_++;
}

std::size_t Conversation::size() const noexcept {
    return size_;
}

const Message& Conversation::at(std::size_t i) const {
    if (i >= size_) {
        throw std::out_of_range("Conversation index out of range");
    }
    return data_[i];
}

const Message* Conversation::begin() const noexcept {
    return data_;
}

const Message* Conversation::end() const noexcept {
    if (size_ == 0) {
        return data_;
    }
    return data_ + size_;
}
