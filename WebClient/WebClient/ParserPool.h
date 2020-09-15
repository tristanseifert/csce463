#ifndef PARSERPOOL_H
#define PARSERPOOL_H

#include <cstddef>

#include <memory>
#include <vector>
#include <queue>
#include <functional>

class HTMLParserBase;

namespace webclient{
/**
 * @brief Exposes a pool of HTML parsers, which can be acquired for use under lock.
*/
class ParserPool {
public:
    using ParserPtr = std::shared_ptr<HTMLParserBase>;

public:
    bool getParser(std::function<void(ParserPtr)>);
    
    /// allocates an additional num parsers
    void alloc(size_t num)
    {
        this->allocParsers(num);
    }

private:
    ParserPool() : ParserPool(50) { }
    ParserPool(size_t numParsers);

    ~ParserPool();

    void allocParsers(size_t);

public:

public:
    static ParserPool shared;

private:
    /// all allocated parsers
    std::vector<ParserPtr> parsers;

    /// protect the parsers available for use
    CRITICAL_SECTION availableLck;
    /// parsers currently available for use
    std::queue<ParserPtr> available;

private:
};
}

#endif