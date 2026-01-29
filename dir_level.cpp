/*
 * dir_level.cpp
 *
 * Recursively lists all files and directories starting from a given path.
 * Outputs: path, type, size, and modification timestamp for each entry in alphabetical
 * order by path. Uses file descriptor-based operations for safety and efficiency.
 */

#include "dir_level.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <stdexcept>
#include <string_view>
#include <vector>

// Implementation of DirLevel::CreateFromPath
DirLevel DirLevel::CreateFromPath(const char *start_path) {
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

// Implementation of DirLevel::CreateFromTraverseFile
DirLevel DirLevel::CreateFromTraverseFile(const char *filename) {
  FILE *file = fopen(filename, "r");
  if (!file) {
    throw std::runtime_error("Cannot open " + std::string(filename) + ": " +
                             strerror(errno));
  }

  DirLevel root(nullptr, nullptr);
  char *line = nullptr;
  size_t line_capacity = 0;
  int line_num = 0;

  while (getline(&line, &line_capacity, file) != -1) {
    line_num++;

    // Parse the line: path type size YYYY-MM-DD HH:MM:SS.nnnnnnnnn
    int type;
    unsigned long size;
    struct tm tm_time = {};
    long nsec;

    // The path may contain spaces so search backwards from EOL for the final 4 fields
    size_t ofs = strlen(line);
    int remain = 4;
    while (ofs > 0) {
      if (line[--ofs] == ' ' && --remain == 0) {
        break;
      }
    }
    if (remain != 0) {
      free(line);
      fclose(file);
      throw std::runtime_error("Parse error at line " + std::to_string(line_num) +
                               ": reading final four fields");
    }

    // Null terminate the filename and scan the final fields
    line[ofs++] = '\0';
    int matched = sscanf(line + ofs, "%d %lu %d-%d-%d %d:%d:%d.%ld", &type, &size,
                         &tm_time.tm_year, &tm_time.tm_mon, &tm_time.tm_mday,
                         &tm_time.tm_hour, &tm_time.tm_min, &tm_time.tm_sec, &nsec);
    if (matched != 9) {
      free(line);
      fclose(file);
      throw std::runtime_error("Parse error at line " + std::to_string(line_num) +
                               ": expected 9 fields, got " + std::to_string(matched));
    }
    tm_time.tm_year -= 1900;
    tm_time.tm_mon -= 1;

    // Split path into directory components and filename
    std::string_view fullpath(line);
    size_t last_slash = fullpath.rfind('/');

    std::string_view dirname =
        (last_slash != std::string_view::npos) ? fullpath.substr(0, last_slash + 1) : "";
    std::string_view file_name = (last_slash != std::string_view::npos)
                                     ? fullpath.substr(last_slash + 1)
                                     : fullpath;

    // Navigate to the appropriate directory level. No need to create directories here;
    // they must already exist in the tree since directories are listed before their
    // contents.
    DirLevel *current_dir = &root;
    if (!dirname.empty()) {
      size_t start = 0;
      while (start < dirname.length()) {
        size_t end = dirname.find('/', start);
        if (end == std::string_view::npos) break;

        std::string_view component = dirname.substr(start, end - start);
        if (!component.empty()) {
          // Find  this directory
          auto it = current_dir->entries_.find(component);
          if (it == current_dir->entries_.end() || it->second.type != DT_DIR ||
              !it->second.dir) {
            std::string path;
            current_dir->FullPath(path);
            throw std::runtime_error("Directory " + path + std::string(component) +
                                     " not found when processing line " +
                                     std::to_string(line_num));
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
    info.mtime.tv_sec = timegm(&tm_time);
    info.mtime.tv_nsec = nsec;

    // If it's a directory, create the nested DirLevel
    if (type == DT_DIR) {
      info.dir.reset(new DirLevel(current_dir, &info));
    }
  }

  free(line);
  fclose(file);
  return root;
}

// Implementation of DirLevel::ReadDir
void DirLevel::ReadDir(int fddir) {
  // Convert file descriptor to DIR stream (takes ownership of fd)
  DIR *raw_dir = fdopendir(fddir);
  if (raw_dir == NULL) {
    std::string path;
    FullPath(path);
    close(fddir);  // Close fd if fdopendir fails
    throw std::runtime_error("Error opening directory " + path + ": " + strerror(errno));
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
    if (fstatat(dirfd(dir.get()), entry->d_name, &file_stat, AT_SYMLINK_NOFOLLOW) == -1) {
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

// Implementation of DirLevel::Traverse
void DirLevel::Traverse(const DirLevel *dir_level, std::string &path) {
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

// Implementation of DirLevel::FullPath
void DirLevel::FullPath(std::string &path) const {
  if (prev_) {
    prev_->FullPath(path);  // Recursively get parent path
    path += *info_->name;
    path += '/';  // Append this directory's name and separator
  }
}

// Implementation of DirLevel::AddEntry
EntryInfo *DirLevel::AddEntry(struct dirent *entry, struct stat *file_stat) {
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

// Implementation of DirLevel::RemoveCommon
void DirLevel::RemoveCommon(DirLevel *dir1, DirLevel *dir2) {
  // Iterate through entries in dir1
  for (auto it = dir1->entries_.begin(), next = it; it != dir1->entries_.end();
       it = next) {
    ++next;
    auto &[name, info1] = *it;

    // Check if this entry exists in dir2
    auto it2 = dir2->entries_.find(name);
    if (it2 != dir2->entries_.end()) {
      const EntryInfo &info2 = it2->second;
      // If both are directories (with same name) then recurse always
      if (info1.type == DT_DIR && info2.type == DT_DIR) {
        if (!info1.dir.get() || !info2.dir.get()) {
          std::string fullpath;
          dir1->FullPath(fullpath);
          throw std::runtime_error("missing directory pointer for " + fullpath + name);
        }
        RemoveCommon(info1.dir.get(), info2.dir.get());
        // Remove directory entries only if they are now empty
        if (info1.dir.get()->entries_.empty()) {
          dir1->entries_.erase(it);
        }
        if (info2.dir.get()->entries_.empty()) {
          dir2->entries_.erase(it2);
        }
        continue;
      }

      // Check if the entries are identical (same type, size, and modification time)
      if (info2.type == info1.type && info2.size == info1.size &&
          info2.mtime.tv_sec == info1.mtime.tv_sec &&
          info2.mtime.tv_nsec == info1.mtime.tv_nsec) {
        dir1->entries_.erase(it);
        dir2->entries_.erase(it2);
      }
    }
  }
}
