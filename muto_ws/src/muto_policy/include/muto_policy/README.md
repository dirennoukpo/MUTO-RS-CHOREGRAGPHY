# muto_policy/include/muto_policy

## Description
Placeholder directory for public headers that would belong to the `muto_policy` package. The current implementation exposes only an executable node, so no public headers are required today.

This README documents the directory so future shared interfaces can be added without ambiguity.

## Key Files
- [README.md](README.md): directory documentation.

## Usage / Examples
There is no public header to include at the moment. The package is consumed through the `muto_policy_node` executable built from `src/muto_policy_node.cpp`.

If public APIs are added later, they should be included from this namespace, for example:

```cpp
#include "muto_policy/<new_header>.hpp"
```

## Technical Notes
- The package CMake file currently builds only the `muto_policy_node` executable.
- If reusable policy helpers or shared types are introduced later, place their public declarations here and document their purpose in this file.
