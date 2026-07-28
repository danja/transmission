// native/tests/PluginScannerTest.cpp

#include "transmission/PluginScanner.h"

#include <cassert>
#include <filesystem>

int main() {
    transmission::PluginScanner scanner;
    const auto candidates = scanner.scan({"/definitely/not/a/plugin"});
    assert(candidates.empty());
    return 0;
}
