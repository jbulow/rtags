#include <rct/String.h>
#include <rct/Path.h>
#include <rct/Log.h>
#include "JSParser.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("[%s] %s:%d: if (argc < 2) {} [after]\n", __func__, __FILE__, __LINE__);
        return 1;
    }
    String c;
    Path file(argv[1]);
    file.resolve();
    if (argc > 2) {
        c = argv[2];
    } else {
        c = file.readAll();
    }
    JSParser parser;
    bool ret = parser.parse(file, c);
    error() << ret << parser.symbols().keys();

    return 0;
}
