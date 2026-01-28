/*
 * dir_level.h
 *
 * Header file containing data structures for recursive directory listing.
 */

#ifndef DIR_LEVEL_H
#define DIR_LEVEL_H

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include <map>
#include <memory>
#include <string>

/**
 * EntryInfo - Stores metadata for a single filesystem entry
 *
 * Holds information about files and directories including type, size,
 * modification time, and a pointer to the entry's name (stored in the map).
 * For directories, contains a unique_ptr to the nested DirLevel structure.
 */
struct EntryInfo {
  int type;                 // Entry type (DT_REG, DT_DIR, etc.)
  size_t size;              // File size in bytes (0 for directories)
  struct timespec mtime;    // Last modification timestamp
  const std::string *name;  // Pointer to name in parent's map

  // If this entry is for a directory (type == DT_DIR), this points to it
  std::unique_ptr<class DirLevel> dir;
};

/**
 * DirLevel - Represents a directory level in the filesystem hierarchy
 *
 * Maintains a map of entries (files/subdirectories) and links to parent directory.
 * Provides methods to recursively read directory contents and traverse the tree.
 */
class DirLevel {
 public:
  // Default constructor
  DirLevel() : prev_(nullptr), info_(nullptr) {}

  // Constructor: links this directory to its parent and corresponding EntryInfo
  DirLevel(DirLevel *const prev, EntryInfo *const info) : prev_(prev), info_(info) {}

  /**
   * CreateFromPath - Factory function to create and initialize a root DirLevel
   *
   * @param start_path: Path to the directory to read
   * @return: Initialized DirLevel containing the entire directory tree
   *
   * Opens the directory, creates a root DirLevel, and recursively reads all contents.
   * Throws std::runtime_error on any failure.
   */
  static DirLevel CreateFromPath(const char *start_path);

  /**
   * CreateFromTraverseFile - Factory function to create DirLevel from Traverse() output
   *
   * @param filename: Path to file containing output from Traverse()
   * @return: Initialized DirLevel reconstructed from the file
   *
   * Parses a file containing lines in the format:
   *   path type size YYYY-MM-DD HH:MM:SS.nnnnnnnnn
   * and reconstructs the directory tree structure.
   * Throws std::runtime_error on parse errors or file access failures.
   */
  static DirLevel CreateFromTraverseFile(const char *filename);

  /**
   * ReadDir - Recursively read directory contents from an open file descriptor
   *
   * @param fddir: Open file descriptor for the directory (ownership transferred)
   *
   * Reads all entries in the directory, storing metadata for each.
   * Recursively processes subdirectories to build complete filesystem tree.
   */
  void ReadDir(int fddir);

  /**
   * Traverse - Static method to recursively print directory tree
   *
   * @param dir_level: Directory level to traverse
   * @param path: Current path string (modified during traversal)
   *
   * Prints each entry with format: path name type size timestamp
   * Recursively descends into subdirectories.
   */
  static void Traverse(const DirLevel *dir_level, std::string &path);

  /**
   * RemoveCommon - Static method to remove identical non-directory entries and empty
   * directories recursively from two DirLevel objects
   *
   * @param dir1: First DirLevel object pointer. Must not be nullptr.
   * @param dir2: Second DirLevel object pointer. Must not be nullptr.
   *
   * Compares entries in both directory levels and removes non-directory entries (files)
   * that are identical in both objects. Two entries are considered identical if they have
   * the same name, type (and not DT_DIR), size, and modification time.
   * Handles directories recursively and removes directory entries only if they become
   * empty.
   */
  static void RemoveCommon(DirLevel *dir1, DirLevel *dir2);

 private:
  /**
   * FullPath - Recursively build the complete path to this directory
   *
   * @param path: String to append path components to
   *
   * Walks up the directory tree to construct the full path.
   */
  void FullPath(std::string &path) const;

  /**
   * AddEntry - Add a new entry to this directory's map
   *
   * @param entry: Directory entry from readdir
   * @param file_stat: File statistics from fstatat
   * @return: Pointer to the created EntryInfo
   *
   * Creates a new EntryInfo in the map and populates it with metadata.
   */
  EntryInfo *AddEntry(struct dirent *entry, struct stat *file_stat);

  // Member variables
  std::map<std::string, EntryInfo>
      entries_;            // Map of entries (automatically sorted by name)
  const DirLevel *prev_;   // Pointer to parent directory (nullptr for root)
  const EntryInfo *info_;  // Pointer to this dir's entry in parent (nullptr for root)
};

#endif  // DIR_LEVEL_H
