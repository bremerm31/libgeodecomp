#ifndef LIBGEODECOMP_IO_MOCKINITIALIZER_H
#define LIBGEODECOMP_IO_MOCKINITIALIZER_H

#include <libgeodecomp/io/testinitializer.h>

namespace LibGeoDecomp {

// Hardwire this warning to off as MSVC would otherwise complain about
// inline functions not being included in object files:
#ifdef _MSC_BUILD
#pragma warning( push )
#pragma warning( disable : 4514 )
#endif

/**
 * This Initializer will record basic events.
 */
class MockInitializer : public TestInitializer<TestCell<2> >
{
public:
    explicit MockInitializer(const std::string& configString = "")
    {
        events += "created, configString: '" + configString + "'\n";
    }

    ~MockInitializer()
    {
        events += "deleted\n";
    }

    static std::string events;
};

#ifdef _MSC_BUILD
#pragma warning( pop )
#endif

}

#endif
