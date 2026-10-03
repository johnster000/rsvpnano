#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

class IndexedBookStore {
public:
    // Tests may supply words. Every lookup reuses one buffer, like the device store replacing its cache window.
    std::vector<std::string> words;

    bool isOpen() const {
        return !words.empty();
    }

    std::string_view sourcePath() const {
        return {};
    }

    size_t wordCount() const {
        return words.size();
    }

    std::string_view wordAt(size_t index) const {
        if (index >= words.size())
            return {};
        window_.assign(words[index]);
        return window_;
    }

    void prefetchAround(size_t) const {}

private:
    mutable std::string window_;
};
