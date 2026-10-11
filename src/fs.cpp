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

#include "fs.h"
#include "net.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <deque>
#include <map>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace bdinfo {

// ---------------------------------------------------------------------------
// Local files
// ---------------------------------------------------------------------------

namespace {

class LocalFileStream : public InputStream
{
public:
    explicit LocalFileStream(const std::string& path)
    {
        fd_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd_ < 0)
        {
            int err = errno;
            if (err == ENOENT) net::ThrowIO("Could not find file '" + path + "'.");
            if (err == EACCES) net::ThrowIO("Access to the path '" + path + "' is denied.");
            net::ThrowIO(path + ": " + strerror(err));
        }
        struct stat st {};
        if (fstat(fd_, &st) == 0) length_ = st.st_size;
#ifdef POSIX_FADV_SEQUENTIAL
        posix_fadvise(fd_, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
    }
    ~LocalFileStream() override
    {
        if (fd_ >= 0) ::close(fd_);
    }

    int64_t Length() const override { return length_; }

    int Read(uint8_t* buffer, int count) override
    {
        // One read(2) per call, like .NET's FileStream: the TS parser's
        // behaviour depends on how the file is split into chunks.
        while (true)
        {
            ssize_t n = ::read(fd_, buffer, static_cast<size_t>(count));
            if (n >= 0) return static_cast<int>(n);
            if (errno == EINTR) continue;
            net::ThrowIO(std::string("Read error: ") + strerror(errno));
        }
    }

private:
    int fd_ = -1;
    int64_t length_ = 0;
};

} // namespace

std::string FileRef::Extension() const
{
    if (IsDisc())
    {
        size_t dot = Name.rfind('.');
        return dot == std::string::npos ? "" : Name.substr(dot + 1);
    }
    size_t dot = Name.rfind('.');
    if (dot == std::string::npos || dot == Name.size() - 1) return "";
    return Name.substr(dot);
}

std::unique_ptr<InputStream> FileRef::OpenRead() const
{
    if (IsDisc()) return Reader->Open(DiscNode);
    return std::make_unique<LocalFileStream>(FullName);
}

namespace local {

std::string FullPath(const std::string& path)
{
    std::string absolute = path;
    if (absolute.empty() || absolute[0] != '/')
    {
        char cwd[4096];
        if (getcwd(cwd, sizeof cwd) == nullptr) net::ThrowIO("Unable to determine the current directory.");
        absolute = std::string(cwd) + "/" + absolute;
    }
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= absolute.size())
    {
        size_t end = absolute.find('/', start);
        if (end == std::string::npos) end = absolute.size();
        std::string part = absolute.substr(start, end - start);
        if (part == "..")
        {
            if (!parts.empty()) parts.pop_back();
        }
        else if (!part.empty() && part != ".")
        {
            parts.push_back(part);
        }
        start = end + 1;
    }
    std::string result;
    for (const std::string& part : parts) result += "/" + part;
    return result.empty() ? "/" : result;
}

std::string FileName(const std::string& path)
{
    if (path == "/") return "/";
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string ParentPath(const std::string& path)
{
    if (path == "/" || path.empty()) return "";
    size_t slash = path.rfind('/');
    if (slash == std::string::npos) return "";
    return slash == 0 ? "/" : path.substr(0, slash);
}

bool IsDirectory(const std::string& path)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool Exists(const std::string& path)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

std::vector<DirEntry> List(const std::string& directory)
{
    std::vector<DirEntry> entries;
    DIR* dir = opendir(directory.c_str());
    if (dir == nullptr)
    {
        if (errno == ENOENT) net::ThrowIO("Could not find a part of the path '" + directory + "'.");
        if (errno == EACCES) net::ThrowIO("Access to the path '" + directory + "' is denied.");
        net::ThrowIO(directory + ": " + strerror(errno));
    }
    while (struct dirent* d = readdir(dir))
    {
        if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0) continue;
        DirEntry entry;
        entry.Name = d->d_name;
        entry.FullName = directory == "/" ? "/" + entry.Name : directory + "/" + entry.Name;
        struct stat st {};
        struct stat lst {};
        bool haveLink = lstat(entry.FullName.c_str(), &lst) == 0;
        entry.IsSymlink = haveLink && S_ISLNK(lst.st_mode);
        if (stat(entry.FullName.c_str(), &st) == 0)
        {
            entry.IsDirectory = S_ISDIR(st.st_mode);
            entry.Length = st.st_size;
        }
        else if (haveLink)
        {
            entry.Length = lst.st_size;
        }
        entries.push_back(std::move(entry));
    }
    closedir(dir);
    return entries;
}

FileRefPtr MakeFile(const DirEntry& entry)
{
    auto file = std::make_shared<FileRef>();
    file->Name = entry.Name;
    file->FullName = entry.FullName;
    file->Length = entry.Length;
    return file;
}

std::vector<FileRefPtr> GetFiles(const std::string& directory)
{
    std::vector<FileRefPtr> files;
    for (const DirEntry& entry : List(directory))
        if (!entry.IsDirectory) files.push_back(MakeFile(entry));
    return files;
}

std::vector<std::string> GetDirectories(const std::string& directory)
{
    std::vector<std::string> directories;
    for (const DirEntry& entry : List(directory))
        if (entry.IsDirectory) directories.push_back(entry.FullName);
    return directories;
}

std::vector<FileRefPtr> GetFilesRecursive(const std::string& directory)
{
    std::vector<FileRefPtr> files;
    std::deque<std::string> pending{directory};
    while (!pending.empty())
    {
        std::string current = pending.front();
        pending.pop_front();
        for (const DirEntry& entry : List(current))
        {
            if (entry.IsDirectory)
            {
                if (!entry.IsSymlink) pending.push_back(entry.FullName);
            }
            else
            {
                files.push_back(MakeFile(entry));
            }
        }
    }
    return files;
}

} // namespace local

// ---------------------------------------------------------------------------
// UDF images
// ---------------------------------------------------------------------------

namespace {

uint16_t U16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t U32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t U64(const uint8_t* p) { return static_cast<uint64_t>(U32(p)) | (static_cast<uint64_t>(U32(p + 4)) << 32); }

bool ValidTag(const uint8_t* p, uint16_t expectedId)
{
    if (U16(p) != expectedId) return false;
    uint8_t sum = 0;
    for (int i = 0; i < 16; i++)
        if (i != 4) sum = static_cast<uint8_t>(sum + p[i]);
    return sum == p[4];
}

void AppendUtf8(std::string& out, uint32_t cp)
{
    if (cp < 0x80)
    {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp < 0x800)
    {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp < 0x10000)
    {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// OSTA Compressed Unicode (d-characters).
std::string DecodeDChars(const uint8_t* p, size_t length)
{
    std::string out;
    if (length == 0) return out;
    uint8_t compression = p[0];
    if (compression == 8 || compression == 254)
    {
        for (size_t i = 1; i < length; i++) AppendUtf8(out, p[i]);
    }
    else if (compression == 16 || compression == 255)
    {
        for (size_t i = 1; i + 1 < length; i += 2)
        {
            uint32_t unit = static_cast<uint32_t>((p[i] << 8) | p[i + 1]);
            if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < length)
            {
                uint32_t low = static_cast<uint32_t>((p[i + 2] << 8) | p[i + 3]);
                if (low >= 0xDC00 && low <= 0xDFFF)
                {
                    AppendUtf8(out, 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00));
                    i += 2;
                    continue;
                }
            }
            AppendUtf8(out, unit);
        }
    }
    return out;
}

// Fixed-size dstring field: the last byte holds the used length.
std::string DecodeDString(const uint8_t* p, size_t fieldLength)
{
    size_t used = p[fieldLength - 1];
    if (used == 0 || used >= fieldLength) return "";
    return DecodeDChars(p, used);
}

class UdfFileStream : public InputStream
{
public:
    UdfFileStream(UdfReader* reader, std::vector<UdfReader::Extent> extents, std::vector<uint8_t> embedded,
                  bool isEmbedded, uint64_t length)
        : reader_(reader), extents_(std::move(extents)), embedded_(std::move(embedded)), isEmbedded_(isEmbedded),
          length_(length)
    {
    }

    int64_t Length() const override { return static_cast<int64_t>(length_); }

    int Read(uint8_t* buffer, int count) override
    {
        if (count <= 0 || position_ >= length_) return 0;
        uint64_t toRead = std::min<uint64_t>(static_cast<uint64_t>(count), length_ - position_);
        if (isEmbedded_)
        {
            for (uint64_t i = 0; i < toRead; i++)
            {
                uint64_t index = position_ + i;
                buffer[i] = index < embedded_.size() ? embedded_[index] : 0;
            }
            position_ += toRead;
            return static_cast<int>(toRead);
        }

        uint64_t done = 0;
        while (done < toRead)
        {
            uint64_t offset = position_ + done;
            // Locate the extent containing `offset`.
            uint64_t extentStart = 0;
            const UdfReader::Extent* extent = nullptr;
            for (const auto& e : extents_)
            {
                if (offset < extentStart + e.Length)
                {
                    extent = &e;
                    break;
                }
                extentStart += e.Length;
            }
            uint64_t chunk;
            if (extent == nullptr)
            {
                chunk = toRead - done;
                memset(buffer + done, 0, chunk);
            }
            else
            {
                chunk = std::min(toRead - done, extentStart + extent->Length - offset);
                reader_->ReadExtentData(*extent, offset - extentStart, buffer + done, chunk);
            }
            done += chunk;
        }
        position_ += toRead;
        return static_cast<int>(toRead);
    }

private:
    UdfReader* reader_;
    std::vector<UdfReader::Extent> extents_;
    std::vector<uint8_t> embedded_;
    bool isEmbedded_;
    uint64_t length_;
    uint64_t position_ = 0;
};

} // namespace

UdfReader::UdfReader(const std::string& imagePath)
{
    fd_ = ::open(imagePath.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd_ < 0)
    {
        if (errno == ENOENT) net::ThrowIO("Could not find file '" + imagePath + "'.");
        net::ThrowIO(imagePath + ": " + strerror(errno));
    }

    struct stat st {};
    fstat(fd_, &st);
    uint64_t sectors = static_cast<uint64_t>(st.st_size) / 2048;

    // Anchor volume descriptor pointer.
    std::vector<uint8_t> sector(2048);
    bool found = false;
    for (uint64_t candidate : {uint64_t(256), sectors - 1, sectors - 257})
    {
        if (candidate >= sectors) continue;
        ReadSectors(candidate, 1, sector.data());
        if (ValidTag(sector.data(), 2))
        {
            found = true;
            break;
        }
    }
    if (!found) net::ThrowIO("Stream is not a recognized UDF format");

    uint32_t vdsLength = U32(&sector[16]);
    uint32_t vdsLocation = U32(&sector[20]);

    std::map<uint16_t, uint32_t> partitionStarts;
    std::vector<uint8_t> lvd;
    for (int guard = 0; guard < 64 && vdsLength >= 2048; guard++)
    {
        bool next = false;
        for (uint32_t i = 0; i < vdsLength / 2048; i++)
        {
            ReadSectors(static_cast<uint64_t>(vdsLocation) + i, 1, sector.data());
            uint16_t id = U16(sector.data());
            if (id == 5)
            {
                partitionStarts[U16(&sector[22])] = U32(&sector[188]);
            }
            else if (id == 6)
            {
                lvd = sector;
            }
            else if (id == 3)
            {
                vdsLength = U32(&sector[20]);
                vdsLocation = U32(&sector[24]);
                next = true;
                break;
            }
            else if (id == 8 || id == 0)
            {
                break;
            }
        }
        if (!next) break;
    }
    if (lvd.empty() || partitionStarts.empty()) net::ThrowIO("Invalid UDF image: missing volume descriptors.");

    blockSize_ = U32(&lvd[212]);
    if (blockSize_ != 2048) net::ThrowIO("Unsupported UDF block size.");
    VolumeLabel = DecodeDString(&lvd[84], 128);

    uint32_t mapCount = U32(&lvd[268]);
    uint32_t mapTableLength = U32(&lvd[264]);
    size_t mapOffset = 440;
    std::vector<std::pair<uint16_t, uint32_t>> metadataMaps; // map index -> metadata file location
    for (uint32_t m = 0; m < mapCount && mapOffset + 2 <= 440 + mapTableLength && mapOffset + 2 <= lvd.size(); m++)
    {
        const uint8_t* map = &lvd[mapOffset];
        uint8_t type = map[0];
        uint8_t length = map[1];
        Partition partition;
        if (type == 1)
        {
            partition.Start = partitionStarts[U16(map + 4)];
        }
        else if (type == 2)
        {
            std::string ident(reinterpret_cast<const char*>(map + 5), 23);
            ident = ident.c_str();
            uint16_t number = U16(map + 38);
            if (net::StartsWith(ident, "*UDF Metadata Partition"))
            {
                partition.IsMetadata = true;
                partition.Start = partitionStarts[number];
                metadataMaps.emplace_back(static_cast<uint16_t>(partitions_.size()), U32(map + 40));
            }
            else if (net::StartsWith(ident, "*UDF Sparable Partition"))
            {
                partition.Start = partitionStarts[number];
            }
            else
            {
                net::ThrowIO("Unsupported UDF partition type: " + ident);
            }
        }
        partitions_.push_back(partition);
        mapOffset += length > 0 ? length : 6;
    }

    // Metadata partitions: block addresses are offsets into the metadata file,
    // which lives in the physical partition with the same partition number.
    for (auto& [index, location] : metadataMaps)
    {
        uint16_t physical = 0;
        for (uint16_t p = 0; p < partitions_.size(); p++)
        {
            if (!partitions_[p].IsMetadata && partitions_[p].Start == partitions_[index].Start)
            {
                physical = p;
                break;
            }
        }
        partitions_[index].PhysicalMap = physical;
        uint64_t length = 0;
        bool isDirectory = false;
        std::vector<uint8_t> embedded;
        partitions_[index].MetadataExtents = ReadFileEntry(physical, location, length, isDirectory, embedded);
    }

    // File set descriptor -> root directory.
    uint32_t fsdBlock = U32(&lvd[252]);
    uint16_t fsdPartition = U16(&lvd[256]);
    std::vector<uint8_t> block(blockSize_);
    ReadBlock(fsdPartition, fsdBlock, block.data());
    if (U16(block.data()) != 256) net::ThrowIO("Invalid UDF image: missing file set descriptor.");

    root_ = std::make_shared<UdfNode>();
    root_->IsDirectory = true;
    root_->IcbBlock = U32(&block[404]);
    root_->IcbPartition = U16(&block[408]);
}

UdfReader::~UdfReader()
{
    if (fd_ >= 0) ::close(fd_);
}

void UdfReader::ReadBytes(uint64_t offset, uint8_t* buffer, size_t count)
{
    size_t done = 0;
    while (done < count)
    {
        ssize_t n = ::pread(fd_, buffer + done, count - done, static_cast<off_t>(offset + done));
        if (n < 0)
        {
            if (errno == EINTR) continue;
            net::ThrowIO(std::string("Read error: ") + strerror(errno));
        }
        if (n == 0)
        {
            memset(buffer + done, 0, count - done);
            break;
        }
        done += static_cast<size_t>(n);
    }
}

void UdfReader::ReadSectors(uint64_t sector, uint32_t count, uint8_t* buffer)
{
    ReadBytes(sector * 2048, buffer, static_cast<size_t>(count) * 2048);
}

void UdfReader::ReadBlock(uint16_t partition, uint32_t block, uint8_t* buffer)
{
    if (partition >= partitions_.size()) net::ThrowIO("Invalid UDF partition reference.");
    const Partition& p = partitions_[partition];
    if (!p.IsMetadata)
    {
        ReadBytes((static_cast<uint64_t>(p.Start) + block) * blockSize_, buffer, blockSize_);
        return;
    }
    uint64_t offset = static_cast<uint64_t>(block) * blockSize_;
    uint64_t extentStart = 0;
    for (const Extent& extent : p.MetadataExtents)
    {
        if (offset < extentStart + extent.Length)
        {
            ReadExtentData(extent, offset - extentStart, buffer, blockSize_);
            return;
        }
        extentStart += extent.Length;
    }
    memset(buffer, 0, blockSize_);
}

void UdfReader::ReadExtentData(const Extent& extent, uint64_t offset, uint8_t* buffer, size_t count)
{
    if (!extent.Recorded)
    {
        memset(buffer, 0, count);
        return;
    }
    if (extent.Partition >= partitions_.size()) net::ThrowIO("Invalid UDF partition reference.");
    const Partition& p = partitions_[extent.Partition];
    if (!p.IsMetadata)
    {
        ReadBytes((static_cast<uint64_t>(p.Start) + extent.Block) * blockSize_ + offset, buffer, count);
        return;
    }
    std::vector<uint8_t> block(blockSize_);
    size_t done = 0;
    while (done < count)
    {
        uint64_t position = offset + done;
        uint32_t blockIndex = static_cast<uint32_t>(position / blockSize_);
        size_t within = static_cast<size_t>(position % blockSize_);
        ReadBlock(extent.Partition, extent.Block + blockIndex, block.data());
        size_t chunk = std::min(count - done, static_cast<size_t>(blockSize_) - within);
        memcpy(buffer + done, block.data() + within, chunk);
        done += chunk;
    }
}

std::vector<UdfReader::Extent> UdfReader::ReadFileEntry(uint16_t partition, uint32_t block, uint64_t& length,
                                                        bool& isDirectory, std::vector<uint8_t>& embedded)
{
    std::vector<uint8_t> entry(blockSize_);
    ReadBlock(partition, block, entry.data());
    uint16_t tag = U16(entry.data());
    if (tag != 261 && tag != 266) net::ThrowIO("Invalid UDF file entry.");

    isDirectory = entry[16 + 11] == 4;
    int adType = U16(&entry[16 + 18]) & 7;
    length = U64(&entry[56]);

    uint32_t eaLength, adLength, adStart;
    if (tag == 261)
    {
        eaLength = U32(&entry[168]);
        adLength = U32(&entry[172]);
        adStart = 176 + eaLength;
    }
    else
    {
        eaLength = U32(&entry[208]);
        adLength = U32(&entry[212]);
        adStart = 216 + eaLength;
    }
    if (adStart > blockSize_ || adLength > blockSize_ - adStart) net::ThrowIO("Invalid UDF file entry.");

    std::vector<Extent> extents;
    if (adType == 3)
    {
        embedded.assign(entry.begin() + adStart, entry.begin() + adStart + adLength);
        return extents;
    }

    std::vector<uint8_t> area(entry.begin() + adStart, entry.begin() + adStart + adLength);
    for (int chain = 0; chain < 4096; chain++)
    {
        size_t size = adType == 0 ? 8 : adType == 1 ? 16 : 20;
        bool continued = false;
        for (size_t pos = 0; pos + size <= area.size(); pos += size)
        {
            const uint8_t* ad = &area[pos];
            uint32_t raw = U32(ad);
            uint32_t extentLength = raw & 0x3FFFFFFF;
            uint32_t extentType = raw >> 30;
            if (extentLength == 0) break;

            Extent extent{};
            if (adType == 0)
            {
                extent.Partition = partition;
                extent.Block = U32(ad + 4);
            }
            else if (adType == 1)
            {
                extent.Block = U32(ad + 4);
                extent.Partition = U16(ad + 8);
            }
            else
            {
                extent.Block = U32(ad + 12);
                extent.Partition = U16(ad + 16);
            }
            extent.Length = extentLength;
            extent.Recorded = extentType == 0;

            if (extentType == 3)
            {
                // Continuation: an allocation extent descriptor holds more ADs.
                std::vector<uint8_t> aed(blockSize_);
                ReadBlock(extent.Partition, extent.Block, aed.data());
                if (U16(aed.data()) != 258) net::ThrowIO("Invalid UDF allocation extent.");
                uint32_t nextLength = std::min<uint32_t>(U32(&aed[20]), blockSize_ - 24);
                area.assign(aed.begin() + 24, aed.begin() + 24 + nextLength);
                continued = true;
                break;
            }
            extents.push_back(extent);
        }
        if (!continued) break;
    }
    return extents;
}

std::vector<uint8_t> UdfReader::ReadAll(uint16_t partition, uint32_t block)
{
    uint64_t length = 0;
    bool isDirectory = false;
    std::vector<uint8_t> embedded;
    std::vector<Extent> extents = ReadFileEntry(partition, block, length, isDirectory, embedded);
    std::vector<uint8_t> data(static_cast<size_t>(length));
    UdfFileStream stream(this, extents, embedded, extents.empty() && !embedded.empty(), length);
    size_t done = 0;
    while (done < data.size())
    {
        int n = stream.Read(data.data() + done, static_cast<int>(std::min<size_t>(data.size() - done, 1 << 30)));
        if (n <= 0) break;
        done += static_cast<size_t>(n);
    }
    return data;
}

void UdfReader::LoadChildren(const UdfNodePtr& directory)
{
    directory->ChildrenLoaded = true;
    std::vector<uint8_t> data = ReadAll(directory->IcbPartition, directory->IcbBlock);
    size_t pos = 0;
    while (pos + 38 <= data.size())
    {
        const uint8_t* fid = &data[pos];
        if (U16(fid) != 257) break;
        uint8_t characteristics = fid[18];
        uint8_t nameLength = fid[19];
        uint32_t icbBlock = U32(fid + 24);
        uint16_t icbPartition = U16(fid + 28);
        uint16_t implementationUseLength = U16(fid + 36);
        size_t total = 38 + implementationUseLength + nameLength;
        if (pos + total > data.size()) break;
        const uint8_t* name = fid + 38 + implementationUseLength;
        pos += (total + 3) & ~static_cast<size_t>(3);

        if (characteristics & 0x08) continue; // parent
        if (characteristics & 0x04) continue; // deleted

        auto node = std::make_shared<UdfNode>();
        node->Name = DecodeDChars(name, nameLength);
        node->IsDirectory = (characteristics & 0x02) != 0;
        node->Parent = directory;
        node->IcbBlock = icbBlock;
        node->IcbPartition = icbPartition;
        uint64_t length = 0;
        bool isDirectory = false;
        std::vector<uint8_t> embedded;
        ReadFileEntry(icbPartition, icbBlock, length, isDirectory, embedded);
        node->Length = length;
        directory->Children.push_back(node);
    }
}

const std::vector<UdfNodePtr>& UdfReader::Children(const UdfNodePtr& directory)
{
    if (!directory->ChildrenLoaded) LoadChildren(directory);
    return directory->Children;
}

std::unique_ptr<InputStream> UdfReader::Open(const UdfNodePtr& file)
{
    uint64_t length = 0;
    bool isDirectory = false;
    std::vector<uint8_t> embedded;
    std::vector<Extent> extents = ReadFileEntry(file->IcbPartition, file->IcbBlock, length, isDirectory, embedded);
    bool isEmbedded = extents.empty() && !embedded.empty();
    return std::make_unique<UdfFileStream>(this, std::move(extents), std::move(embedded), isEmbedded, length);
}

UdfNodePtr UdfReader::GetDirectory(const std::string& name)
{
    for (const UdfNodePtr& child : Children(root_))
        if (child->IsDirectory && net::ToUpper(child->Name) == net::ToUpper(name)) return child;
    return nullptr;
}

bool WildcardMatch(const std::string& pattern, const std::string& name)
{
    // DiscUtils: a pattern without '.' gets one appended, and so does a name.
    std::string p = net::ToUpper(pattern.find('.') == std::string::npos ? pattern + "." : pattern);
    std::string n = net::ToUpper(name.find('.') == std::string::npos ? name + "." : name);

    // Glob match where '*' is any sequence and '?' any character but '.'.
    size_t pi = 0, ni = 0, starP = std::string::npos, starN = 0;
    while (ni < n.size())
    {
        if (pi < p.size() && p[pi] == '*')
        {
            starP = pi++;
            starN = ni;
        }
        else if (pi < p.size() && (p[pi] == n[ni] || (p[pi] == '?' && n[ni] != '.')))
        {
            pi++;
            ni++;
        }
        else if (starP != std::string::npos)
        {
            pi = starP + 1;
            ni = ++starN;
        }
        else
        {
            return false;
        }
    }
    while (pi < p.size() && p[pi] == '*') pi++;
    return pi == p.size();
}

std::string UdfReader::FullName(const UdfNodePtr& node) const
{
    std::string path;
    for (UdfNodePtr n = node; n && n != root_; n = n->Parent.lock())
        path = path.empty() ? n->Name : n->Name + "\\" + path;
    return path;
}

FileRefPtr UdfReader::MakeFile(const UdfNodePtr& node)
{
    auto file = std::make_shared<FileRef>();
    file->Name = node->Name;
    file->FullName = FullName(node);
    file->Length = static_cast<int64_t>(node->Length);
    file->DiscNode = node;
    file->Reader = this;
    return file;
}

std::vector<FileRefPtr> UdfReader::GetFiles(const UdfNodePtr& directory, const std::string& pattern)
{
    std::vector<FileRefPtr> files;
    for (const UdfNodePtr& child : Children(directory))
        if (!child->IsDirectory && WildcardMatch(pattern, child->Name)) files.push_back(MakeFile(child));
    return files;
}

std::vector<UdfNodePtr> UdfReader::GetDirectories(const UdfNodePtr& directory)
{
    std::vector<UdfNodePtr> directories;
    for (const UdfNodePtr& child : Children(directory))
        if (child->IsDirectory) directories.push_back(child);
    return directories;
}

std::vector<FileRefPtr> UdfReader::GetFilesRecursive(const UdfNodePtr& directory, const std::string& pattern)
{
    // DiscUtils searches depth first, visiting entries in directory order.
    std::vector<FileRefPtr> files;
    for (const UdfNodePtr& child : Children(directory))
    {
        if (!child->IsDirectory && WildcardMatch(pattern, child->Name)) files.push_back(MakeFile(child));
        if (child->IsDirectory)
        {
            std::vector<FileRefPtr> nested = GetFilesRecursive(child, pattern);
            files.insert(files.end(), nested.begin(), nested.end());
        }
    }
    return files;
}

} // namespace bdinfo
