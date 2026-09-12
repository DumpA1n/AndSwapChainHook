#pragma once
#include <cstddef>
int DobbyBeginPatch(void *address, std::size_t size);
int DobbyEndPatch(void *address);
bool DobbyReadable(void *address, std::size_t size);
