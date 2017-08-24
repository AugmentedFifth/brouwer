#include <iostream>
#include <string>

#include "Tree.h"
#include "Token.h"
#include "Parser.h"

int main(int argc, char* argv[])
{
    using namespace brouwer;
    using AST = Tree<Token>;

    if (argc < 2)
    {
        std::cout << "Please provide the source file." << std::endl;
        return 1;
    }

    std::string filename(argv[1]);

    Parser parser = { filename };
    const AST* ast = parser.parse();

    Parser::log_depthfirst(ast, 0);
    std::cout << std::endl;

    return 0;
}
