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

// Command-line front end (replaces the Windows GUI's FormMain/FormReport).

#include "bdinfo.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#include <sys/ioctl.h>
#include <unistd.h>

#ifndef BDINFO_VERSION
#define BDINFO_VERSION "0.7.6.2"
#endif

using namespace bdinfo;

namespace {

const char* Usage =
    "Usage: bdinfo [options] <path> [<report destination>]\n"
    "\n"
    "  <path>  A Blu-ray folder (the disc root, or its BDMV folder), a mounted\n"
    "          disc, or an ISO image.\n"
    "  <report destination>\n"
    "          Same as -o (BDInfoCLI-ng compatible form, as used by upsies).\n"
    "\n"
    "Options:\n"
    "  -l, --list               List the disc's playlists and exit (no scan).\n"
    "  -m, --mpls <names>       Report only these playlists, comma separated\n"
    "                           (e.g. 00800.MPLS,00801). Default: every playlist\n"
    "                           the GUI would list.\n"
    "  -o, --output <path>      Write the report to a file instead of stdout. If\n"
    "                           <path> is a directory, the report is saved there\n"
    "                           as BDINFO.<disc label>.txt (as the GUI's autosave).\n"
    "  -s, --no-scan            Skip the bitrate scan (fast, but the report has\n"
    "                           no measured bitrates).\n"
    "  -q, --quiet              Don't print progress to stderr.\n"
    "\n"
    "Report settings (defaults match the Windows GUI):\n"
    "      --no-diagnostics     Omit the STREAM DIAGNOSTICS section.\n"
    "      --no-summary         Omit the QUICK SUMMARY section.\n"
    "      --extended           Include extended stream diagnostics (HEVC HDR).\n"
    "      --no-ssif            Disable SSIF (3D interleaved file) support.\n"
    "      --sort-streams       Sort streams by type instead of keeping disc order.\n"
    "      --include-short      Include playlists shorter than --min-length.\n"
    "      --min-length <sec>   Minimum playlist length to list (default: 20).\n"
    "      --include-looping    Include playlists with looping clips.\n"
    "      --byte-sizes         Show exact byte counts in --list output.\n"
    "\n"
    "  -h, --help               Show this help.\n"
    "      --version            Show the version.\n";

bool Quiet = false;

// ---------------------------------------------------------------------------
// Progress output
// ---------------------------------------------------------------------------

FILE* Status = stderr;
bool StatusIsTerminal = isatty(STDERR_FILENO) != 0;
bool ProgressLineActive = false;
int ProgressLineWidth = 0;
std::mutex ProgressLock;

int TerminalWidth()
{
    for (int fd : {fileno(Status), STDOUT_FILENO, STDERR_FILENO})
    {
        struct winsize ws {};
        if (ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
    }
    return 0;
}

void Progress(std::string message)
{
    if (Quiet) return;
    std::lock_guard<std::mutex> lock(ProgressLock);
    if (!StatusIsTerminal)
    {
        fprintf(Status, "%s\n", message.c_str());
        fflush(Status);
    }
    else
    {
        int width = 79;
        // Some pseudo-terminals report a width of 0; keep the 80-column default then.
        int columns = TerminalWidth();
        if (columns > 0) width = std::max(20, columns - 1);
        if (static_cast<int>(message.size()) > width) message.resize(static_cast<size_t>(width));
        fprintf(Status, "\r%s", net::Pad(message, -width).c_str());
        fflush(Status);
        ProgressLineActive = true;
        ProgressLineWidth = width;
    }
}

void ClearProgressLine()
{
    std::lock_guard<std::mutex> lock(ProgressLock);
    if (ProgressLineActive)
    {
        fprintf(Status, "\r%s\r", std::string(static_cast<size_t>(ProgressLineWidth), ' ').c_str());
        fflush(Status);
        ProgressLineActive = false;
    }
}

int UsageError(const std::string& message)
{
    fprintf(stderr, "bdinfo: %s\n", message.c_str());
    fprintf(stderr, "Try 'bdinfo --help' for more information.\n");
    return 2;
}

// Writes text that may contain NUL characters (as .NET strings can).
void Write(FILE* stream, const std::string& text)
{
    fwrite(text.data(), 1, text.size(), stream);
    fflush(stream);
}

bool ScanWarning(const std::string& kind, const std::string& name, const std::exception& ex)
{
    ClearProgressLine();
    Write(stderr, "bdinfo: warning: An error occurred while scanning the " + kind + " " + name + ": " +
                      net::MessageOf(ex) + " (the disc may be copy-protected or damaged); continuing.\n");
    return true;
}

// int.TryParse(value, NumberStyles.Integer, InvariantCulture, ...)
bool TryParseInt(const std::string& value, int& result)
{
    std::string s = net::Trim(value);
    if (s.empty()) return false;
    size_t i = 0;
    bool negative = false;
    if (s[0] == '+' || s[0] == '-')
    {
        negative = s[0] == '-';
        i = 1;
    }
    if (i >= s.size()) return false;
    int64_t v = 0;
    for (; i < s.size(); i++)
    {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
        if (v > 2147483648LL) return false;
    }
    if (negative) v = -v;
    if (v > INT32_MAX || v < INT32_MIN) return false;
    result = static_cast<int>(v);
    return true;
}

// ---------------------------------------------------------------------------
// Playlists
// ---------------------------------------------------------------------------

// Same ordering as FormMain.ComparePlaylistFiles.
int ComparePlaylistFiles(const TSPlaylistFilePtr& x, const TSPlaylistFilePtr& y)
{
    if (!x && !y) return 0;
    if (!x) return 1;
    if (!y) return -1;
    if (x->TotalLength() > y->TotalLength()) return -1;
    if (y->TotalLength() > x->TotalLength()) return 1;
    return net::CompareOrdinal(x->Name, y->Name);
}

bool SharesClip(const TSPlaylistFile& x, const TSPlaylistFile& y)
{
    for (const auto& clip1 : x.StreamClips)
        for (const auto& clip2 : y.StreamClips)
            if (clip1->Name == clip2->Name) return true;
    return false;
}

// Mirrors FormMain.LoadPlaylists: valid playlists, longest first, grouped by
// shared stream clips. Returns (group number, playlist) in display order.
std::vector<std::pair<int, TSPlaylistFilePtr>> GetListedPlaylists(BDROM& bdrom)
{
    std::vector<std::vector<TSPlaylistFilePtr>> groups;

    std::vector<TSPlaylistFilePtr> sortedPlaylistFiles;
    for (auto& entry : bdrom.PlaylistFiles) sortedPlaylistFiles.push_back(entry.second);
    net::Sort(sortedPlaylistFiles, ComparePlaylistFiles);

    for (const TSPlaylistFilePtr& playlist1 : sortedPlaylistFiles)
    {
        if (!playlist1->IsValid()) continue;

        int matchingGroupIndex = 0;
        for (size_t groupIndex = 0; groupIndex < groups.size() && matchingGroupIndex == 0; groupIndex++)
        {
            for (const TSPlaylistFilePtr& playlist2 : groups[groupIndex])
            {
                if (!playlist2->IsValid()) continue;
                if (SharesClip(*playlist1, *playlist2))
                {
                    matchingGroupIndex = static_cast<int>(groupIndex) + 1;
                    break;
                }
            }
        }
        if (matchingGroupIndex > 0)
        {
            groups[static_cast<size_t>(matchingGroupIndex) - 1].push_back(playlist1);
        }
        else
        {
            groups.push_back({playlist1});
        }
    }

    std::vector<std::pair<int, TSPlaylistFilePtr>> listed;
    for (size_t groupIndex = 0; groupIndex < groups.size(); groupIndex++)
    {
        net::Sort(groups[groupIndex], ComparePlaylistFiles);
        for (const TSPlaylistFilePtr& playlist : groups[groupIndex])
        {
            listed.emplace_back(static_cast<int>(groupIndex) + 1, playlist);
        }
    }
    return listed;
}

bool ResolvePlaylists(BDROM& bdrom, const std::string& mplsList, std::vector<TSPlaylistFilePtr>& playlists)
{
    std::vector<std::string> items;
    std::string current;
    for (char c : mplsList)
    {
        if (c == ',' || c == ' ')
        {
            if (!current.empty()) items.push_back(current);
            current.clear();
        }
        else
        {
            current.push_back(c);
        }
    }
    if (!current.empty()) items.push_back(current);

    for (const std::string& item : items)
    {
        std::string name = net::ToUpper(net::Trim(item));
        if (!net::EndsWith(name, ".MPLS")) name += ".MPLS";

        TSPlaylistFilePtr* playlist = bdrom.PlaylistFiles.TryGet(name);
        if (playlist == nullptr)
        {
            Write(stderr, "bdinfo: Playlist not found on disc: " + item + "\n");
            return false;
        }
        if (!(*playlist)->IsInitialized)
        {
            Write(stderr, "bdinfo: Playlist could not be read: " + item + "\n");
            return false;
        }
        if (std::find(playlists.begin(), playlists.end(), *playlist) == playlists.end())
            playlists.push_back(*playlist);
    }
    return true;
}

void PrintPlaylists(BDROM& bdrom, const std::vector<std::pair<int, TSPlaylistFilePtr>>& listed)
{
    std::string out;
    out += net::Pad("Disc Label:", -16) + bdrom.VolumeLabel + "\n";
    if (!bdrom.DiscTitle.empty())
    {
        out += net::Pad("Disc Title:", -16) + bdrom.DiscTitle + "\n";
    }
    out += net::Pad("Disc Size:", -16) + net::N0(bdrom.Size) + " bytes (" +
           ToolBox::FormatFileSize(static_cast<double>(bdrom.Size)) + ")\n";
    out += "\n";
    out += net::Pad("Group", -6) + net::Pad("Playlist", -16) + net::Pad("Length", -10) + net::Pad("Estimated Size", 18) +
           net::Pad("Measured Size", 18) + "\n";

    bool hasHiddenTracks = false;
    for (const auto& [group, playlistPtr] : listed)
    {
        const TSPlaylistFile& playlist = *playlistPtr;
        if (playlist.HasHiddenTracks) hasHiddenTracks = true;

        net::TimeSpan length = net::TimeSpan::FromSecondsTruncated(playlist.TotalLength());
        int64_t estimated = BDInfoSettings::EnableSSIF && playlist.InterleavedFileSize() > 0
                                ? static_cast<int64_t>(playlist.InterleavedFileSize())
                                : static_cast<int64_t>(playlist.FileSize());

        std::string lengthText = net::D(static_cast<int>(length.TotalHours()), 2) + ":" + net::D(length.Minutes(), 2) +
                                 ":" + net::D(length.Seconds(), 2);
        out += net::Pad(std::to_string(group), -6) + net::Pad(playlist.Name + (playlist.HasHiddenTracks ? "*" : ""), -16) +
               net::Pad(lengthText, -10) +
               net::Pad(estimated > 0 ? ToolBox::FormatFileSize(static_cast<double>(estimated)) : "-", 18) +
               net::Pad(playlist.TotalAngleSize() > 0 ? ToolBox::FormatFileSize(static_cast<double>(playlist.TotalAngleSize()))
                                                      : "-",
                        18) +
               "\n";
    }

    if (hasHiddenTracks)
    {
        out += "\n";
        out += "(*) Some playlists on this disc have hidden tracks.\n";
    }
    fputs(out.c_str(), stdout);
    fflush(stdout);
}

// ---------------------------------------------------------------------------
// Scan and report
// ---------------------------------------------------------------------------

int64_t GetScanLength(const TSStreamFile& streamFile)
{
    if (BDInfoSettings::EnableSSIF && streamFile.InterleavedFile) return streamFile.InterleavedFile->File->Length;
    return streamFile.File->Length;
}

void ReportScanProgress(const TSStreamFile& file, int64_t finishedBytes, int64_t totalBytes,
                        std::chrono::steady_clock::time_point timeStarted)
{
    double progress = totalBytes > 0 ? static_cast<double>(finishedBytes) / totalBytes : 0;
    progress = std::max(0.0, std::min(1.0, progress));

    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - timeStarted).count();
    double remaining = progress > 0 && progress < 1 ? elapsed / progress - elapsed : 0;

    auto hms = [](double seconds) {
        int64_t s = static_cast<int64_t>(seconds);
        return net::D(s / 3600, 2) + ":" + net::D((s / 60) % 60, 2) + ":" + net::D(s % 60, 2);
    };
    std::string percent = net::Pad(net::Custom(progress * 100, 0, 15), 3);
    Progress("Scanning " + file.DisplayName() + "  " + percent + "%  elapsed " + hms(elapsed) + "  remaining " +
             hms(remaining));
}

// Mirrors FormMain.ScanBDROMWork: scans the stream files used by the chosen
// playlists (or every stream file when no playlists were chosen explicitly).
ScanBDROMResult ScanBitrates(BDROM& bdrom, const std::vector<TSPlaylistFilePtr>& playlists, bool onlySelected)
{
    ScanBDROMResult result;

    std::vector<TSStreamFilePtr> streamFiles;
    if (onlySelected)
    {
        for (const TSPlaylistFilePtr& playlist : playlists)
        {
            for (const TSStreamClipPtr& clip : playlist->StreamClips)
            {
                if (clip->StreamFile &&
                    std::find(streamFiles.begin(), streamFiles.end(), clip->StreamFile) == streamFiles.end())
                {
                    streamFiles.push_back(clip->StreamFile);
                }
            }
        }
    }
    else
    {
        for (auto& entry : bdrom.StreamFiles)
            if (entry.second) streamFiles.push_back(entry.second);
    }

    for (auto& entry : bdrom.PlaylistFiles)
    {
        entry.second->ClearBitrates();
    }

    int64_t totalBytes = 0;
    net::Dictionary<std::string, std::vector<TSPlaylistFile*>> playlistMap;
    for (const TSStreamFilePtr& streamFile : streamFiles)
    {
        totalBytes += GetScanLength(*streamFile);

        std::vector<TSPlaylistFile*> filePlaylists;
        for (auto& entry : bdrom.PlaylistFiles)
        {
            for (const TSStreamClipPtr& clip : entry.second->StreamClips)
            {
                if (clip->Name == streamFile->Name &&
                    std::find(filePlaylists.begin(), filePlaylists.end(), entry.second.get()) == filePlaylists.end())
                {
                    filePlaylists.push_back(entry.second.get());
                }
            }
        }
        playlistMap.Set(streamFile->Name, filePlaylists);
    }

    auto timeStarted = std::chrono::steady_clock::now();
    std::atomic<int64_t> finishedBytes{0};
    std::atomic<TSStreamFile*> current{nullptr};

    std::mutex timerLock;
    std::condition_variable timerWake;
    bool timerStop = false;
    std::thread timer([&]() {
        std::unique_lock<std::mutex> lock(timerLock);
        while (!timerWake.wait_for(lock, std::chrono::seconds(1), [&] { return timerStop; }))
        {
            TSStreamFile* file = current.load();
            if (file == nullptr) continue;
            ReportScanProgress(*file, finishedBytes.load() + file->Size.load(), totalBytes, timeStarted);
        }
    });

    try
    {
        for (const TSStreamFilePtr& streamFile : streamFiles)
        {
            current = streamFile.get();
            ReportScanProgress(*streamFile, finishedBytes.load(), totalBytes, timeStarted);

            try
            {
                streamFile->Scan(playlistMap[streamFile->Name], true);
            }
            catch (const std::exception& ex)
            {
                result.FileExceptions.Set(streamFile->Name, net::MessageOf(ex));
                ScanWarning("stream file", streamFile->Name, ex);
            }
            finishedBytes += GetScanLength(*streamFile);
        }
    }
    catch (...)
    {
        {
            std::lock_guard<std::mutex> lock(timerLock);
            timerStop = true;
        }
        timerWake.notify_all();
        timer.join();
        throw;
    }
    current = nullptr;
    {
        std::lock_guard<std::mutex> lock(timerLock);
        timerStop = true;
    }
    timerWake.notify_all();
    timer.join();

    int64_t elapsed = static_cast<int64_t>(
        std::chrono::duration<double>(std::chrono::steady_clock::now() - timeStarted).count());
    ClearProgressLine();
    if (!Quiet)
    {
        fprintf(Status, "Scan complete (%s:%s:%s).\n", net::D(elapsed / 3600, 2).c_str(),
                net::D((elapsed / 60) % 60, 2).c_str(), net::D(elapsed % 60, 2).c_str());
        fflush(Status);
    }

    result.ScanException = std::nullopt;
    return result;
}

void WriteReport(BDROM& bdrom, const std::vector<TSPlaylistFilePtr>& playlists, const ScanBDROMResult& scanResult,
                 std::string outputPath)
{
    if (!outputPath.empty() && outputPath != "-" && local::IsDirectory(outputPath))
    {
        std::string reportName = ToolBox::GetSafeFileName("BDINFO." + bdrom.VolumeLabel + ".txt");
        outputPath = outputPath == "/" ? "/" + reportName : outputPath + "/" + reportName;
    }

    Progress("Generating report...");
    ClearProgressLine();

    bool toStdout = outputPath.empty() || outputPath == "-";
    FILE* writer = stdout;
    if (!toStdout)
    {
        writer = fopen(outputPath.c_str(), "wb");
        if (writer == nullptr)
            throw net::Exception("Could not open '" + outputPath + "' for writing: " + strerror(errno));
    }
    try
    {
        ReportGenerator::Generate(bdrom, playlists, scanResult, BDINFO_VERSION, [&](const std::string& report) {
            if (!report.empty() && fwrite(report.data(), 1, report.size(), writer) != report.size())
                throw net::Exception("Error writing the report: " + std::string(strerror(errno)));
        });
        fflush(writer);
    }
    catch (...)
    {
        if (!toStdout) fclose(writer);
        throw;
    }
    if (!toStdout)
    {
        if (fclose(writer) != 0) throw net::Exception("Error writing the report: " + std::string(strerror(errno)));
        if (!Quiet)
        {
            fprintf(Status, "Report saved to %s\n", outputPath.c_str());
            fflush(Status);
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);

    std::optional<std::string> path;
    std::optional<std::string> outputPath;
    std::optional<std::string> reportDestination;
    std::optional<std::string> mplsList;
    bool listOnly = false;
    bool scan = true;

    for (size_t i = 0; i < args.size(); i++)
    {
        std::string arg = args[i];

        // Accept GNU-style "--option=value" as well as "--option value".
        std::optional<std::string> inlineValue;
        bool valueUsed = false;
        size_t equals = arg.find('=');
        if (net::StartsWith(arg, "--") && equals != std::string::npos && equals > 2)
        {
            inlineValue = arg.substr(equals + 1);
            arg = arg.substr(0, equals);
        }

        auto takeValue = [&]() -> std::optional<std::string> {
            valueUsed = true;
            if (inlineValue) return inlineValue->empty() ? std::nullopt : inlineValue;
            if (i + 1 < args.size()) return args[++i];
            return std::nullopt;
        };

        if (arg == "-h" || arg == "--help")
        {
            fputs(Usage, stdout);
            return 0;
        }
        else if (arg == "--version")
        {
            printf("%s\n", BDINFO_VERSION);
            return 0;
        }
        else if (arg == "-l" || arg == "--list") listOnly = true;
        else if (arg == "-s" || arg == "--no-scan") scan = false;
        else if (arg == "-q" || arg == "--quiet") Quiet = true;
        else if (arg == "-m" || arg == "--mpls")
        {
            if (!(mplsList = takeValue())) return UsageError(arg + " requires a value.");
        }
        else if (arg == "-o" || arg == "--output")
        {
            if (!(outputPath = takeValue())) return UsageError(arg + " requires a value.");
        }
        else if (arg == "--no-diagnostics") BDInfoSettings::GenerateStreamDiagnostics = false;
        else if (arg == "--no-summary") BDInfoSettings::GenerateTextSummary = false;
        else if (arg == "--extended") BDInfoSettings::ExtendedStreamDiagnostics = true;
        else if (arg == "--no-ssif") BDInfoSettings::EnableSSIF = false;
        else if (arg == "--sort-streams") BDInfoSettings::KeepStreamOrder = false;
        else if (arg == "--include-short") BDInfoSettings::FilterShortPlaylists = false;
        else if (arg == "--include-looping") BDInfoSettings::FilterLoopingPlaylists = false;
        else if (arg == "--byte-sizes") BDInfoSettings::MainFormHRSizeFormat = false;
        else if (arg == "--min-length")
        {
            std::optional<std::string> value = takeValue();
            if (!value || !TryParseInt(*value, BDInfoSettings::FilterShortPlaylistsValue))
            {
                return UsageError("--min-length expects a whole number of seconds.");
            }
        }
        else
        {
            if (net::StartsWith(arg, "-") && arg.size() > 1)
            {
                return UsageError("Unknown option: " + arg);
            }
            if (!path) path = arg;
            else if (!reportDestination) reportDestination = arg;
            else return UsageError("Too many arguments: " + arg);
        }
        if (inlineValue && !valueUsed)
        {
            return UsageError(arg + " does not take a value.");
        }
    }

    if (!path)
    {
        return UsageError("No disc path given.");
    }
    if (reportDestination)
    {
        if (outputPath)
        {
            return UsageError("Give the report destination either with -o or as a second path, not both.");
        }
        outputPath = reportDestination;
    }

    // When the report goes to a file, status and progress go to stdout so that stderr
    // only carries warnings and errors (tools such as upsies treat any stderr output
    // as failure). When the report itself goes to stdout, status goes to stderr.
    if (outputPath && *outputPath != "-")
    {
        Status = stdout;
        StatusIsTerminal = isatty(STDOUT_FILENO) != 0;
    }
    if (!local::Exists(*path))
    {
        fprintf(stderr, "bdinfo: %s: No such file or directory\n", path->c_str());
        return 1;
    }

    std::unique_ptr<BDROM> bdrom;
    try
    {
        Progress("Reading disc structure...");
        bdrom = std::make_unique<BDROM>(*path);
        bdrom->StreamClipFileScanError = [](TSStreamClipFile& file, const std::exception& ex) {
            return ScanWarning("stream clip file", file.Name, ex);
        };
        bdrom->StreamFileScanError = [](TSStreamFile& file, const std::exception& ex) {
            return ScanWarning("stream file", file.Name, ex);
        };
        bdrom->PlaylistFileScanError = [](TSPlaylistFile& file, const std::exception& ex) {
            return ScanWarning("playlist file", file.Name, ex);
        };
        if (StatusIsTerminal)
        {
            bdrom->StreamFileScanStarted = [](TSStreamFile& file, int index, int count) {
                Progress("Reading disc structure: " + file.DisplayName() + " (" + std::to_string(index + 1) + "/" +
                         std::to_string(count) + ")...");
            };
        }
        bdrom->Scan();

        std::vector<std::pair<int, TSPlaylistFilePtr>> listed = GetListedPlaylists(*bdrom);

        ClearProgressLine();
        if (listOnly)
        {
            PrintPlaylists(*bdrom, listed);
            return 0;
        }

        std::vector<TSPlaylistFilePtr> playlists;
        if (mplsList)
        {
            if (!ResolvePlaylists(*bdrom, *mplsList, playlists)) return 1;
        }
        else
        {
            for (const auto& item : listed) playlists.push_back(item.second);
        }

        if (playlists.empty())
        {
            fprintf(stderr, "bdinfo: No playlists to report. Try --include-short or --mpls.\n");
            return 1;
        }

        ScanBDROMResult scanResult = scan ? ScanBitrates(*bdrom, playlists, mplsList.has_value()) : ScanBDROMResult();

        WriteReport(*bdrom, playlists, scanResult, outputPath.value_or(""));

        if (scanResult.ScanException && scan)
        {
            Write(stderr, "bdinfo: Scan failed: " + *scanResult.ScanException + "\n");
            return 1;
        }
        if (scanResult.FileExceptions.Count() > 0)
        {
            fprintf(stderr, "bdinfo: Scan completed with errors (see report).\n");
        }
        return 0;
    }
    catch (const std::exception& ex)
    {
        Write(stderr, "\nbdinfo: " + net::MessageOf(ex) + "\n");
        return 1;
    }
}
