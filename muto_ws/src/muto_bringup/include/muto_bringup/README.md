# muto_bringup/include/muto_bringup

## Description
This directory is reserved for public headers that are specific to the `muto_bringup` package. In the current repository, the public interfaces used by the build are exposed through `muto_link`; this folder acts as the documentation anchor for future package-specific headers.

## Key Files
- [README.md](README.md): documentation for the directory.

## Usage / Examples
No package-specific public header is currently consumed by the compiled code. To use the low-level stack, include the headers from [muto_bringup/include/muto_link](../muto_link) instead.

## Technical Notes
- The real public interfaces of the package currently live in `muto_link`.
- If package-specific headers are added later, document their role, dependencies, and usage here.
