#include "pch.h"
#include "ParserPool.h"

#include "HTMLParserBase.h"

using namespace webclient;

/// Shared parser pool
ParserPool ParserPool::shared(50);

/**
 * @brief Sets up a parser pool with the given number of HTML parsers already preallocated.
*/
ParserPool::ParserPool(size_t numParsers)
{
    // set up critical sections
    InitializeCriticalSection(&this->availableLck);

    // allocate parsers
    this->allocParsers(numParsers);
}

/**
 * @brief Release all allocated parsers.
*/
ParserPool::~ParserPool()
{
    // last reference to all parsers is in here so they'll get DELET
    this->parsers.clear();

    // clean up critical sections
    DeleteCriticalSection(&this->availableLck);
}

/**
 * @brief Allocates the parsers.
*/
void ParserPool::allocParsers(size_t num)
{
    for (size_t i = 0; i < num; i++) {
        auto parser = std::make_shared<HTMLParserBase>();

        this->parsers.push_back(parser);

        EnterCriticalSection(&this->availableLck);
        this->available.push(parser);
        LeaveCriticalSection(&this->availableLck);
    }
}


/**
 * @brief Acquires exclusive access to a parser, then invokes the given block.
 * @return Whether the block was invoked with a parser or not
*/
bool ParserPool::getParser(std::function<void(ParserPtr)> block)
{
    ParserPtr parser = nullptr;

    // check if we can get one out of the available
    EnterCriticalSection(&this->availableLck);

    if (!this->available.empty()) {
        parser = this->available.front();
        this->available.pop();
    }

    LeaveCriticalSection(&this->availableLck);

    // we can, invoke it
    if (!parser)
        goto wait;

    block(parser);

    // put it back in the queue
    EnterCriticalSection(&this->availableLck);
    this->available.push(parser);
    LeaveCriticalSection(&this->availableLck);

    return true;

    // wait for a parser to be available
wait:;
    Sleep(1);

    // check again
    parser = nullptr;
}