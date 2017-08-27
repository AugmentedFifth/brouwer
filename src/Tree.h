#pragma once

#include <vector>

namespace brouwer
{
    template<class T>
    class Tree
    {
        private:
            T value;

            std::vector<Tree<T>*> children;

        public:
            Tree(T val) noexcept : value(val) {}

            ~Tree() noexcept
            {
                for (Tree<T>* child_ptr : this->children)
                {
                    delete child_ptr;
                }
            }

            void add_child(Tree<T>* child) noexcept
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

            const Tree<T>* const operator[](size_t i) const noexcept
            {
                return this->children[i];
            }

            const Tree<T>* const get_child(size_t i) const noexcept
            {
                return this->children[i];
            }
    };
}
