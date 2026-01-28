#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dir_level.h"

/**
 * main - Program entry point
 *
 * Usage: file-lister [directory_path]
 *
 * If no path is provided, lists current directory "."
 * Recursively reads directory tree and outputs all entries with metadata.
 */
int main(int argc, char *argv[]) {
  // Determine starting directory: argument or current directory
  const char *start_path = (argc > 1) ? argv[1] : ".";

  DirLevel root;
  try {
    // Create and initialize directory tree from starting path
    root = DirLevel::CreateFromPath(start_path);
  } catch (const std::exception &e) {
    fprintf(stderr, "Error: %s\n", e.what());
    return 1;
  }
  // Traverse and print the complete directory tree
  std::string basedir;
  DirLevel::Traverse(&root, basedir);

  return 0;
}
