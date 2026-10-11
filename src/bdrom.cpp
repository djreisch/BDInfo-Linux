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

// BDROM.cs

#include "bdinfo.h"

#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

#include <climits>
#include <cstdlib>
#include <sys/stat.h>

namespace bdinfo {

namespace {

// ---------------------------------------------------------------------------
// Local directory helpers (DirectoryInfo), case-insensitive like Windows.
// ---------------------------------------------------------------------------

std::string GetDirectoryBDMV(const std::string& path)
{
    std::string dir = local::FullPath(path);
    while (!dir.empty())
    {
        if (net::ToUpper(local::FileName(dir)) == "BDMV")
        {
            return dir;
        }
        dir = local::ParentPath(dir);
    }

    std::string root = local::FullPath(path);
    for (const std::string& child : local::GetDirectories(root))
    {
        if (net::ToUpper(local::FileName(child)) == "BDMV") return child;
    }
    return "";
}

std::string GetDirectory(const std::string& name, const std::string& dir)
{
    if (!dir.empty())
    {
        for (const std::string& child : local::GetDirectories(dir))
        {
            if (net::ToUpper(local::FileName(child)) == net::ToUpper(name))
            {
                return child;
            }
        }
    }
    return "";
}

std::vector<FileRefPtr> GetFilesByExtension(const std::string& dir, const std::string& extension)
{
    std::vector<FileRefPtr> files;
    for (const FileRefPtr& file : local::GetFiles(dir))
        if (net::ToUpper(file->Extension()) == net::ToUpper(extension)) files.push_back(file);
    return files;
}

std::vector<FileRefPtr> GetFilesByName(const std::string& dir, const std::string& name, bool recursive)
{
    std::vector<FileRefPtr> files;
    for (const FileRefPtr& file : recursive ? local::GetFilesRecursive(dir) : local::GetFiles(dir))
        if (net::ToUpper(file->Name) == net::ToUpper(name)) files.push_back(file);
    return files;
}

int64_t GetDirectorySize(const std::string& directory)
{
    int64_t size = 0;
    for (const FileRefPtr& file : local::GetFiles(directory))
    {
        if (net::ToUpper(file->Extension()) == ".SSIF")
        {
            continue;
        }
        size += file->Length;
    }
    for (const std::string& child : local::GetDirectories(directory))
    {
        size += GetDirectorySize(child);
    }
    return size;
}

// ---------------------------------------------------------------------------
// Disc image helpers (DiscDirectoryInfo).
// ---------------------------------------------------------------------------

UdfNodePtr GetDiscDirectory(UdfReader& reader, const std::string& name, const UdfNodePtr& dir)
{
    if (dir)
    {
        for (const UdfNodePtr& child : reader.GetDirectories(dir))
        {
            if (child->Name == name)
            {
                return child;
            }
        }
    }
    return nullptr;
}

int64_t GetDiscDirectorySize(UdfReader& reader, const UdfNodePtr& directory)
{
    int64_t size = 0;
    for (const FileRefPtr& file : reader.GetFiles(directory, "*.*"))
    {
        if (net::ToUpper(file->Extension()) == "SSIF")
        {
            continue;
        }
        size += file->Length;
    }
    for (const UdfNodePtr& child : reader.GetDirectories(directory))
    {
        size += GetDiscDirectorySize(reader, child);
    }
    return size;
}

// ---------------------------------------------------------------------------
// Volume label of a mounted file system (/proc/self/mountinfo + udev).
// ---------------------------------------------------------------------------

std::string UnescapeMountField(const std::string& value)
{
    std::string result;
    for (size_t i = 0; i < value.size(); i++)
    {
        if (value[i] == '\\' && i + 3 < value.size() && value[i + 1] == 'x')
        {
            result.push_back(static_cast<char>(std::stoi(value.substr(i + 2, 2), nullptr, 16)));
            i += 3;
        }
        else if (value[i] == '\\' && i + 3 < value.size() && isdigit(static_cast<unsigned char>(value[i + 1])) &&
                 isdigit(static_cast<unsigned char>(value[i + 2])) && isdigit(static_cast<unsigned char>(value[i + 3])))
        {
            result.push_back(static_cast<char>(std::stoi(value.substr(i + 1, 3), nullptr, 8)));
            i += 3;
        }
        else
        {
            result.push_back(value[i]);
        }
    }
    return result;
}

std::string ResolveLink(const std::string& path)
{
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) != nullptr) return resolved;
    return local::FullPath(path);
}

std::string GetUnixVolumeLabel(const std::string& path)
{
    try
    {
        std::string fullPath = local::FullPath(path);
        std::ifstream mountinfo("/proc/self/mountinfo");
        std::string line, bestMount, bestDevice;
        bool found = false;
        while (std::getline(mountinfo, line))
        {
            // Format: id parent major:minor root mountpoint options ... - fstype source superoptions
            std::vector<std::string> fields;
            std::istringstream words(line);
            for (std::string word; std::getline(words, word, ' ');) fields.push_back(word);
            int sep = -1;
            for (size_t i = 0; i < fields.size(); i++)
                if (fields[i] == "-")
                {
                    sep = static_cast<int>(i);
                    break;
                }
            if (fields.size() < 5 || sep < 0 || static_cast<size_t>(sep) + 2 >= fields.size()) continue;

            std::string mountPoint = UnescapeMountField(fields[4]);
            while (mountPoint.size() > 1 && mountPoint.back() == '/') mountPoint.pop_back();
            std::string prefix = mountPoint == "/" ? "/" : mountPoint + "/";
            bool contains = fullPath == mountPoint || net::StartsWith(fullPath, prefix);
            if (contains && (!found || mountPoint.size() >= bestMount.size()))
            {
                found = true;
                bestMount = mountPoint;
                bestDevice = UnescapeMountField(fields[static_cast<size_t>(sep) + 2]);
            }
        }

        if (!found || !net::StartsWith(bestDevice, "/dev/") || !local::IsDirectory("/dev/disk/by-label"))
        {
            return "";
        }

        std::string device = ResolveLink(bestDevice);
        for (const local::DirEntry& entry : local::List("/dev/disk/by-label"))
        {
            if (ResolveLink(entry.FullName) == device)
            {
                return UnescapeMountField(entry.Name);
            }
        }
    }
    catch (const std::exception&)
    {
    }
    return "";
}

// ---------------------------------------------------------------------------
// Minimal XML reader for META/DL/bdmt_eng.xml. It reproduces what
// XmlDocument + SelectSingleNode("di:discinfo/di:title/di:name") returns.
// ---------------------------------------------------------------------------

class XmlTitleReader
{
public:
    explicit XmlTitleReader(std::string text) : text_(std::move(text)) {}

    // Returns false for malformed documents (XmlException in C#).
    bool Parse(std::string& title)
    {
        struct Element
        {
            std::string Namespace;
            std::string LocalName;
            std::map<std::string, std::string> Prefixes;
            int Depth;
            bool Capturing;
        };
        std::vector<Element> stack;
        bool rootSeen = false;
        bool found = false;
        std::string capture;

        while (pos_ < text_.size())
        {
            if (text_[pos_] != '<')
            {
                size_t end = text_.find('<', pos_);
                if (end == std::string::npos) end = text_.size();
                std::string raw = text_.substr(pos_, end - pos_);
                pos_ = end;
                if (!stack.empty() && stack.back().Capturing && !found)
                {
                    std::string decoded;
                    if (!Decode(raw, decoded)) return false;
                    // Whitespace-only text nodes are dropped (PreserveWhitespace = false).
                    if (decoded.find_first_not_of(" \t\r\n") != std::string::npos) capture += decoded;
                }
                else if (stack.empty() && raw.find_first_not_of(" \t\r\n") != std::string::npos)
                {
                    return false;
                }
                continue;
            }

            if (text_.compare(pos_, 4, "<!--") == 0)
            {
                size_t end = text_.find("-->", pos_ + 4);
                if (end == std::string::npos) return false;
                pos_ = end + 3;
            }
            else if (text_.compare(pos_, 9, "<![CDATA[") == 0)
            {
                size_t end = text_.find("]]>", pos_ + 9);
                if (end == std::string::npos) return false;
                if (!stack.empty() && stack.back().Capturing && !found) capture += text_.substr(pos_ + 9, end - pos_ - 9);
                pos_ = end + 3;
            }
            else if (text_.compare(pos_, 2, "<?") == 0)
            {
                size_t end = text_.find("?>", pos_ + 2);
                if (end == std::string::npos) return false;
                pos_ = end + 2;
            }
            else if (text_.compare(pos_, 2, "<!") == 0)
            {
                size_t end = text_.find('>', pos_ + 2);
                if (end == std::string::npos) return false;
                pos_ = end + 1;
            }
            else if (text_.compare(pos_, 2, "</") == 0)
            {
                size_t end = text_.find('>', pos_ + 2);
                if (end == std::string::npos || stack.empty()) return false;
                std::string name = net::Trim(text_.substr(pos_ + 2, end - pos_ - 2));
                pos_ = end + 1;
                std::string expectedLocal, expectedNamespace;
                if (!Resolve(name, stack, stack.back().Prefixes, expectedNamespace, expectedLocal)) return false;
                if (expectedLocal != stack.back().LocalName) return false;
                if (stack.back().Capturing && !found)
                {
                    found = true;
                    title = capture;
                }
                stack.pop_back();
            }
            else
            {
                size_t end = FindTagEnd(pos_ + 1);
                if (end == std::string::npos) return false;
                std::string body = text_.substr(pos_ + 1, end - pos_ - 1);
                pos_ = end + 1;
                bool selfClosing = !body.empty() && body.back() == '/';
                if (selfClosing) body.pop_back();

                std::string name;
                std::vector<std::pair<std::string, std::string>> attributes;
                if (!ParseTag(body, name, attributes)) return false;
                if (stack.empty() && rootSeen) return false;
                rootSeen = true;

                Element element;
                element.Prefixes = stack.empty() ? std::map<std::string, std::string>() : stack.back().Prefixes;
                for (const auto& [key, value] : attributes)
                {
                    if (key == "xmlns") element.Prefixes[""] = value;
                    else if (net::StartsWith(key, "xmlns:")) element.Prefixes[key.substr(6)] = value;
                }
                if (!Resolve(name, stack, element.Prefixes, element.Namespace, element.LocalName)) return false;
                element.Depth = static_cast<int>(stack.size());

                // Path from the document element: discinfo/title/name in the di namespace.
                static const char* path[] = {"discinfo", "title", "name"};
                bool onPath = element.Depth >= 1 && element.Depth <= 3 &&
                              element.Namespace == "urn:BDA:bdmv;discinfo" &&
                              element.LocalName == path[element.Depth - 1] &&
                              (element.Depth == 1 || stack.back().LocalName == path[element.Depth - 2]);
                // Every ancestor on the path must match as well.
                for (size_t d = 1; onPath && d < stack.size(); d++)
                {
                    if (stack[d].Namespace != "urn:BDA:bdmv;discinfo" || stack[d].LocalName != path[d - 1]) onPath = false;
                }
                element.Capturing = onPath && element.Depth == 3 && !found;
                if (element.Capturing) capture.clear();

                if (selfClosing)
                {
                    if (element.Capturing && !found)
                    {
                        found = true;
                        title = "";
                    }
                }
                else
                {
                    stack.push_back(element);
                }
            }
        }
        if (!stack.empty() || !rootSeen) return false;
        return true;
    }

private:
    size_t FindTagEnd(size_t from) const
    {
        char quote = 0;
        for (size_t i = from; i < text_.size(); i++)
        {
            char c = text_[i];
            if (quote)
            {
                if (c == quote) quote = 0;
            }
            else if (c == '"' || c == '\'')
            {
                quote = c;
            }
            else if (c == '>')
            {
                return i;
            }
        }
        return std::string::npos;
    }

    bool ParseTag(const std::string& body, std::string& name,
                  std::vector<std::pair<std::string, std::string>>& attributes)
    {
        size_t i = 0;
        while (i < body.size() && !isspace(static_cast<unsigned char>(body[i]))) i++;
        name = body.substr(0, i);
        if (name.empty()) return false;
        while (i < body.size())
        {
            while (i < body.size() && isspace(static_cast<unsigned char>(body[i]))) i++;
            if (i >= body.size()) break;
            size_t eq = body.find('=', i);
            if (eq == std::string::npos) return false;
            std::string key = net::Trim(body.substr(i, eq - i));
            i = eq + 1;
            while (i < body.size() && isspace(static_cast<unsigned char>(body[i]))) i++;
            if (i >= body.size() || (body[i] != '"' && body[i] != '\'')) return false;
            char quote = body[i];
            size_t close = body.find(quote, i + 1);
            if (close == std::string::npos) return false;
            std::string value;
            if (!Decode(body.substr(i + 1, close - i - 1), value)) return false;
            attributes.emplace_back(key, value);
            i = close + 1;
        }
        return true;
    }

    template <class Stack>
    static bool Resolve(const std::string& qualified, const Stack&, const std::map<std::string, std::string>& prefixes,
                        std::string& ns, std::string& localName)
    {
        size_t colon = qualified.find(':');
        std::string prefix = colon == std::string::npos ? "" : qualified.substr(0, colon);
        localName = colon == std::string::npos ? qualified : qualified.substr(colon + 1);
        auto it = prefixes.find(prefix);
        if (it != prefixes.end())
        {
            ns = it->second;
            return true;
        }
        ns.clear();
        return prefix.empty(); // undeclared prefix: XmlException
    }

    static void AppendUtf8(std::string& out, uint32_t cp)
    {
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
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

    static bool Decode(const std::string& raw, std::string& out)
    {
        out.clear();
        for (size_t i = 0; i < raw.size(); i++)
        {
            if (raw[i] != '&')
            {
                // XML normalises CR LF and lone CR to LF.
                if (raw[i] == '\r')
                {
                    out.push_back('\n');
                    if (i + 1 < raw.size() && raw[i + 1] == '\n') i++;
                    continue;
                }
                out.push_back(raw[i]);
                continue;
            }
            size_t semi = raw.find(';', i);
            if (semi == std::string::npos) return false;
            std::string entity = raw.substr(i + 1, semi - i - 1);
            if (entity == "lt") out.push_back('<');
            else if (entity == "gt") out.push_back('>');
            else if (entity == "amp") out.push_back('&');
            else if (entity == "quot") out.push_back('"');
            else if (entity == "apos") out.push_back('\'');
            else if (!entity.empty() && entity[0] == '#')
            {
                char* end = nullptr;
                unsigned long cp = entity.size() > 1 && (entity[1] == 'x')
                                       ? strtoul(entity.c_str() + 2, &end, 16)
                                       : strtoul(entity.c_str() + 1, &end, 10);
                if (end == nullptr || *end != '\0' || cp == 0 || cp > 0x10FFFF) return false;
                AppendUtf8(out, static_cast<uint32_t>(cp));
            }
            else
            {
                return false;
            }
            i = semi;
        }
        return true;
    }

    std::string text_;
    size_t pos_ = 0;
};

// StreamReader(UTF-8, detect BOM): returns the text as UTF-8.
std::string DecodeText(const std::vector<uint8_t>& bytes)
{
    auto utf16 = [&](size_t start, bool bigEndian) {
        std::string out;
        for (size_t i = start; i + 1 < bytes.size(); i += 2)
        {
            uint32_t unit = bigEndian ? (bytes[i] << 8) | bytes[i + 1] : bytes[i] | (bytes[i + 1] << 8);
            if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < bytes.size())
            {
                uint32_t low = bigEndian ? (bytes[i + 2] << 8) | bytes[i + 3] : bytes[i + 2] | (bytes[i + 3] << 8);
                if (low >= 0xDC00 && low <= 0xDFFF)
                {
                    unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                    i += 2;
                }
            }
            std::string encoded;
            if (unit < 0x80) encoded.push_back(static_cast<char>(unit));
            else if (unit < 0x800)
            {
                encoded.push_back(static_cast<char>(0xC0 | (unit >> 6)));
                encoded.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
            }
            else if (unit < 0x10000)
            {
                encoded.push_back(static_cast<char>(0xE0 | (unit >> 12)));
                encoded.push_back(static_cast<char>(0x80 | ((unit >> 6) & 0x3F)));
                encoded.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
            }
            else
            {
                encoded.push_back(static_cast<char>(0xF0 | (unit >> 18)));
                encoded.push_back(static_cast<char>(0x80 | ((unit >> 12) & 0x3F)));
                encoded.push_back(static_cast<char>(0x80 | ((unit >> 6) & 0x3F)));
                encoded.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
            }
            out += encoded;
        }
        return out;
    };
    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) return utf16(2, false);
    if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) return utf16(2, true);
    size_t start = bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF ? 3 : 0;
    return std::string(bytes.begin() + static_cast<std::ptrdiff_t>(start), bytes.end());
}

std::vector<uint8_t> ReadAllBytes(InputStream& stream)
{
    std::vector<uint8_t> bytes;
    uint8_t chunk[65536];
    while (true)
    {
        int n = stream.Read(chunk, sizeof chunk);
        if (n <= 0) break;
        bytes.insert(bytes.end(), chunk, chunk + n);
    }
    return bytes;
}

} // namespace

// ---------------------------------------------------------------------------

BDROM::BDROM(const std::string& path)
{
    //
    // Locate BDMV directories.
    //
    if (!local::IsDirectory(path))
    {
        IsImage = true;
        CdReader = std::make_unique<UdfReader>(path);
    }

    if (!IsImage)
        DirectoryBDMV = GetDirectoryBDMV(path);
    else
        DiscDirectoryBDMV = CdReader->GetDirectory("BDMV");

    if ((!IsImage && DirectoryBDMV.empty()) || (IsImage && !DiscDirectoryBDMV))
    {
        throw net::Exception("Unable to locate BD structure.");
    }

    if (IsImage)
    {
        DiscDirectoryRoot = DiscDirectoryBDMV->Parent.lock();

        DiscDirectoryBDJO = GetDiscDirectory(*CdReader, "BDJO", DiscDirectoryBDMV);
        DiscDirectoryCLIPINF = GetDiscDirectory(*CdReader, "CLIPINF", DiscDirectoryBDMV);
        DiscDirectoryPLAYLIST = GetDiscDirectory(*CdReader, "PLAYLIST", DiscDirectoryBDMV);
        DiscDirectorySNP = GetDiscDirectory(*CdReader, "SNP", DiscDirectoryRoot);
        DiscDirectorySTREAM = GetDiscDirectory(*CdReader, "STREAM", DiscDirectoryBDMV);
        DiscDirectorySSIF = GetDiscDirectory(*CdReader, "SSIF", DiscDirectorySTREAM);
        DiscDirectoryMETA = GetDiscDirectory(*CdReader, "META", DiscDirectoryBDMV);
    }
    else
    {
        DirectoryRoot = local::ParentPath(DirectoryBDMV);

        DirectoryBDJO = GetDirectory("BDJO", DirectoryBDMV);
        DirectoryCLIPINF = GetDirectory("CLIPINF", DirectoryBDMV);
        DirectoryPLAYLIST = GetDirectory("PLAYLIST", DirectoryBDMV);
        DirectorySNP = GetDirectory("SNP", DirectoryRoot);
        DirectorySTREAM = GetDirectory("STREAM", DirectoryBDMV);
        DirectorySSIF = GetDirectory("SSIF", DirectorySTREAM);
        DirectoryMETA = GetDirectory("META", DirectoryBDMV);
    }

    if ((!IsImage && (DirectoryCLIPINF.empty() || DirectoryPLAYLIST.empty())) ||
        (IsImage && (!DiscDirectoryCLIPINF || !DiscDirectoryPLAYLIST)))
    {
        throw net::Exception("Unable to locate BD structure.");
    }

    //
    // Initialize basic disc properties.
    //
    if (IsImage)
    {
        UdfReader& reader = *CdReader;
        VolumeLabel = reader.VolumeLabel;
        Size = static_cast<uint64_t>(GetDiscDirectorySize(reader, DiscDirectoryRoot));

        for (const FileRefPtr& indexFile : reader.GetFiles(DiscDirectoryBDMV, "*.*"))
        {
            if (net::ToLower(indexFile->Name) == "index.bdmv")
            {
                ReadIndexVersion(*indexFile->OpenRead());
                break;
            }
        }

        if (GetDiscDirectory(reader, "BDSVM", DiscDirectoryRoot)) IsBDPlus = true;
        if (GetDiscDirectory(reader, "SLYVM", DiscDirectoryRoot)) IsBDPlus = true;
        if (GetDiscDirectory(reader, "ANYVM", DiscDirectoryRoot)) IsBDPlus = true;

        if (DiscDirectoryBDJO && !reader.GetFiles(DiscDirectoryBDJO, "*.*").empty())
        {
            IsBDJava = true;
        }

        if (DiscDirectorySNP && !reader.GetFiles(DiscDirectorySNP, "*.mnv").empty())
        {
            IsPSP = true;
        }

        if (DiscDirectorySSIF && !reader.GetFiles(DiscDirectorySSIF, "*.*").empty())
        {
            Is3D = true;
        }

        if (!reader.GetFiles(DiscDirectoryRoot, "FilmIndex.xml").empty())
        {
            IsDBOX = true;
        }

        if (DiscDirectoryMETA)
        {
            std::vector<FileRefPtr> metaFiles = reader.GetFilesRecursive(DiscDirectoryMETA, "bdmt_eng.xml");
            if (!metaFiles.empty())
            {
                ReadDiscTitle(*metaFiles[0]->OpenRead());
            }
        }

        //
        // Initialize file lists.
        //
        for (const FileRefPtr& file : reader.GetFiles(DiscDirectoryPLAYLIST, "*.mpls"))
        {
            PlaylistFiles.Add(net::ToUpper(file->Name), std::make_shared<TSPlaylistFile>(this, file));
        }

        if (DiscDirectorySTREAM)
        {
            for (const FileRefPtr& file : reader.GetFiles(DiscDirectorySTREAM, "*.m2ts"))
            {
                StreamFiles.Add(net::ToUpper(file->Name), std::make_shared<TSStreamFile>(file));
            }
        }

        for (const FileRefPtr& file : reader.GetFiles(DiscDirectoryCLIPINF, "*.clpi"))
        {
            StreamClipFiles.Add(net::ToUpper(file->Name), std::make_shared<TSStreamClipFile>(file));
        }

        if (DiscDirectorySSIF)
        {
            for (const FileRefPtr& file : reader.GetFiles(DiscDirectorySSIF, "*.ssif"))
            {
                InterleavedFiles.Add(net::ToUpper(file->Name), std::make_shared<TSInterleavedFile>(file));
            }
        }
    }
    else
    {
        VolumeLabel = GetUnixVolumeLabel(DirectoryRoot);
        if (VolumeLabel.empty()) VolumeLabel = local::FileName(DirectoryRoot);
        Size = static_cast<uint64_t>(GetDirectorySize(DirectoryRoot));

        for (const FileRefPtr& indexFile : local::GetFiles(DirectoryBDMV))
        {
            if (net::ToLower(indexFile->Name) == "index.bdmv")
            {
                ReadIndexVersion(*indexFile->OpenRead());
                break;
            }
        }

        if (!GetDirectory("BDSVM", DirectoryRoot).empty()) IsBDPlus = true;
        if (!GetDirectory("SLYVM", DirectoryRoot).empty()) IsBDPlus = true;
        if (!GetDirectory("ANYVM", DirectoryRoot).empty()) IsBDPlus = true;

        if (!DirectoryBDJO.empty() && !local::GetFiles(DirectoryBDJO).empty())
        {
            IsBDJava = true;
        }

        if (!DirectorySNP.empty() && !GetFilesByExtension(DirectorySNP, ".mnv").empty())
        {
            IsPSP = true;
        }

        if (!DirectorySSIF.empty() && !local::GetFiles(DirectorySSIF).empty())
        {
            Is3D = true;
        }

        if (!GetFilesByName(DirectoryRoot, "FilmIndex.xml", false).empty())
        {
            IsDBOX = true;
        }

        if (!DirectoryMETA.empty())
        {
            std::vector<FileRefPtr> metaFiles = GetFilesByName(DirectoryMETA, "bdmt_eng.xml", true);
            if (!metaFiles.empty())
            {
                ReadDiscTitle(*metaFiles[0]->OpenRead());
            }
        }

        //
        // Initialize file lists.
        //
        for (const FileRefPtr& file : GetFilesByExtension(DirectoryPLAYLIST, ".mpls"))
        {
            PlaylistFiles.Add(net::ToUpper(file->Name), std::make_shared<TSPlaylistFile>(this, file));
        }

        if (!DirectorySTREAM.empty())
        {
            for (const FileRefPtr& file : GetFilesByExtension(DirectorySTREAM, ".m2ts"))
            {
                StreamFiles.Add(net::ToUpper(file->Name), std::make_shared<TSStreamFile>(file));
            }
        }

        for (const FileRefPtr& file : GetFilesByExtension(DirectoryCLIPINF, ".clpi"))
        {
            StreamClipFiles.Add(net::ToUpper(file->Name), std::make_shared<TSStreamClipFile>(file));
        }

        if (!DirectorySSIF.empty())
        {
            for (const FileRefPtr& file : GetFilesByExtension(DirectorySSIF, ".ssif"))
            {
                InterleavedFiles.Add(net::ToUpper(file->Name), std::make_shared<TSInterleavedFile>(file));
            }
        }
    }
}

BDROM::~BDROM() = default;

void BDROM::ReadDiscTitle(InputStream& stream)
{
    DiscTitle.clear();
    try
    {
        XmlTitleReader reader(DecodeText(ReadAllBytes(stream)));
        std::string title;
        if (!reader.Parse(title)) return;
        DiscTitle = title;

        if (!DiscTitle.empty() && net::ToLower(DiscTitle) == "blu-ray") DiscTitle.clear();
    }
    catch (const std::exception&)
    {
        DiscTitle.clear();
    }
}

void BDROM::ReadIndexVersion(InputStream& stream)
{
    std::vector<uint8_t> buffer(8);
    int count = stream.Read(buffer.data(), 8);
    int pos = 0;
    if (count > 0)
    {
        std::string indexVer = ToolBox::ReadString(buffer, count, pos);
        IsUHD = indexVer == "INDX0300";
    }
}

int BDROM::CompareStreamFiles(const TSStreamFilePtr& x, const TSStreamFilePtr& y)
{
    // TODO: Use interleaved file sizes. Files inside a disc image have no
    // FileInfo in the C# code, so they all compare equal.
    bool xLocal = x && !x->File->IsDisc();
    bool yLocal = y && !y->File->IsDisc();
    if (!xLocal && !yLocal) return 0;
    if (!xLocal && yLocal) return 1;
    if (xLocal && !yLocal) return -1;
    if (x->File->Length > y->File->Length) return 1;
    if (y->File->Length > x->File->Length) return -1;
    return 0;
}

void BDROM::Scan()
{
    for (auto& entry : StreamClipFiles)
    {
        TSStreamClipFile& streamClipFile = *entry.second;
        try
        {
            streamClipFile.Scan();
        }
        catch (const std::exception& ex)
        {
            if (StreamClipFileScanError)
            {
                if (StreamClipFileScanError(streamClipFile, ex)) continue;
                break;
            }
            throw;
        }
    }

    for (auto& entry : StreamFiles)
    {
        TSStreamFile& streamFile = *entry.second;
        std::string name = streamFile.Name;
        size_t dot = name.rfind('.');
        std::string ssifName = (dot == std::string::npos ? name : name.substr(0, dot)) + ".SSIF";
        if (InterleavedFiles.ContainsKey(ssifName))
        {
            streamFile.InterleavedFile = InterleavedFiles[ssifName];
        }
    }

    std::vector<TSStreamFilePtr> streamFiles;
    for (auto& entry : StreamFiles) streamFiles.push_back(entry.second);
    net::Sort(streamFiles, CompareStreamFiles);

    for (auto& entry : PlaylistFiles)
    {
        TSPlaylistFile& playlistFile = *entry.second;
        try
        {
            playlistFile.Scan(StreamFiles, StreamClipFiles);
        }
        catch (const std::exception& ex)
        {
            if (PlaylistFileScanError)
            {
                if (PlaylistFileScanError(playlistFile, ex)) continue;
                break;
            }
            throw;
        }
    }

    for (size_t streamFileIndex = 0; streamFileIndex < streamFiles.size(); streamFileIndex++)
    {
        TSStreamFile& streamFile = *streamFiles[streamFileIndex];
        if (StreamFileScanStarted)
            StreamFileScanStarted(streamFile, static_cast<int>(streamFileIndex), static_cast<int>(streamFiles.size()));
        try
        {
            std::vector<TSPlaylistFile*> playlists;
            for (auto& entry : PlaylistFiles)
            {
                for (const auto& streamClip : entry.second->StreamClips)
                {
                    if (streamClip->Name == streamFile.Name)
                    {
                        playlists.push_back(entry.second.get());
                        break;
                    }
                }
            }
            streamFile.Scan(playlists, false);
        }
        catch (const std::exception& ex)
        {
            if (StreamFileScanError)
            {
                if (StreamFileScanError(streamFile, ex)) continue;
                break;
            }
            throw;
        }
    }

    for (auto& entry : PlaylistFiles)
    {
        TSPlaylistFile& playlistFile = *entry.second;
        playlistFile.Initialize();
        if (!Is50Hz)
        {
            size_t vidStreamCount = playlistFile.VideoStreams.size();
            for (const auto& videoStream : playlistFile.VideoStreams)
            {
                if (videoStream->FrameRate() == TSFrameRate::FRAMERATE_25 ||
                    videoStream->FrameRate() == TSFrameRate::FRAMERATE_50)
                {
                    Is50Hz = true;
                }

                if (vidStreamCount > 1 && Is3D)
                {
                    if ((videoStream->StreamType == TSStreamType::AVC_VIDEO && playlistFile.MVCBaseViewR) ||
                        (videoStream->StreamType == TSStreamType::MVC_VIDEO && !playlistFile.MVCBaseViewR))
                        videoStream->BaseView = true;
                    else if (videoStream->StreamType == TSStreamType::AVC_VIDEO ||
                             videoStream->StreamType == TSStreamType::MVC_VIDEO)
                        videoStream->BaseView = false;
                }
            }
        }
    }
}

void BDROM::CloseDiscImage() { CdReader.reset(); }

} // namespace bdinfo
