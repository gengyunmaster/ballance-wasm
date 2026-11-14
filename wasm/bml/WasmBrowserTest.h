#ifndef BML_WASM_BROWSER_TEST_H
#define BML_WASM_BROWSER_TEST_H

#include <memory>
#include <vector>

class ICommand;
class ModContext;

namespace WasmBrowserTesting {
std::vector<std::shared_ptr<ICommand>> CreateCommands(ModContext &context);
}

#endif
