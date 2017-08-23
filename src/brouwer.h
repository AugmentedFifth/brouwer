#pragma once

#include <string>
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
            Tree(T val) noexcept;

            ~Tree() noexcept;

            void add_child(Tree<T>* child) const noexcept;

            T val() const noexcept;

            const Tree<T> const* operator[](size_t i) const noexcept;
    };

    enum class TokenType : unsigned int
    {
        root,  // No lexical representation
        prog,  // No lexical representation
        expr,  // No lexical representation
        chrLit,
        strLit,
        fnDecl,
        parened,
        case_,
        ifElse
    };

    class Token
    {
        public:
            TokenType type;

            std::string lexeme;

            Token(std::string lex) noexcept;

            Token(TokenType t, std::string lex) noexcept;
    };

    std::string vec_to_str(std::vector<char>& char_vec) noexcept
    {
        char_vec.push_back('\0');
        const std::string ret = { char_vec.data() };
        char_vec.clear();
        return ret;
    }
}
