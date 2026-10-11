//============================================================================
// BDInfo - Blu-ray Video and Audio Analysis Tool
// Copyright © 2010 Cinema Squid
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//=============================================================================

// File access for BDInfo: local directories (a mounted disc or a copied
// BDMV folder) and UDF disc images. Replaces System.IO.FileInfo /
// DirectoryInfo and DiscUtils' DiscFileInfo / DiscDirectoryInfo.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bdinfo {

class InputStream
{
public:
    virtual ~InputStream() = default;
    virtual int64_t Length() const = 0;
    // Reads up to `count` bytes; returns 0 at end of stream.
    virtual int Read(uint8_t* buffer, int count) = 0;
};

class UdfReader;
struct UdfNode;

// A file on a local file system or inside a UDF image.
struct FileRef
{
    std::string Name;
    std::string FullName;
    int64_t Length = 0;

    // Set for files inside a disc image; null for local files.
    std::shared_ptr<UdfNode> DiscNode;
    UdfReader* Reader = nullptr;

    bool IsDisc() const { return Reader != nullptr; }
    // FileInfo.Extension (".ext") for local files, DiscUtils' Extension
    // ("ext") for files in an image, as the original code expects.
    std::string Extension() const;
    std::unique_ptr<InputStream> OpenRead() const;
};

using FileRefPtr = std::shared_ptr<FileRef>;

// ---------------------------------------------------------------------------
// Local file system (System.IO semantics).
// ---------------------------------------------------------------------------

namespace local {

// Path.GetFullPath: absolute, "." and ".." collapsed, no trailing slash.
std::string FullPath(const std::string& path);
std::string FileName(const std::string& path);         // DirectoryInfo.Name
std::string ParentPath(const std::string& path);       // "" for the root
bool IsDirectory(const std::string& path);
bool Exists(const std::string& path);

struct DirEntry
{
    std::string Name;
    std::string FullName;
    bool IsDirectory = false;
    bool IsSymlink = false;
    int64_t Length = 0;
};

// Entries in readdir order, without "." and "..".
std::vector<DirEntry> List(const std::string& directory);
std::vector<FileRefPtr> GetFiles(const std::string& directory);
std::vector<std::string> GetDirectories(const std::string& directory);
// GetFiles("*", SearchOption.AllDirectories): breadth-first, like .NET.
std::vector<FileRefPtr> GetFilesRecursive(const std::string& directory);

FileRefPtr MakeFile(const DirEntry& entry);

} // namespace local

// ---------------------------------------------------------------------------
// UDF disc images (DiscUtils semantics).
// ---------------------------------------------------------------------------

struct UdfNode
{
    std::string Name;
    bool IsDirectory = false;
    uint64_t Length = 0;
    std::weak_ptr<UdfNode> Parent;

    // Location of the node's (extended) file entry.
    uint16_t IcbPartition = 0;
    uint32_t IcbBlock = 0;

    bool ChildrenLoaded = false;
    std::vector<std::shared_ptr<UdfNode>> Children;
};

using UdfNodePtr = std::shared_ptr<UdfNode>;

class UdfReader
{
public:
    explicit UdfReader(const std::string& imagePath);
    ~UdfReader();
    UdfReader(const UdfReader&) = delete;
    UdfReader& operator=(const UdfReader&) = delete;

    std::string VolumeLabel;

    UdfNodePtr Root() { return root_; }
    const std::vector<UdfNodePtr>& Children(const UdfNodePtr& directory);
    std::unique_ptr<InputStream> Open(const UdfNodePtr& file);

    // DiscDirectoryInfo helpers.
    UdfNodePtr GetDirectory(const std::string& name);          // CdReader.GetDirectoryInfo("BDMV")
    std::vector<FileRefPtr> GetFiles(const UdfNodePtr& directory, const std::string& pattern);
    std::vector<UdfNodePtr> GetDirectories(const UdfNodePtr& directory);
    std::vector<FileRefPtr> GetFilesRecursive(const UdfNodePtr& directory, const std::string& pattern);
    std::string FullName(const UdfNodePtr& node) const;
    FileRefPtr MakeFile(const UdfNodePtr& node);

    struct Extent
    {
        uint16_t Partition;
        uint32_t Block;
        uint64_t Length;
        bool Recorded;
    };

    // Internal, used by the file stream implementation.
    void ReadExtentData(const Extent& extent, uint64_t offset, uint8_t* buffer, size_t count);

private:
    struct Partition
    {
        bool IsMetadata = false;
        uint16_t PhysicalMap = 0;      // map index of the underlying physical partition
        uint32_t Start = 0;            // physical partitions: start sector
        std::vector<Extent> MetadataExtents;
    };

    void ReadSectors(uint64_t sector, uint32_t count, uint8_t* buffer);
    void ReadBlock(uint16_t partition, uint32_t block, uint8_t* buffer);
    void ReadBytes(uint64_t offset, uint8_t* buffer, size_t count);
    std::vector<Extent> ReadFileEntry(uint16_t partition, uint32_t block, uint64_t& length, bool& isDirectory,
                                      std::vector<uint8_t>& embedded);
    std::vector<uint8_t> ReadAll(uint16_t partition, uint32_t block);
    void LoadChildren(const UdfNodePtr& directory);

    int fd_ = -1;
    uint32_t blockSize_ = 2048;
    std::vector<Partition> partitions_;
    UdfNodePtr root_;
};

// DiscUtils wildcard matching ("*.mpls", "FilmIndex.xml"): case-insensitive.
bool WildcardMatch(const std::string& pattern, const std::string& name);

} // namespace bdinfo
