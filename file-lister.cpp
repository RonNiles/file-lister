/*
 * file-lister.cpp
 *
 * Recursively lists all files and directories starting from a given path.
 * Outputs: path, type, size, and modification timestamp for each entry in alphabetical
 * order by path. Uses file descriptor-based operations for safety and efficiency.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <map>
#include <memory>
#include <stdexcept>
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
  static DirLevel CreateFromPath(const char *start_path) {
    // Verify directory is readable
    if (access(start_path, R_OK) < 0) {
      throw std::runtime_error("Cannot access " + std::string(start_path) + ": " +
                               strerror(errno));
    }

    // Open the starting directory
    int fddir = open(start_path, O_RDONLY | O_DIRECTORY);
    if (fddir < 0) {
      throw std::runtime_error("Cannot open " + std::string(start_path) + ": " +
                               strerror(errno));
    }

    // Create root directory level and read entire tree
    DirLevel root(nullptr, nullptr);  // Root has no parent or info
    root.ReadDir(fddir);              // Recursively read all subdirectories
    return root;
  }

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
  static DirLevel CreateFromTraverseFile(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
      throw std::runtime_error("Cannot open " + std::string(filename) + ": " +
                               strerror(errno));
    }

    DirLevel root(nullptr, nullptr);
    char line[4096];
    int line_num = 0;

    while (fgets(line, sizeof(line), file)) {
      line_num++;

      // Parse the line: path type size YYYY-MM-DD HH:MM:SS.nnnnnnnnn
      int type;
      unsigned long size;
      int year, month, day, hour, min, sec;
      long nsec;

      // The path may contain spaces so search backwards from EOL for the final 4 fields
      size_t ofs = strlen(line);
      int remain = 4;
      while (ofs-- > 0) {
        if (line[ofs] == ' ' && --remain == 0) {
          break;
        }
      }
      if (ofs == 0) {
        fclose(file);
        throw std::runtime_error("Parse error at line " + std::to_string(line_num) +
                                 ": reading final four fields");
      }

      // Null terminate the filename and scan the final fields
      line[ofs++] = '\0';
      int matched = sscanf(line + ofs, "%d %lu %d-%d-%d %d:%d:%d.%ld", &type, &size,
                           &year, &month, &day, &hour, &min, &sec, &nsec);
      if (matched != 9) {
        fclose(file);
        throw std::runtime_error("Parse error at line " + std::to_string(line_num) +
                                 ": expected 9 fields, got " + std::to_string(matched));
      }

      // Split path into directory components and filename
      std::string fullpath(line);
      size_t last_slash = fullpath.rfind('/');

      std::string dirname =
          (last_slash != std::string::npos) ? fullpath.substr(0, last_slash + 1) : "";
      std::string file_name =
          (last_slash != std::string::npos) ? fullpath.substr(last_slash + 1) : fullpath;

      // Navigate to the appropriate directory level, creating as needed
      DirLevel *current_dir = &root;
      if (!dirname.empty()) {
        size_t start = 0;
        while (start < dirname.length()) {
          size_t end = dirname.find('/', start);
          if (end == std::string::npos) break;

          std::string component = dirname.substr(start, end - start);
          if (!component.empty()) {
            // Find or create this directory
            auto it = current_dir->entries_.find(component);
            if (it == current_dir->entries_.end()) {
              // Create directory entry
              auto inserted = current_dir->entries_.emplace(component, EntryInfo{}).first;
              EntryInfo &info = inserted->second;
              info.type = DT_DIR;
              info.size = 0;
              info.name = &inserted->first;
              info.dir.reset(new DirLevel(current_dir, &info));
              current_dir = info.dir.get();
            } else {
              current_dir = it->second.dir.get();
            }
          }
          start = end + 1;
        }
      }

      // Add the file/directory entry at the current level
      auto inserted = current_dir->entries_.emplace(file_name, EntryInfo{}).first;
      EntryInfo &info = inserted->second;
      info.type = type;
      info.size = size;
      info.name = &inserted->first;

      // Convert timestamp to timespec
      struct tm tm_time = {};
      tm_time.tm_year = year - 1900;
      tm_time.tm_mon = month - 1;
      tm_time.tm_mday = day;
      tm_time.tm_hour = hour;
      tm_time.tm_min = min;
      tm_time.tm_sec = sec;
      info.mtime.tv_sec = timegm(&tm_time);
      info.mtime.tv_nsec = nsec;

      // If it's a directory, create the nested DirLevel
      if (type == DT_DIR) {
        info.dir.reset(new DirLevel(current_dir, &info));
      }
    }

    fclose(file);
    return root;
  }

  /**
   * ReadDir - Recursively read directory contents from an open file descriptor
   *
   * @param fddir: Open file descriptor for the directory (ownership transferred)
   *
   * Reads all entries in the directory, storing metadata for each.
   * Recursively processes subdirectories to build complete filesystem tree.
   */
  void ReadDir(int fddir) {
    // Convert file descriptor to DIR stream (takes ownership of fd)
    DIR *raw_dir = fdopendir(fddir);
    if (raw_dir == NULL) {
      std::string path;
      FullPath(path);
      close(fddir);  // Close fd if fdopendir fails
      throw std::runtime_error("Error opening directory " + path + ": " +
                               strerror(errno));
    }

    // Use RAII to automatically close directory on scope exit
    std::unique_ptr<DIR, int (*)(DIR *)> dir(raw_dir, closedir);

    // Iterate through all directory entries
    struct dirent *entry;
    struct stat file_stat;
    while ((entry = readdir(dir.get())) != NULL) {
      // Skip "." and ".." entries to avoid infinite loops or errors
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        continue;
      }

      // Get file metadata using fstatat (avoids race conditions)
      // AT_SYMLINK_NOFOLLOW: don't follow symbolic links
      if (fstatat(dirfd(dir.get()), entry->d_name, &file_stat, AT_SYMLINK_NOFOLLOW) ==
          -1) {
        std::string path;
        FullPath(path);
        throw std::runtime_error("Can't stat " + path + entry->d_name + ": " +
                                 strerror(errno));
      }

      // Add entry to this directory's map
      EntryInfo *info = AddEntry(entry, &file_stat);

      // If the entry is a directory, recursively process its contents
      if (entry->d_type == DT_DIR) {
        // Open subdirectory using openat (relative to current dir fd)
        int nextfd = openat(dirfd(dir.get()), entry->d_name, O_RDONLY | O_DIRECTORY);
        if (nextfd < 0) {
          std::string path;
          FullPath(path);
          throw std::runtime_error("Can't open directory " + path + entry->d_name + ": " +
                                   strerror(errno));
        }
        // Create new DirLevel for subdirectory and read its contents
        info->dir.reset(new DirLevel(this, info));
        info->dir->ReadDir(nextfd);  // Recursive call
      }
    }
    // Directory automatically closed when unique_ptr goes out of scope
  }

  /**
   * Traverse - Static method to recursively print directory tree
   *
   * @param dir_level: Directory level to traverse
   * @param path: Current path string (modified during traversal)
   *
   * Prints each entry with format: path name type size timestamp
   * Recursively descends into subdirectories.
   */
  static void Traverse(const DirLevel *dir_level, std::string &path) {
    // Iterate through all entries in sorted order (map keeps keys sorted)
    for (const auto &[name, info] : dir_level->entries_) {
      // Convert modification time to human-readable format
      time_t seconds = (time_t)info.mtime.tv_sec;
      struct tm *tt = gmtime(&seconds);
      if (tt == NULL) {
        std::string fullpath;
        dir_level->FullPath(fullpath);
        throw std::runtime_error("gmtime failed for " + fullpath + name);
      }

      // Print: full_path type size timestamp_with_nanoseconds
      printf("%s%s %d %lu %04u-%02u-%02u %02u:%02u:%02u.%09lu\n", path.c_str(),
             name.c_str(), info.type, info.size, 1900 + tt->tm_year, tt->tm_mon + 1,
             tt->tm_mday, tt->tm_hour, tt->tm_min, tt->tm_sec, info.mtime.tv_nsec);

      // If this is a directory, recursively traverse it
      if (info.dir) {
        size_t prevlen = path.length();  // Save current path length
        path += name;                    // Append directory name
        path += '/';
        Traverse(info.dir.get(), path);  // Recursive traversal
        path.resize(prevlen);            // Restore path for next sibling
      }
    }
  }

 private:
  /**
   * FullPath - Recursively build the complete path to this directory
   *
   * @param path: String to append path components to
   *
   * Walks up the directory tree to construct the full path.
   */
  void FullPath(std::string &path) const {
    if (prev_) {
      prev_->FullPath(path);  // Recursively get parent path
      path += *info_->name;
      path += '/';  // Append this directory's name and separator
    }
  }

  /**
   * AddEntry - Add a new entry to this directory's map
   *
   * @param entry: Directory entry from readdir
   * @param file_stat: File statistics from fstatat
   * @return: Pointer to the created EntryInfo
   *
   * Creates a new EntryInfo in the map and populates it with metadata.
   */
  EntryInfo *AddEntry(struct dirent *entry, struct stat *file_stat) {
    // Insert new entry into map, get iterator to inserted element
    auto it = entries_.emplace(entry->d_name, EntryInfo{}).first;
    EntryInfo &info = it->second;

    // Populate entry metadata
    info.type = entry->d_type;  // File type
    info.size =
        entry->d_type == DT_DIR ? 0 : (size_t)file_stat->st_size;  // Size (0 for dirs)
    info.mtime = file_stat->st_mtim;                               // Modification time
    info.name = &it->first;                                        // Point to key in map

    return &info;
  }

  // Member variables
  std::map<std::string, EntryInfo>
      entries_;            // Map of entries (automatically sorted by name)
  const DirLevel *prev_;   // Pointer to parent directory (nullptr for root)
  const EntryInfo *info_;  // Pointer to this dir's entry in parent (nullptr for root)
};

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

  DirLevel root(nullptr, nullptr);
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
