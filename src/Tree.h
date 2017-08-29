#pragma once

#include <vector>

namespace brouwer
{
    template<class T>
    class Tree
    {
        private:
            T value;

            std::vector<Tree<T>> children;

        public:
            Tree(T val) noexcept : value(val) {}

            Tree(const Tree<T>& that) = default;

            void add_child(Tree<T>&& child) noexcept
            {
                this->children.push_back(child);
            }

            const T& val() const noexcept
            {
                return this->value;
            }

            size_t child_count() const noexcept
            {
                return this->children.size();
            }

            const Tree<T>& operator[](size_t i) const noexcept
            {
                return this->children[i];
            }

            const Tree<T>& get_child(size_t i) const noexcept
            {
                return this->children[i];
            }
    };
}
