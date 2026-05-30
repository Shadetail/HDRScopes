#pragma once
#include "scope/IScope.h"
#include <memory>

// Creates a scope instance for the given type.
std::unique_ptr<IScope> CreateScope(ScopeType type);
const char* ScopeTypeName(ScopeType type);
