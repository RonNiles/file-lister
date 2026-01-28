#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dir_level.h"

/**
 * main - Program entry point
 *
 * Usage: file-comparer [directory_path] [input_file]
 *
 * Recursively reads directory tree and compares all entries with input file.
 */
int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s [directory_path] [input_file]\n", argv[0]);
    return 1;
  }
  // Determine starting directory: argument or current directory
  const char *start_path = argv[1];
  const char *input_file = argv[2];

  DirLevel root, from_file;
  try {
    // Create and initialize directory tree from starting path
    root = DirLevel::CreateFromPath(start_path);
    // Create and initialize directory tree from input file
    from_file = DirLevel::CreateFromTraverseFile(input_file);
  } catch (const std::exception &e) {
    fprintf(stderr, "Error initializing: %s\n", e.what());
    return 1;
  }

  try {
    DirLevel::RemoveCommon(&root, &from_file);
  } catch (const std::exception &e) {
    fprintf(stderr, "Error removing common: %s\n", e.what());
    return 1;
  }
  // Traverse and print the complete directory tree
  std::string basedir1, basedir2;
  printf("From Path: ----------------------------------------\n");
  DirLevel::Traverse(&root, basedir1);
  printf("From File: ----------------------------------------\n");
  DirLevel::Traverse(&from_file, basedir2);

  return 0;
}
