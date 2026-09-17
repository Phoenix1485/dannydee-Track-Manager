#include "UpdateManager.h"

#include <iostream>

int main()
{
    struct Case {
        const char *candidate;
        const char *installed;
        bool expected;
    };
    const Case cases[] = {
        {"0.6.1", "0.6.0", true},
        {"v1.0.0", "0.9.9", true},
        {"0.6.0", "0.6.0", false},
        {"0.5.9", "0.6.0", false},
        {"broken", "0.6.0", false},
    };

    for (const Case &test : cases) {
        if (UpdateManager::isNewerVersion(QString::fromLatin1(test.candidate),
                                          QString::fromLatin1(test.installed)) != test.expected) {
            std::cerr << "Version comparison failed for " << test.candidate
                      << " and " << test.installed << '\n';
            return 1;
        }
    }
    return 0;
}
