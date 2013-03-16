#ifndef JSParser_h
#define JSParser_h

#include <rct/Path.h>
#include <rct/String.h>
#include <rct/Map.h>
// #include <Location.h>

struct Location
{
    String file;
    int offset;
};
class JSParser
{
public:
    JSParser();
    ~JSParser();

    bool parse(const Path &file, const String &contents = String());
    const Map<String, Location> &symbols() const { return mSymbols; }
private:
    Map<String, Location> mSymbols;
};

#endif
