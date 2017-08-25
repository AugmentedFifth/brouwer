#include <iostream>
#include <stdexcept>
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
    const AST* ast;

    try
    {
        ast = parser.parse();
    }
    catch (const std::runtime_error& re)
    {
        std::cout << "Uh-oh:\n    " << re.what() << std::endl;

        return 1;
    }
    catch (const std::logic_error& le)
    {
        std::cout << "Internal error:\n    " << le.what() << std::endl;

        return 1;
    }
    catch (const std::exception& e)
    {
        std::cout << "Unknown error:\n    " << e.what() << std::endl;

        return 2;
    }
    catch (...)
    {
        std::cout << "Parser panic! Something is terribly wrong!" << std::endl;

        return 3;
    }

    Parser::log_depthfirst(ast, 0);
    std::cout << std::endl;

    return 0;
}
