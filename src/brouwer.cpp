#include <fstream>
#include <iostream>

#include "brouwer.h"

namespace brouwer
{
    template<class T>
    Tree<T>::Tree(T val) noexcept : value(val) {}

    template<class T>
    Tree<T>::~Tree() noexcept
    {
        for (Tree<T>* child_ptr : this->children)
        {
            delete child_ptr;
        }
    }

    template<class T>
    void Tree<T>::add_child(Tree<T>* child) const noexcept
    {
        children.push_back(child);
    }

    template<class T>
    T Tree<T>::val() const noexcept
    {
        return this->value;
    }

    template<class T>
    const Tree<T> const* Tree<T>::operator[](size_t i) const noexcept
    {
        return this->children[i];
    }

    Token::Token(std::string lex) noexcept : lexeme(lex) {}

    Token::Token(TokenType t, std::string lex) noexcept
        : type(t), lexeme(lex) {}
}

int main(int argc, char* argv[])
{
    using namespace brouwer;

    if (argc < 2)
    {
        std::cout << "Please provide the source file." << std::endl;
        return 1;
    }

    std::ifstream f(argv[1]);

    if (!f.is_open())
    {
        std::cout << "Failed to open " << argv[1] << std::endl;
        return 1;
    }

    //const Tree<Token> ast = { { TokenType::root, "" } };
    std::vector<char> current_lexeme(20);
    char ch;

    while (f >> std::noskipws >> ch)
    {

    }

    return 0;
}
