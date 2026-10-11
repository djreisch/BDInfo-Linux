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

// TSPlaylistFile.cs

#include "bdinfo.h"

#include <algorithm>

namespace bdinfo {

using net::A;
using net::L;

namespace {

int32_t ReadInt32(std::vector<uint8_t>& data, int& pos)
{
    int32_t val = static_cast<int32_t>((static_cast<uint32_t>(A(data, pos)) << 24) +
                                       (static_cast<uint32_t>(A(data, pos + 1)) << 16) +
                                       (static_cast<uint32_t>(A(data, pos + 2)) << 8) + A(data, pos + 3));
    pos += 4;
    return val;
}

int32_t ReadInt16(std::vector<uint8_t>& data, int& pos)
{
    int32_t val = (A(data, pos) << 8) + A(data, pos + 1);
    pos += 2;
    return val;
}

uint8_t ReadByte(std::vector<uint8_t>& data, int& pos) { return A(data, pos++); }

int GetStreamTypeSortIndex(TSStreamType streamType)
{
    switch (streamType)
    {
        case TSStreamType::Unknown: return 0;
        case TSStreamType::MPEG1_VIDEO: return 1;
        case TSStreamType::MPEG2_VIDEO: return 2;
        case TSStreamType::AVC_VIDEO: return 3;
        case TSStreamType::VC1_VIDEO: return 4;
        case TSStreamType::MVC_VIDEO: return 5;
        case TSStreamType::HEVC_VIDEO: return 6;

        case TSStreamType::MPEG1_AUDIO: return 1;
        case TSStreamType::MPEG2_AUDIO: return 2;
        case TSStreamType::AC3_PLUS_SECONDARY_AUDIO: return 3;
        case TSStreamType::DTS_HD_SECONDARY_AUDIO: return 4;
        case TSStreamType::AC3_AUDIO: return 5;
        case TSStreamType::DTS_AUDIO: return 6;
        case TSStreamType::AC3_PLUS_AUDIO: return 7;
        case TSStreamType::MPEG2_AAC_AUDIO: return 8;
        case TSStreamType::MPEG4_AAC_AUDIO: return 9;
        case TSStreamType::DTS_HD_AUDIO: return 10;
        case TSStreamType::AC3_TRUE_HD_AUDIO: return 11;
        case TSStreamType::DTS_HD_MASTER_AUDIO: return 12;
        case TSStreamType::LPCM_AUDIO: return 13;

        case TSStreamType::SUBTITLE: return 1;
        case TSStreamType::INTERACTIVE_GRAPHICS: return 2;
        case TSStreamType::PRESENTATION_GRAPHICS: return 3;

        default: return 0;
    }
}

int CompareVideoStreams(const std::shared_ptr<TSVideoStream>& x, const std::shared_ptr<TSVideoStream>& y)
{
    if (!x && !y) return 0;
    if (!x) return 1;
    if (!y) return -1;
    if (x->Height > y->Height) return -1;
    if (y->Height > x->Height) return 1;
    if (x->PID > y->PID) return 1;
    if (y->PID > x->PID) return -1;
    return 0;
}

int CompareAudioStreams(const std::shared_ptr<TSAudioStream>& x, const std::shared_ptr<TSAudioStream>& y)
{
    if (x == y) return 0;
    if (!x && !y) return 0;
    if (!x) return -1;
    if (!y) return 1;
    if (x->ChannelCount > y->ChannelCount) return -1;
    if (y->ChannelCount > x->ChannelCount) return 1;

    int sortX = GetStreamTypeSortIndex(x->StreamType);
    int sortY = GetStreamTypeSortIndex(y->StreamType);
    if (sortX > sortY) return -1;
    if (sortY > sortX) return 1;
    if (x->LanguageCode() == "eng") return -1;
    if (y->LanguageCode() == "eng") return 1;
    if (x->LanguageCode() != y->LanguageCode()) return net::CompareOrdinal(x->LanguageName, y->LanguageName);
    if (x->PID < y->PID) return -1;
    if (y->PID < x->PID) return 1;
    return 0;
}

int CompareTextStreams(const std::shared_ptr<TSTextStream>& x, const std::shared_ptr<TSTextStream>& y)
{
    if (x == y) return 0;
    if (!x && !y) return 0;
    if (!x) return -1;
    if (!y) return 1;
    if (x->LanguageCode() == "eng") return -1;
    if (y->LanguageCode() == "eng") return 1;
    if (x->LanguageCode() == y->LanguageCode())
    {
        if (x->PID > y->PID) return 1;
        if (y->PID > x->PID) return -1;
        return 0;
    }
    return net::CompareOrdinal(x->LanguageName, y->LanguageName);
}

int CompareGraphicsStreams(const std::shared_ptr<TSGraphicsStream>& x, const std::shared_ptr<TSGraphicsStream>& y)
{
    if (x == y) return 0;
    if (!x && !y) return 0;
    if (!x) return -1;
    if (!y) return 1;

    int sortX = GetStreamTypeSortIndex(x->StreamType);
    int sortY = GetStreamTypeSortIndex(y->StreamType);
    if (sortX > sortY) return -1;
    if (sortY > sortX) return 1;
    if (x->LanguageCode() == "eng") return -1;
    if (y->LanguageCode() == "eng") return 1;
    if (x->LanguageCode() == y->LanguageCode())
    {
        if (x->PID > y->PID) return 1;
        if (y->PID > x->PID) return -1;
        return 0;
    }
    return net::CompareOrdinal(x->LanguageName, y->LanguageName);
}

} // namespace

uint64_t TSPlaylistFile::InterleavedFileSize() const
{
    uint64_t size = 0;
    for (const auto& clip : StreamClips) size += clip->InterleavedFileSize;
    return size;
}

uint64_t TSPlaylistFile::FileSize() const
{
    uint64_t size = 0;
    for (const auto& clip : StreamClips) size += clip->FileSize;
    return size;
}

double TSPlaylistFile::TotalLength() const
{
    double length = 0;
    for (const auto& clip : StreamClips)
        if (clip->AngleIndex == 0) length += clip->Length;
    return length;
}

double TSPlaylistFile::TotalAngleLength() const
{
    double length = 0;
    for (const auto& clip : StreamClips) length += clip->Length;
    return length;
}

uint64_t TSPlaylistFile::TotalSize() const
{
    uint64_t size = 0;
    for (const auto& clip : StreamClips)
        if (clip->AngleIndex == 0) size += clip->PacketSize();
    return size;
}

uint64_t TSPlaylistFile::TotalAngleSize() const
{
    uint64_t size = 0;
    for (const auto& clip : StreamClips) size += clip->PacketSize();
    return size;
}

uint64_t TSPlaylistFile::TotalBitRate() const
{
    if (TotalLength() > 0) return net::ToUInt64(net::Round((TotalSize() * 8.0) / TotalLength()));
    return 0;
}

uint64_t TSPlaylistFile::TotalAngleBitRate() const
{
    if (TotalAngleLength() > 0) return net::ToUInt64(net::Round((TotalAngleSize() * 8.0) / TotalAngleLength()));
    return 0;
}

void TSPlaylistFile::Scan(net::Dictionary<std::string, TSStreamFilePtr>& streamFiles,
                          net::Dictionary<std::string, TSStreamClipFilePtr>& streamClipFiles)
{
    Streams.Clear();
    StreamClips.clear();

    std::unique_ptr<InputStream> stream = File->OpenRead();
    std::vector<uint8_t> data(static_cast<size_t>(stream->Length()));
    if (!data.empty()) stream->Read(data.data(), static_cast<int>(data.size()));
    stream.reset();

    // The C# messages below use FileInfo.Name, which is null for disc images.
    auto fileName = [&]() -> std::string {
        if (File->IsDisc()) net::ThrowNullReference();
        return File->Name;
    };

    int pos = 0;

    FileType = ToolBox::ReadString(data, 8, pos);
    if (FileType != "MPLS0100" && FileType != "MPLS0200" && FileType != "MPLS0300")
    {
        throw net::Exception("Playlist " + fileName() + " has an unknown file type " + FileType + ".");
    }

    int playlistOffset = ReadInt32(data, pos);
    int chaptersOffset = ReadInt32(data, pos);
    ReadInt32(data, pos); // extensionsOffset

    // misc flags
    pos = 0x38;
    uint8_t miscFlags = ReadByte(data, pos);

    // MVC_Base_view_R_flag is stored in 4th bit
    MVCBaseViewR = (miscFlags & 0x10) != 0;

    pos = playlistOffset;

    ReadInt32(data, pos); // playlistLength
    ReadInt16(data, pos); // playlistReserved
    int itemCount = ReadInt16(data, pos);
    ReadInt16(data, pos); // subitemCount

    std::vector<TSStreamClipPtr> chapterClips;
    for (int itemIndex = 0; itemIndex < itemCount; itemIndex++)
    {
        int itemStart = pos;
        int itemLength = ReadInt16(data, pos);
        std::string itemName = ToolBox::ReadString(data, 5, pos);
        ToolBox::ReadString(data, 4, pos); // itemType

        TSStreamFilePtr streamFile;
        std::string streamFileName = itemName + ".M2TS";
        if (streamFiles.ContainsKey(streamFileName))
        {
            streamFile = streamFiles[streamFileName];
        }

        TSStreamClipFilePtr streamClipFile;
        std::string streamClipFileName = itemName + ".CLPI";
        if (streamClipFiles.ContainsKey(streamClipFileName))
        {
            streamClipFile = streamClipFiles[streamClipFileName];
        }
        if (!streamClipFile)
        {
            throw net::Exception("Playlist " + fileName() + " referenced missing file " + streamFileName + ".");
        }

        pos += 1;
        int multiangle = (A(data, pos) >> 4) & 0x01;
        pos += 2;

        int32_t inTime = ReadInt32(data, pos);
        if (inTime < 0) inTime &= 0x7FFFFFFF;
        double timeIn = static_cast<double>(inTime) / 45000;

        int32_t outTime = ReadInt32(data, pos);
        if (outTime < 0) outTime &= 0x7FFFFFFF;
        double timeOut = static_cast<double>(outTime) / 45000;

        auto streamClip = std::make_shared<TSStreamClip>(streamFile, streamClipFile);

        streamClip->Name = streamFileName; // TODO
        streamClip->TimeIn = timeIn;
        streamClip->TimeOut = timeOut;
        streamClip->Length = streamClip->TimeOut - streamClip->TimeIn;
        streamClip->RelativeTimeIn = TotalLength();
        streamClip->RelativeTimeOut = streamClip->RelativeTimeIn + streamClip->Length;
        streamClip->RelativeLength = streamClip->Length / TotalLength();
        StreamClips.push_back(streamClip);
        chapterClips.push_back(streamClip);

        pos += 12;
        if (multiangle > 0)
        {
            int angles = A(data, pos);
            pos += 2;
            for (int angle = 0; angle < angles - 1; angle++)
            {
                std::string angleName = ToolBox::ReadString(data, 5, pos);
                ToolBox::ReadString(data, 4, pos); // angleType
                pos += 1;

                TSStreamFilePtr angleFile;
                std::string angleFileName = angleName + ".M2TS";
                if (streamFiles.ContainsKey(angleFileName))
                {
                    angleFile = streamFiles[angleFileName];
                }
                if (!angleFile)
                {
                    throw net::Exception("Playlist " + fileName() + " referenced missing angle file " + angleFileName + ".");
                }

                TSStreamClipFilePtr angleClipFile;
                std::string angleClipFileName = angleName + ".CLPI";
                if (streamClipFiles.ContainsKey(angleClipFileName))
                {
                    angleClipFile = streamClipFiles[angleClipFileName];
                }
                if (!angleClipFile)
                {
                    throw net::Exception("Playlist " + fileName() + " referenced missing angle file " + angleClipFileName +
                                         ".");
                }

                auto angleClip = std::make_shared<TSStreamClip>(angleFile, angleClipFile);
                angleClip->AngleIndex = angle + 1;
                angleClip->TimeIn = streamClip->TimeIn;
                angleClip->TimeOut = streamClip->TimeOut;
                angleClip->RelativeTimeIn = streamClip->RelativeTimeIn;
                angleClip->RelativeTimeOut = streamClip->RelativeTimeOut;
                angleClip->Length = streamClip->Length;
                StreamClips.push_back(angleClip);
            }
            if (angles - 1 > AngleCount) AngleCount = angles - 1;
        }

        ReadInt16(data, pos); // streamInfoLength
        pos += 2;
        int streamCountVideo = A(data, pos++);
        int streamCountAudio = A(data, pos++);
        int streamCountPG = A(data, pos++);
        int streamCountIG = A(data, pos++);
        int streamCountSecondaryAudio = A(data, pos++);
        int streamCountSecondaryVideo = A(data, pos++);
        A(data, pos++); // streamCountPIP
        pos += 5;

        auto addStream = [&](const TSStreamPtr& stream) {
            if (stream)
            {
                if (!PlaylistStreams.ContainsKey(stream->PID) || streamClip->RelativeLength > 0.01)
                    PlaylistStreams.Set(stream->PID, stream);
            }
        };

        for (int i = 0; i < streamCountVideo; i++) addStream(CreatePlaylistStream(data, pos));
        for (int i = 0; i < streamCountAudio; i++) addStream(CreatePlaylistStream(data, pos));
        for (int i = 0; i < streamCountPG; i++) addStream(CreatePlaylistStream(data, pos));
        for (int i = 0; i < streamCountIG; i++) addStream(CreatePlaylistStream(data, pos));
        for (int i = 0; i < streamCountSecondaryAudio; i++)
        {
            addStream(CreatePlaylistStream(data, pos));
            pos += 2;
        }
        for (int i = 0; i < streamCountSecondaryVideo; i++)
        {
            addStream(CreatePlaylistStream(data, pos));
            pos += 6;
        }

        pos += itemLength - (pos - itemStart) + 2;
    }

    pos = chaptersOffset + 4;

    int chapterCount = ReadInt16(data, pos);

    for (int chapterIndex = 0; chapterIndex < chapterCount; chapterIndex++)
    {
        int chapterType = A(data, pos + 1);

        if (chapterType == 1)
        {
            int streamFileIndex = (A(data, pos + 2) << 8) + A(data, pos + 3);

            int64_t chapterTime = (static_cast<int64_t>(A(data, pos + 4)) << 24) +
                                  (static_cast<int64_t>(A(data, pos + 5)) << 16) +
                                  (static_cast<int64_t>(A(data, pos + 6)) << 8) + static_cast<int64_t>(A(data, pos + 7));

            TSStreamClipPtr streamClip = L(chapterClips, streamFileIndex);

            double chapterSeconds = static_cast<double>(chapterTime) / 45000;

            double relativeSeconds = chapterSeconds - streamClip->TimeIn + streamClip->RelativeTimeIn;

            // TODO: Ignore short last chapter?
            if (TotalLength() - relativeSeconds > 1.0)
            {
                streamClip->Chapters.push_back(chapterSeconds);
                Chapters.push_back(relativeSeconds);
            }
        }
        pos += 14;
    }
}

void TSPlaylistFile::Initialize()
{
    LoadStreamClips();

    net::Dictionary<std::string, std::vector<double>> clipTimes;
    for (const auto& clip : StreamClips)
    {
        if (clip->AngleIndex == 0)
        {
            if (clipTimes.ContainsKey(clip->Name))
            {
                std::vector<double>& times = clipTimes[clip->Name];
                if (std::find(times.begin(), times.end(), clip->TimeIn) != times.end())
                {
                    HasLoops = true;
                    break;
                }
                times.push_back(clip->TimeIn);
            }
            else
            {
                clipTimes.Set(clip->Name, std::vector<double>{clip->TimeIn});
            }
        }
    }
    ClearBitrates();
    IsInitialized = true;
}

TSStreamPtr TSPlaylistFile::CreatePlaylistStream(std::vector<uint8_t>& data, int& pos)
{
    TSStreamPtr stream;

    int headerLength = A(data, pos++);
    int headerPos = pos;
    int headerType = A(data, pos++);

    int pid = 0;

    switch (headerType)
    {
        case 1:
            pid = ReadInt16(data, pos);
            break;
        case 2:
            A(data, pos++); // subpathid
            A(data, pos++); // subclipid
            pid = ReadInt16(data, pos);
            break;
        case 3:
            A(data, pos++); // subpathid
            pid = ReadInt16(data, pos);
            break;
        case 4:
            A(data, pos++); // subpathid
            A(data, pos++); // subclipid
            pid = ReadInt16(data, pos);
            break;
        default:
            break;
    }

    pos = headerPos + headerLength;

    int streamLength = A(data, pos++);
    int streamPos = pos;

    TSStreamType streamType = static_cast<TSStreamType>(A(data, pos++));
    switch (streamType)
    {
        case TSStreamType::MVC_VIDEO:
            // TODO
            break;

        case TSStreamType::HEVC_VIDEO:
        case TSStreamType::AVC_VIDEO:
        case TSStreamType::MPEG1_VIDEO:
        case TSStreamType::MPEG2_VIDEO:
        case TSStreamType::VC1_VIDEO:
        {
            TSVideoFormat videoFormat = static_cast<TSVideoFormat>(A(data, pos) >> 4);
            TSFrameRate frameRate = static_cast<TSFrameRate>(A(data, pos) & 0xF);
            TSAspectRatio aspectRatio = static_cast<TSAspectRatio>(A(data, pos + 1) >> 4);

            auto video = std::make_shared<TSVideoStream>();
            video->SetVideoFormat(videoFormat);
            video->AspectRatio = aspectRatio;
            video->SetFrameRate(frameRate);
            stream = video;
            break;
        }

        case TSStreamType::AC3_AUDIO:
        case TSStreamType::AC3_PLUS_AUDIO:
        case TSStreamType::AC3_PLUS_SECONDARY_AUDIO:
        case TSStreamType::AC3_TRUE_HD_AUDIO:
        case TSStreamType::DTS_AUDIO:
        case TSStreamType::DTS_HD_AUDIO:
        case TSStreamType::DTS_HD_MASTER_AUDIO:
        case TSStreamType::DTS_HD_SECONDARY_AUDIO:
        case TSStreamType::LPCM_AUDIO:
        case TSStreamType::MPEG1_AUDIO:
        case TSStreamType::MPEG2_AUDIO:
        case TSStreamType::MPEG2_AAC_AUDIO:
        case TSStreamType::MPEG4_AAC_AUDIO:
        {
            int audioFormat = ReadByte(data, pos);

            TSChannelLayout channelLayout = static_cast<TSChannelLayout>(audioFormat >> 4);
            TSSampleRate sampleRate = static_cast<TSSampleRate>(audioFormat & 0xF);

            std::string audioLanguage = ToolBox::ReadString(data, 3, pos);

            auto audio = std::make_shared<TSAudioStream>();
            audio->ChannelLayout = channelLayout;
            audio->SampleRate = TSAudioStream::ConvertSampleRate(sampleRate);
            audio->SetLanguageCode(audioLanguage);
            stream = audio;
            break;
        }

        case TSStreamType::INTERACTIVE_GRAPHICS:
        case TSStreamType::PRESENTATION_GRAPHICS:
        {
            std::string graphicsLanguage = ToolBox::ReadString(data, 3, pos);

            auto graphics = std::make_shared<TSGraphicsStream>();
            graphics->SetLanguageCode(graphicsLanguage);
            stream = graphics;

            A(data, pos); // `if (data[pos] != 0) { }`
            break;
        }

        case TSStreamType::SUBTITLE:
        {
            ReadByte(data, pos); // code
            std::string textLanguage = ToolBox::ReadString(data, 3, pos);

            auto text = std::make_shared<TSTextStream>();
            text->SetLanguageCode(textLanguage);
            stream = text;
            break;
        }

        default:
            break;
    }

    pos = streamPos + streamLength;

    if (stream)
    {
        stream->PID = static_cast<uint16_t>(pid);
        stream->StreamType = streamType;
    }

    return stream;
}

void TSPlaylistFile::LoadStreamClips()
{
    AngleClips.clear();
    if (AngleCount > 0)
    {
        for (int angleIndex = 0; angleIndex < AngleCount; angleIndex++)
        {
            AngleClips.emplace_back();
        }
    }

    TSStreamClipPtr referenceClip;
    if (!StreamClips.empty())
    {
        referenceClip = StreamClips[0];
    }
    for (const auto& clip : StreamClips)
    {
        if (!referenceClip->StreamFile && clip->StreamFile) referenceClip = clip;

        if (clip->StreamClipFile->Streams.Count() > referenceClip->StreamClipFile->Streams.Count() &&
            clip->RelativeLength > 0.01)
        {
            referenceClip = clip;
        }
        else if (clip->Length > referenceClip->Length && clip->StreamFile)
        {
            referenceClip = clip;
        }
        if (AngleCount > 0)
        {
            if (clip->AngleIndex == 0)
            {
                for (int angleIndex = 0; angleIndex < AngleCount; angleIndex++)
                {
                    L(AngleClips, angleIndex).Set(clip->RelativeTimeIn, clip);
                }
            }
            else
            {
                L(AngleClips, clip->AngleIndex - 1).Set(clip->RelativeTimeIn, clip);
            }
        }
    }

    if (!referenceClip) return;

    for (const auto& entry : referenceClip->StreamClipFile->Streams)
    {
        const TSStreamPtr& clipStream = entry.second;
        if (!Streams.ContainsKey(clipStream->PID))
        {
            TSStreamPtr stream = clipStream->Clone();
            Streams.Set(clipStream->PID, stream);

            if (!IsCustom && !PlaylistStreams.ContainsKey(stream->PID))
            {
                stream->IsHidden = true;
                HasHiddenTracks = true;
            }

            if (stream->IsVideoStream())
            {
                VideoStreams.push_back(std::static_pointer_cast<TSVideoStream>(stream));
            }
            else if (stream->IsAudioStream())
            {
                AudioStreams.push_back(std::static_pointer_cast<TSAudioStream>(stream));
            }
            else if (stream->IsGraphicsStream())
            {
                GraphicsStreams.push_back(std::static_pointer_cast<TSGraphicsStream>(stream));
            }
            else if (stream->IsTextStream())
            {
                TextStreams.push_back(std::static_pointer_cast<TSTextStream>(stream));
            }
        }
    }

    if (referenceClip->StreamFile)
    {
        // TODO: Better way to add this in?
        if (BDInfoSettings::EnableSSIF && referenceClip->StreamFile->InterleavedFile &&
            referenceClip->StreamFile->Streams.ContainsKey(4114) && !Streams.ContainsKey(4114))
        {
            TSStreamPtr stream = referenceClip->StreamFile->Streams[4114]->Clone();
            Streams.Set(4114, stream);
            if (stream->IsVideoStream())
            {
                VideoStreams.push_back(std::static_pointer_cast<TSVideoStream>(stream));
            }
        }

        for (const auto& entry : referenceClip->StreamFile->Streams)
        {
            const TSStreamPtr& clipStream = entry.second;
            if (Streams.ContainsKey(clipStream->PID))
            {
                TSStreamPtr stream = Streams[clipStream->PID];

                if (stream->StreamType != clipStream->StreamType) continue;

                if (clipStream->BitRate > stream->BitRate)
                {
                    stream->BitRate = clipStream->BitRate;
                }
                stream->IsVBR = clipStream->IsVBR;

                if (stream->IsVideoStream() && clipStream->IsVideoStream())
                {
                    auto& video = static_cast<TSVideoStream&>(*stream);
                    auto& clipVideo = static_cast<TSVideoStream&>(*clipStream);
                    video.EncodingProfile = clipVideo.EncodingProfile;
                    video.ExtendedData = clipVideo.ExtendedData;
                }
                else if (stream->IsAudioStream() && clipStream->IsAudioStream())
                {
                    auto& audioStream = static_cast<TSAudioStream&>(*stream);
                    auto& clipAudioStream = static_cast<TSAudioStream&>(*clipStream);

                    if (clipAudioStream.ChannelCount > audioStream.ChannelCount)
                    {
                        audioStream.ChannelCount = clipAudioStream.ChannelCount;
                    }
                    if (clipAudioStream.LFE > audioStream.LFE)
                    {
                        audioStream.LFE = clipAudioStream.LFE;
                    }
                    if (clipAudioStream.SampleRate > audioStream.SampleRate)
                    {
                        audioStream.SampleRate = clipAudioStream.SampleRate;
                    }
                    if (clipAudioStream.BitDepth > audioStream.BitDepth)
                    {
                        audioStream.BitDepth = clipAudioStream.BitDepth;
                    }
                    if (clipAudioStream.DialNorm < audioStream.DialNorm)
                    {
                        audioStream.DialNorm = clipAudioStream.DialNorm;
                    }
                    if (clipAudioStream.AudioMode != TSAudioMode::Unknown)
                    {
                        audioStream.AudioMode = clipAudioStream.AudioMode;
                    }
                    if (clipAudioStream.HasExtensions != audioStream.HasExtensions)
                    {
                        audioStream.HasExtensions = clipAudioStream.HasExtensions;
                    }
                    audioStream.ExtendedData = clipAudioStream.ExtendedData;
                    if (clipAudioStream.CoreStream && !audioStream.CoreStream)
                    {
                        audioStream.CoreStream =
                            std::static_pointer_cast<TSAudioStream>(clipAudioStream.CoreStream->Clone());
                    }
                }
                else if (stream->IsGraphicsStream() && clipStream->IsGraphicsStream())
                {
                    auto& graphicsStream = static_cast<TSGraphicsStream&>(*stream);
                    auto& clipGraphicsStream = static_cast<TSGraphicsStream&>(*clipStream);

                    graphicsStream.Captions = clipGraphicsStream.Captions;
                    graphicsStream.ForcedCaptions = clipGraphicsStream.ForcedCaptions;
                    graphicsStream.Width = clipGraphicsStream.Width;
                    graphicsStream.Height = clipGraphicsStream.Height;
                    graphicsStream.CaptionIDs = clipGraphicsStream.CaptionIDs;
                }
            }
        }
    }

    for (int i = 0; i < AngleCount; i++)
    {
        AngleStreams.emplace_back();
    }

    if (!BDInfoSettings::KeepStreamOrder)
    {
        net::Sort(VideoStreams, CompareVideoStreams);
    }
    for (const auto& stream : VideoStreams)
    {
        SortedStreams.push_back(stream);
        for (int i = 0; i < AngleCount; i++)
        {
            TSStreamPtr angleStream = stream->Clone();
            angleStream->AngleIndex = i + 1;
            AngleStreams[static_cast<size_t>(i)].Set(angleStream->PID, angleStream);
            SortedStreams.push_back(angleStream);
        }
    }

    if (!BDInfoSettings::KeepStreamOrder)
    {
        net::Sort(AudioStreams, CompareAudioStreams);
    }
    for (const auto& stream : AudioStreams)
    {
        SortedStreams.push_back(stream);
    }

    if (!BDInfoSettings::KeepStreamOrder)
    {
        net::Sort(GraphicsStreams, CompareGraphicsStreams);
    }
    for (const auto& stream : GraphicsStreams)
    {
        SortedStreams.push_back(stream);
    }

    if (!BDInfoSettings::KeepStreamOrder)
    {
        net::Sort(TextStreams, CompareTextStreams);
    }
    for (const auto& stream : TextStreams)
    {
        SortedStreams.push_back(stream);
    }
}

void TSPlaylistFile::ClearBitrates()
{
    for (const auto& clip : StreamClips)
    {
        clip->PayloadBytes = 0;
        clip->PacketCount = 0;
        clip->PacketSeconds = 0;

        if (clip->StreamFile)
        {
            for (auto& entry : clip->StreamFile->Streams)
            {
                entry.second->PayloadBytes = 0;
                entry.second->PacketCount = 0;
                entry.second->PacketSeconds = 0;
            }

            clip->StreamFile->StreamDiagnostics.Clear();
        }
    }

    for (const auto& stream : SortedStreams)
    {
        stream->PayloadBytes = 0;
        stream->PacketCount = 0;
        stream->PacketSeconds = 0;
    }
}

bool TSPlaylistFile::IsValid() const
{
    if (!IsInitialized) return false;

    if (BDInfoSettings::FilterShortPlaylists && TotalLength() < BDInfoSettings::FilterShortPlaylistsValue)
    {
        return false;
    }

    if (HasLoops && BDInfoSettings::FilterLoopingPlaylists)
    {
        return false;
    }

    return true;
}

} // namespace bdinfo
