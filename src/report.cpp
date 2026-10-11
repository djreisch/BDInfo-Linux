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

// Report generation from BDInfo's FormReport.cs. Emits Unix line endings
// where the C# code builds "\r\n" lines.

#include "bdinfo.h"

#include <deque>

namespace bdinfo {
namespace ReportGenerator {

using net::Pad;

namespace {

std::string Time(const net::TimeSpan& t) { return net::FormatTime(t, 1, true); }
std::string Time2(const net::TimeSpan& t) { return net::FormatTime(t, 2, true); }
net::TimeSpan Span(double seconds) { return net::TimeSpan::FromSecondsTruncated(seconds); }

// Math.Round(x / 10000) / 100 formatted with {0:F2}.
std::string Mbps(double bitsPerSecond) { return net::F(net::Round(bitsPerSecond / 10000) / 100, 2); }

} // namespace

void Generate(BDROM& bdrom, const std::vector<TSPlaylistFilePtr>& playlists, const ScanBDROMResult& scanResult,
              const std::string& productVersion, const std::function<void(const std::string&)>& output)
{
    std::string report;
    std::string protection = bdrom.IsBDPlus ? "BD+" : bdrom.IsUHD ? "AACS2" : "AACS";

    if (!bdrom.DiscTitle.empty()) report += Pad("Disc Title:", -16) + bdrom.DiscTitle + "\n";
    report += Pad("Disc Label:", -16) + bdrom.VolumeLabel + "\n";
    report += Pad("Disc Size:", -16) + net::N0(bdrom.Size) + " bytes\n";
    report += Pad("Protection:", -16) + protection + "\n";

    std::vector<std::string> extraFeatures;
    if (bdrom.IsUHD) extraFeatures.push_back("Ultra HD");
    if (bdrom.IsBDJava) extraFeatures.push_back("BD-Java");
    if (bdrom.Is50Hz) extraFeatures.push_back("50Hz Content");
    if (bdrom.Is3D) extraFeatures.push_back("Blu-ray 3D");
    if (bdrom.IsDBOX) extraFeatures.push_back("D-BOX Motion Code");
    if (bdrom.IsPSP) extraFeatures.push_back("PSP Digital Copy");
    if (!extraFeatures.empty())
    {
        report += Pad("Extras:", -16) + net::Join(", ", extraFeatures) + "\n";
    }
    report += Pad("BDInfo:", -16) + productVersion + "\n";

    report += "\n";
    report += Pad("Notes:", -16) + "\n";
    report += "\n";
    report += "BDINFO HOME:\n";
    report += "  Cinema Squid (old)\n";
    report += "    http://www.cinemasquid.com/blu-ray/tools/bdinfo\n";
    report += "  UniqProject GitHub (new)\n";
    report += "   https://github.com/UniqProject/BDInfo\n";
    report += "\n";
    report += "INCLUDES FORUMS REPORT FOR:\n";
    report += "  AVS Forum Blu-ray Audio and Video Specifications Thread\n";
    report += "    http://www.avsforum.com/avs-vb/showthread.php?t=1155731\n";
    report += "\n";

    if (scanResult.ScanException.has_value())
    {
        report += "WARNING: Report is incomplete because: " + *scanResult.ScanException + "\n";
    }
    if (scanResult.FileExceptions.Count() > 0)
    {
        report += "WARNING: File errors were encountered during scan:\n";
        for (const auto& entry : scanResult.FileExceptions)
        {
            report += "\n" + entry.first + "\t" + entry.second + "\n";
            // The C# report adds the exception's stack trace here; there is
            // no managed stack trace in the native build.
            report += "\n";
        }
    }

    for (const TSPlaylistFilePtr& playlistPtr : playlists)
    {
        TSPlaylistFile& playlist = *playlistPtr;
        std::string summary;

        std::string title = playlist.Name;
        std::string discSize = net::N0(bdrom.Size);

        net::TimeSpan playlistTotalLength = Span(playlist.TotalLength());
        std::string totalLength = Time(playlistTotalLength);
        std::string totalLengthShort = net::FormatTime(playlistTotalLength, 1, false);
        std::string totalSize = net::N0(playlist.TotalSize());
        std::string totalBitrate = Mbps(static_cast<double>(playlist.TotalBitRate()));

        net::TimeSpan playlistAngleLength = Span(playlist.TotalAngleLength());
        std::string totalAngleLength = Time(playlistAngleLength);
        std::string totalAngleSize = net::N0(playlist.TotalAngleSize());
        std::string totalAngleBitrate = Mbps(static_cast<double>(playlist.TotalAngleBitRate()));

        std::vector<std::string> angleLengths;
        std::vector<std::string> angleSizes;
        std::vector<std::string> angleBitrates;
        std::vector<std::string> angleTotalLengths;
        std::vector<std::string> angleTotalSizes;
        std::vector<std::string> angleTotalBitrates;
        if (playlist.AngleCount > 0)
        {
            for (int angleIndex = 0; angleIndex < playlist.AngleCount; angleIndex++)
            {
                double angleLength = 0;
                uint64_t angleSize = 0;
                uint64_t angleTotalSize = 0;
                if (angleIndex < static_cast<int>(playlist.AngleClips.size()))
                {
                    for (const auto& entry : playlist.AngleClips[static_cast<size_t>(angleIndex)])
                    {
                        const TSStreamClip& clip = *entry.second;
                        angleTotalSize += clip.PacketSize();
                        if (clip.AngleIndex == angleIndex + 1)
                        {
                            angleSize += clip.PacketSize();
                            angleLength += clip.Length;
                        }
                    }
                }

                angleSizes.push_back(net::N0(angleSize));
                angleLengths.push_back(Time(Span(angleLength)));
                angleTotalSizes.push_back(net::N0(angleTotalSize));
                angleTotalLengths.push_back(totalLength);

                double angleBitrate = 0;
                if (angleLength > 0)
                {
                    angleBitrate = net::Round(static_cast<double>(angleSize * 8) / angleLength / 10000) / 100;
                }
                angleBitrates.push_back(net::F(angleBitrate, 2) + " kbps");

                double angleTotalBitrate = 0;
                if (playlist.TotalLength() > 0)
                {
                    angleTotalBitrate =
                        net::Round(static_cast<double>(angleTotalSize * 8) / playlist.TotalLength() / 10000) / 100;
                }
                angleTotalBitrates.push_back(net::F(angleTotalBitrate, 2) + " kbps");
            }
        }

        std::string videoCodec;
        std::string videoBitrate;
        if (!playlist.VideoStreams.empty())
        {
            const TSStream& videoStream = *playlist.VideoStreams[0];
            videoCodec = videoStream.CodecAltName();
            videoBitrate = Mbps(static_cast<double>(videoStream.BitRate));
        }

        auto audioSummary = [](const TSAudioStream& audioStream) {
            std::string text = audioStream.CodecAltName() + " " + audioStream.ChannelDescription();
            if (audioStream.BitRate > 0)
            {
                text += " " + std::to_string(net::ToInt32(net::Round(static_cast<double>(audioStream.BitRate) / 1000))) +
                        " kbps";
            }
            if (audioStream.SampleRate > 0 && audioStream.BitDepth > 0)
            {
                text += " (" +
                        std::to_string(net::ToInt32(net::Round(static_cast<double>(audioStream.SampleRate) / 1000))) +
                        "kHz/" + std::to_string(audioStream.BitDepth) + "-bit)";
            }
            return text;
        };

        std::string audio1;
        std::string languageCode1;
        if (!playlist.AudioStreams.empty())
        {
            const TSAudioStream& audioStream = *playlist.AudioStreams[0];
            languageCode1 = audioStream.LanguageCode();
            audio1 = audioSummary(audioStream);
        }

        std::string audio2;
        if (playlist.AudioStreams.size() > 1)
        {
            for (size_t i = 1; i < playlist.AudioStreams.size(); i++)
            {
                const TSAudioStream& audioStream = *playlist.AudioStreams[i];

                if (audioStream.LanguageCode() == languageCode1 &&
                    audioStream.StreamType != TSStreamType::AC3_PLUS_SECONDARY_AUDIO &&
                    audioStream.StreamType != TSStreamType::DTS_HD_SECONDARY_AUDIO &&
                    !(audioStream.StreamType == TSStreamType::AC3_AUDIO && audioStream.ChannelCount == 2))
                {
                    audio2 = audioSummary(audioStream);
                    break;
                }
            }
        }

        report += "\n";
        report += "********************\n";
        report += "PLAYLIST: " + playlist.Name + "\n";
        report += "********************\n";
        report += "\n";
        report += "<--- BEGIN FORUMS PASTE --->\n";
        report += "[code]\n";

        auto forumRow = [](const std::string& a, const std::string& b, const std::string& c, const std::string& d,
                           const std::string& e, const std::string& f, const std::string& g, const std::string& h,
                           const std::string& i) {
            return Pad(a, -64) + Pad(b, -8) + Pad(c, -8) + Pad(d, -16) + Pad(e, -18) + Pad(f, -13) + Pad(g, -13) +
                   Pad(h, -42) + i + "\n";
        };

        report += forumRow("", "", "", "", "", "Total", "Video", "", "");
        report += forumRow("Title", "Codec", "Length", "Movie Size", "Disc Size", "Bitrate", "Bitrate",
                           "Main Audio Track", "Secondary Audio Track");
        report += forumRow("-----", "------", "-------", "--------------", "----------------", "-----------",
                           "-----------", "------------------", "---------------------");
        report += forumRow(title, videoCodec, totalLengthShort, totalSize, discSize, totalBitrate + " Mbps",
                           videoBitrate + " Mbps", audio1, audio2);

        report += "[/code]\n";
        report += "\n";
        report += "[code]\n";

        report += "\n";
        report += "DISC INFO:\n";
        report += "\n";

        if (!bdrom.DiscTitle.empty()) report += Pad("Disc Title:", -16) + bdrom.DiscTitle + "\n";
        report += Pad("Disc Label:", -16) + bdrom.VolumeLabel + "\n";
        report += Pad("Disc Size:", -16) + net::N0(bdrom.Size) + " bytes\n";
        report += Pad("Protection:", -16) + protection + "\n";
        if (!extraFeatures.empty())
        {
            report += Pad("Extras:", -16) + net::Join(", ", extraFeatures) + "\n";
        }
        report += Pad("BDInfo:", -16) + productVersion + "\n";

        report += "\n";
        report += "PLAYLIST REPORT:\n";
        report += "\n";
        report += Pad("Name:", -24) + title + "\n";
        report += Pad("Length:", -24) + totalLength + " (h:m:s.ms)\n";
        report += Pad("Size:", -24) + totalSize + " bytes\n";
        report += Pad("Total Bitrate:", -24) + totalBitrate + " Mbps\n";
        if (playlist.AngleCount > 0)
        {
            for (int angleIndex = 0; angleIndex < playlist.AngleCount; angleIndex++)
            {
                size_t a = static_cast<size_t>(angleIndex);
                report += "\n";
                report += Pad("Angle " + std::to_string(angleIndex + 1) + " Length:", -24) + angleLengths[a] +
                          " (h:m:s.ms) / " + angleTotalLengths[a] + " (h:m:s.ms)\n";
                report += Pad("Angle " + std::to_string(angleIndex + 1) + " Size:", -24) + angleSizes[a] + " bytes / " +
                          angleTotalSizes[a] + " bytes\n";
                report += Pad("Angle " + std::to_string(angleIndex + 1) + " Total Bitrate:", -24) + angleBitrates[a] +
                          " Mbps / " + angleTotalBitrates[a] + " Mbps\n";
            }
            report += "\n";
            report += Pad("All Angles Length:", -24) + totalAngleLength + " (h:m:s.ms)\n";
            report += Pad("All Angles Size:", -24) + totalAngleSize + " bytes\n";
            report += Pad("All Angles Bitrate:", -24) + totalAngleBitrate + " Mbps\n";
        }

        if (!bdrom.DiscTitle.empty()) summary += Pad("Disc Title:", -16) + bdrom.DiscTitle + "\n";
        summary += Pad("Disc Label:", -16) + bdrom.VolumeLabel + "\n";
        summary += Pad("Disc Size:", -16) + net::N0(bdrom.Size) + " bytes\n";
        summary += Pad("Protection:", -16) + protection + "\n";
        summary += Pad("Playlist:", -16) + title + "\n";
        summary += Pad("Size:", -16) + totalSize + " bytes\n";
        summary += Pad("Length:", -16) + totalLength + "\n";
        summary += Pad("Total Bitrate:", -16) + totalBitrate + " Mbps\n";

        if (playlist.HasHiddenTracks)
        {
            report += "\n(*) Indicates included stream hidden by this playlist.\n";
        }

        if (!playlist.VideoStreams.empty())
        {
            report += "\n";
            report += "VIDEO:\n";
            report += "\n";
            report += Pad("Codec", -24) + Pad("Bitrate", -20) + Pad("Description", -16) + "\n";
            report += Pad("---------------", -24) + Pad("-------------", -20) + Pad("-----------", -16) + "\n";

            for (const TSStreamPtr& stream : playlist.SortedStreams)
            {
                if (!stream->IsVideoStream()) continue;

                std::string streamName = stream->CodecName();
                if (stream->AngleIndex > 0)
                {
                    streamName += " (" + std::to_string(stream->AngleIndex) + ")";
                }

                std::string streamBitrate = net::N0(static_cast<int64_t>(net::ToInt32(net::Round(static_cast<double>(stream->BitRate) / 1000))));
                if (stream->AngleIndex > 0)
                {
                    streamBitrate += " (" + net::D(net::ToInt32(net::Round(static_cast<double>(stream->ActiveBitRate) / 1000))) + ")";
                }
                streamBitrate += " kbps";

                std::string description = stream->Description();
                report += Pad((stream->IsHidden ? "* " : "") + streamName, -24) + Pad(streamBitrate, -20) +
                          Pad(description, -16) + "\n";

                summary += Pad(std::string(stream->IsHidden ? "* " : "") + "Video:", -16) + streamName + " / " +
                           streamBitrate + " / " + description + "\n";
            }
        }

        if (!playlist.AudioStreams.empty())
        {
            report += "\n";
            report += "AUDIO:\n";
            report += "\n";
            report += Pad("Codec", -32) + Pad("Language", -16) + Pad("Bitrate", -16) + Pad("Description", -16) + "\n";
            report += Pad("---------------", -32) + Pad("-------------", -16) + Pad("-------------", -16) +
                      Pad("-----------", -16) + "\n";

            for (const TSStreamPtr& stream : playlist.SortedStreams)
            {
                if (!stream->IsAudioStream()) continue;

                std::string streamBitrate =
                    Pad(net::D(net::ToInt32(net::Round(static_cast<double>(stream->BitRate) / 1000))), 5) + " kbps";

                std::string description = stream->Description();
                report += Pad((stream->IsHidden ? "* " : "") + stream->CodecName(), -32) + Pad(stream->LanguageName, -16) +
                          Pad(streamBitrate, -16) + Pad(description, -16) + "\n";

                summary += Pad(std::string(stream->IsHidden ? "* " : "") + "Audio:", -16) + stream->LanguageName + " / " +
                           stream->CodecName() + " / " + description + "\n";
            }
        }

        if (!playlist.GraphicsStreams.empty())
        {
            report += "\n";
            report += "SUBTITLES:\n";
            report += "\n";
            report += Pad("Codec", -32) + Pad("Language", -16) + Pad("Bitrate", -16) + Pad("Description", -16) + "\n";
            report += Pad("---------------", -32) + Pad("-------------", -16) + Pad("-------------", -16) +
                      Pad("-----------", -16) + "\n";

            for (const TSStreamPtr& stream : playlist.SortedStreams)
            {
                if (!stream->IsGraphicsStream()) continue;

                std::string streamBitrate = Pad(net::F(static_cast<double>(stream->BitRate) / 1000, 2), 5) + " kbps";

                std::string description = stream->Description();
                report += Pad((stream->IsHidden ? "* " : "") + stream->CodecName(), -32) + Pad(stream->LanguageName, -16) +
                          Pad(streamBitrate, -16) + Pad(description, -16) + "\n";

                summary += Pad(std::string(stream->IsHidden ? "* " : "") + "Subtitle:", -16) + stream->LanguageName +
                           " / " + net::Trim(streamBitrate) + "\n";
            }
        }

        if (!playlist.TextStreams.empty())
        {
            report += "\n";
            report += "TEXT:\n";
            report += "\n";
            report += Pad("Codec", -32) + Pad("Language", -16) + Pad("Bitrate", -16) + Pad("Description", -16) + "\n";
            report += Pad("---------------", -32) + Pad("-------------", -16) + Pad("-------------", -16) +
                      Pad("-----------", -16) + "\n";

            for (const TSStreamPtr& stream : playlist.SortedStreams)
            {
                if (!stream->IsTextStream()) continue;

                std::string streamBitrate = Pad(net::F(static_cast<double>(stream->BitRate) / 1000, 2), 5) + " kbps";

                report += Pad((stream->IsHidden ? "* " : "") + stream->CodecName(), -32) + Pad(stream->LanguageName, -16) +
                          Pad(streamBitrate, -16) + Pad(stream->Description(), -16) + "\n";
            }
        }

        report += "\n";
        report += "FILES:\n";
        report += "\n";
        report += Pad("Name", -16) + Pad("Time In", -16) + Pad("Length", -16) + Pad("Size", -16) +
                  Pad("Total Bitrate", -16) + "\n";
        report += Pad("---------------", -16) + Pad("-------------", -16) + Pad("-------------", -16) +
                  Pad("-------------", -16) + Pad("-------------", -16) + "\n";

        for (const TSStreamClipPtr& clipPtr : playlist.StreamClips)
        {
            const TSStreamClip& clip = *clipPtr;
            std::string clipName = clip.DisplayName();

            if (clip.AngleIndex > 0)
            {
                clipName += " (" + std::to_string(clip.AngleIndex) + ")";
            }

            std::string clipSize = net::N0(clip.PacketSize());

            std::string clipTimeIn = Time(Span(clip.RelativeTimeIn));
            std::string clipLength = Time(Span(clip.Length));

            std::string clipBitrate =
                Pad(net::N(net::Round(static_cast<double>(clip.PacketBitRate()) / 1000), 0), 6) + " kbps";

            report += Pad(clipName, -16) + Pad(clipTimeIn, -16) + Pad(clipLength, -16) + Pad(clipSize, -16) +
                      Pad(clipBitrate, -16) + "\n";
        }

        report += "\n";
        report += "CHAPTERS:\n";
        report += "\n";

        auto chapterRow = [](const std::vector<std::string>& columns) {
            std::string row;
            for (const std::string& column : columns) row += Pad(column, -16);
            return row + "\n";
        };
        report += chapterRow({"Number", "Time In", "Length", "Avg Video Rate", "Max 1-Sec Rate", "Max 1-Sec Time",
                              "Max 5-Sec Rate", "Max 5-Sec Time", "Max 10Sec Rate", "Max 10Sec Time", "Avg Frame Size",
                              "Max Frame Size", "Max Frame Time"});
        report += chapterRow({"------", "-------------", "-------------", "--------------", "--------------",
                              "--------------", "--------------", "--------------", "--------------", "--------------",
                              "--------------", "--------------", "--------------"});

        std::deque<double> window1Bits;
        std::deque<double> window1Seconds;
        double window1BitsSum = 0;
        double window1SecondsSum = 0;
        double window1PeakBitrate = 0;
        double window1PeakLocation = 0;

        std::deque<double> window5Bits;
        std::deque<double> window5Seconds;
        double window5BitsSum = 0;
        double window5SecondsSum = 0;
        double window5PeakBitrate = 0;
        double window5PeakLocation = 0;

        std::deque<double> window10Bits;
        std::deque<double> window10Seconds;
        double window10BitsSum = 0;
        double window10SecondsSum = 0;
        double window10PeakBitrate = 0;
        double window10PeakLocation = 0;

        double chapterPosition = 0;
        double chapterBits = 0;
        int64_t chapterFrameCount = 0;
        double chapterSeconds = 0;
        double chapterMaxFrameSize = 0;
        double chapterMaxFrameLocation = 0;

        uint16_t diagPID = net::L(playlist.VideoStreams, 0)->PID;

        int chapterIndex = 0;
        int clipIndex = 0;
        int diagIndex = 0;

        auto dequeue = [](std::deque<double>& queue) {
            if (queue.empty()) throw net::Exception("Queue empty.");
            double value = queue.front();
            queue.pop_front();
            return value;
        };

        while (chapterIndex < static_cast<int>(playlist.Chapters.size()))
        {
            TSStreamClip* clip = nullptr;
            TSStreamFile* file = nullptr;

            if (clipIndex < static_cast<int>(playlist.StreamClips.size()))
            {
                clip = playlist.StreamClips[static_cast<size_t>(clipIndex)].get();
                file = clip->StreamFile.get();
            }

            double chapterStart = playlist.Chapters[static_cast<size_t>(chapterIndex)];
            double chapterEnd;
            if (chapterIndex < static_cast<int>(playlist.Chapters.size()) - 1)
            {
                chapterEnd = playlist.Chapters[static_cast<size_t>(chapterIndex) + 1];
            }
            else
            {
                chapterEnd = playlist.TotalLength();
            }
            double chapterLength = chapterEnd - chapterStart;

            std::vector<TSStreamDiagnostics>* diagList = nullptr;

            if (clip != nullptr && clip->AngleIndex == 0 && file != nullptr && file->StreamDiagnostics.ContainsKey(diagPID))
            {
                diagList = file->StreamDiagnostics[diagPID].get();

                while (diagIndex < static_cast<int>(diagList->size()) && chapterPosition < chapterEnd)
                {
                    const TSStreamDiagnostics& diag = (*diagList)[static_cast<size_t>(diagIndex++)];

                    if (diag.Marker < clip->TimeIn) continue;

                    chapterPosition = diag.Marker - clip->TimeIn + clip->RelativeTimeIn;

                    double seconds = diag.Interval;
                    double bits = diag.Bytes * 8.0;

                    chapterBits += bits;
                    chapterSeconds += seconds;

                    if (diag.Tag.has_value())
                    {
                        chapterFrameCount++;
                    }

                    window1SecondsSum += seconds;
                    window1Seconds.push_back(seconds);
                    window1BitsSum += bits;
                    window1Bits.push_back(bits);

                    window5SecondsSum += diag.Interval;
                    window5Seconds.push_back(diag.Interval);
                    window5BitsSum += bits;
                    window5Bits.push_back(bits);

                    window10SecondsSum += seconds;
                    window10Seconds.push_back(seconds);
                    window10BitsSum += bits;
                    window10Bits.push_back(bits);

                    if (bits > chapterMaxFrameSize * 8)
                    {
                        chapterMaxFrameSize = bits / 8;
                        chapterMaxFrameLocation = chapterPosition;
                    }
                    if (window1SecondsSum > 1.0)
                    {
                        double bitrate = window1BitsSum / window1SecondsSum;
                        if (bitrate > window1PeakBitrate && chapterPosition - window1SecondsSum > 0)
                        {
                            window1PeakBitrate = bitrate;
                            window1PeakLocation = chapterPosition - window1SecondsSum;
                        }
                        window1BitsSum -= dequeue(window1Bits);
                        window1SecondsSum -= dequeue(window1Seconds);
                    }
                    if (window5SecondsSum > 5.0)
                    {
                        double bitrate = window5BitsSum / window5SecondsSum;
                        if (bitrate > window5PeakBitrate && chapterPosition - window5SecondsSum > 0)
                        {
                            window5PeakBitrate = bitrate;
                            window5PeakLocation = chapterPosition - window5SecondsSum;
                            if (window5PeakLocation < 0)
                            {
                                window5PeakLocation = 0;
                            }
                        }
                        window5BitsSum -= dequeue(window5Bits);
                        window5SecondsSum -= dequeue(window5Seconds);
                    }
                    if (window10SecondsSum > 10.0)
                    {
                        double bitrate = window10BitsSum / window10SecondsSum;
                        if (bitrate > window10PeakBitrate && chapterPosition - window10SecondsSum > 0)
                        {
                            window10PeakBitrate = bitrate;
                            window10PeakLocation = chapterPosition - window10SecondsSum;
                        }
                        window10BitsSum -= dequeue(window10Bits);
                        window10SecondsSum -= dequeue(window10Seconds);
                    }
                }
            }
            if (diagList == nullptr || diagIndex == static_cast<int>(diagList->size()))
            {
                if (clipIndex < static_cast<int>(playlist.StreamClips.size()))
                {
                    clipIndex++;
                    diagIndex = 0;
                }
                else
                {
                    chapterPosition = chapterEnd;
                }
            }
            if (chapterPosition >= chapterEnd)
            {
                ++chapterIndex;

                double chapterBitrate = 0;
                if (chapterLength > 0)
                {
                    chapterBitrate = chapterBits / chapterLength;
                }
                double chapterAvgFrameSize = 0;
                if (chapterFrameCount > 0)
                {
                    chapterAvgFrameSize = chapterBits / chapterFrameCount / 8;
                }

                auto kbps = [](double value) { return Pad(net::N(net::Round(value / 1000), 0), 6) + " kbps"; };
                auto bytes = [](double value) { return Pad(net::N(value, 0), 7) + " bytes"; };

                report += chapterRow({std::to_string(chapterIndex), Time(Span(chapterStart)), Time(Span(chapterLength)),
                                      kbps(chapterBitrate), kbps(window1PeakBitrate), Time2(Span(window1PeakLocation)),
                                      kbps(window5PeakBitrate), Time2(Span(window5PeakLocation)),
                                      kbps(window10PeakBitrate), Time2(Span(window10PeakLocation)),
                                      bytes(chapterAvgFrameSize), bytes(chapterMaxFrameSize),
                                      Time2(Span(chapterMaxFrameLocation))});

                window1Bits.clear();
                window1Seconds.clear();
                window1BitsSum = 0;
                window1SecondsSum = 0;
                window1PeakBitrate = 0;
                window1PeakLocation = 0;

                window5Bits.clear();
                window5Seconds.clear();
                window5BitsSum = 0;
                window5SecondsSum = 0;
                window5PeakBitrate = 0;
                window5PeakLocation = 0;

                window10Bits.clear();
                window10Seconds.clear();
                window10BitsSum = 0;
                window10SecondsSum = 0;
                window10PeakBitrate = 0;
                window10PeakLocation = 0;

                chapterBits = 0;
                chapterSeconds = 0;
                chapterFrameCount = 0;
                chapterMaxFrameSize = 0;
                chapterMaxFrameLocation = 0;
            }
        }

        if (BDInfoSettings::GenerateStreamDiagnostics)
        {
            report += "\n";
            report += "STREAM DIAGNOSTICS:\n";
            report += "\n";

            auto diagRow = [](const std::string& a, const std::string& b, const std::string& c, const std::string& d,
                              const std::string& e, const std::string& f, const std::string& g, const std::string& h,
                              const std::string& i) {
                return Pad(a, -16) + Pad(b, -16) + Pad(c, -16) + Pad(d, -16) + Pad(e, -24) + Pad(f, -24) + Pad(g, -24) +
                       Pad(h, -16) + Pad(i, -16) + "\n";
            };
            report += diagRow("File", "PID", "Type", "Codec", "Language", "Seconds", Pad("Bitrate", 11), Pad("Bytes", 10),
                              Pad("Packets", 9));
            report += diagRow("----------", "-------------", "-----", "----------", "-------------", "--------------",
                              "---------------", "--------------", "-----------");

            net::Dictionary<std::string, TSStreamClipPtr> reportedClips;
            for (const TSStreamClipPtr& clipPtr : playlist.StreamClips)
            {
                const TSStreamClip& clip = *clipPtr;
                if (!clip.StreamFile) continue;
                if (reportedClips.ContainsKey(clip.Name)) continue;
                reportedClips.Set(clip.Name, clipPtr);

                std::string clipName = clip.DisplayName();
                if (clip.AngleIndex > 0)
                {
                    clipName += " (" + std::to_string(clip.AngleIndex) + ")";
                }
                for (const auto& entry : clip.StreamFile->Streams)
                {
                    const TSStream& clipStream = *entry.second;
                    if (!playlist.Streams.ContainsKey(clipStream.PID)) continue;

                    const TSStream& playlistStream = *playlist.Streams[clipStream.PID];

                    std::string clipBitRate = Pad(net::N0(static_cast<int64_t>(0)), 7) + " kbps";
                    std::string clipSeconds = "0";

                    if (clip.StreamFile->Length > 0)
                    {
                        clipSeconds = net::F(clip.StreamFile->Length, 3);
                        clipBitRate = Pad(net::N(net::Round(static_cast<double>(clipStream.PayloadBytes) * 8 /
                                                            clip.StreamFile->Length / 1000),
                                                 0),
                                          7) +
                                      " kbps";
                    }
                    std::string language;
                    if (!playlistStream.LanguageCode().empty())
                    {
                        language = playlistStream.LanguageCode() + " (" + playlistStream.LanguageName + ")";
                    }

                    report += diagRow(clipName, std::to_string(clipStream.PID) + " (0x" + net::X(clipStream.PID) + ")",
                                      "0x" + net::X(static_cast<uint8_t>(clipStream.StreamType), 2),
                                      clipStream.CodecShortName(), language, clipSeconds, clipBitRate,
                                      Pad(net::N0(clipStream.PayloadBytes), 14), Pad(net::N0(clipStream.PacketCount), 11));
                }
            }
        }

        report += "\n";
        report += "[/code]\n";
        report += "<---- END FORUMS PASTE ---->\n";
        report += "\n";

        if (BDInfoSettings::GenerateTextSummary)
        {
            report += "QUICK SUMMARY:\n\n";
            report += summary;
            report += "\n";
        }

        output(report);
        report.clear();
    }

    output(report);
}

} // namespace ReportGenerator
} // namespace bdinfo
