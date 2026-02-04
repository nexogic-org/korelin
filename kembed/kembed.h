#ifndef KORELION_H
#define KORELION_H

#include "kapi.h"

// Korelion Single Header Entry Point
// This header, along with the source files in this directory, 
// provides the full Korelin engine API.

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the engine
void korelion_init();

// Execute a script string
void korelion_run(const char* source);

// Execute a script file
void korelion_run_file(const char* path);

// Cleanup
void korelion_cleanup();

#ifdef __cplusplus
}
#endif

#endif // KORELION_H
