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

// TSStreamClip.cs and TSStreamClipFile.cs

#include "bdinfo.h"

namespace bdinfo {

using net::A;

// ---------------------------------------------------------------------------
// TSStreamClip.cs
// ---------------------------------------------------------------------------

TSStreamClip::TSStreamClip(const TSStreamFilePtr& streamFile, const TSStreamClipFilePtr& streamClipFile)
{
    if (streamFile)
    {
        Name = streamFile->Name;
        StreamFile = streamFile;

        FileSize = static_cast<uint64_t>(StreamFile->File->Length);

        if (StreamFile->InterleavedFile)
        {
            InterleavedFileSize = static_cast<uint64_t>(StreamFile->InterleavedFile->File->Length);
        }
    }
    StreamClipFile = streamClipFile;
}

std::string TSStreamClip::DisplayName() const
{
    if (StreamFile && StreamFile->InterleavedFile && BDInfoSettings::EnableSSIF)
    {
        return StreamFile->InterleavedFile->Name;
    }
    return Name;
}

uint64_t TSStreamClip::PacketBitRate() const
{
    if (PacketSeconds > 0)
    {
        return net::ToUInt64(net::Round((PacketSize() * 8.0) / PacketSeconds));
    }
    return 0;
}

// ---------------------------------------------------------------------------
// TSStreamClipFile.cs
// ---------------------------------------------------------------------------

void TSStreamClipFile::Scan()
{
    Streams.Clear();

    std::unique_ptr<InputStream> stream = File->OpenRead();
    int64_t streamLength = stream->Length();

    std::vector<uint8_t> data(static_cast<size_t>(streamLength));
    if (!data.empty()) stream->Read(data.data(), static_cast<int>(data.size()));
    stream.reset();

    if (data.size() < 8)
        throw net::Exception("Source array was not long enough. Check the source index, length, and the array's lower bounds. (Parameter 'sourceArray')");
    std::string fileType;
    for (int i = 0; i < 8; i++) fileType.push_back(data[static_cast<size_t>(i)] < 0x80 ? static_cast<char>(data[static_cast<size_t>(i)]) : '?');
    FileType = fileType;
    if (FileType != "HDMV0100" && FileType != "HDMV0200" && FileType != "HDMV0300")
    {
        if (File->IsDisc()) net::ThrowNullReference(); // the C# message dereferences FileInfo
        throw net::Exception("Clip info file " + File->Name + " has an unknown file type " + FileType + ".");
    }

    int32_t clipIndex = static_cast<int32_t>((static_cast<uint32_t>(A(data, 12)) << 24) +
                                             (static_cast<uint32_t>(A(data, 13)) << 16) +
                                             (static_cast<uint32_t>(A(data, 14)) << 8) + A(data, 15));

    int32_t clipLength = static_cast<int32_t>((static_cast<uint32_t>(A(data, clipIndex)) << 24) +
                                              (static_cast<uint32_t>(A(data, static_cast<int64_t>(clipIndex) + 1)) << 16) +
                                              (static_cast<uint32_t>(A(data, static_cast<int64_t>(clipIndex) + 2)) << 8) +
                                              A(data, static_cast<int64_t>(clipIndex) + 3));

    if (clipLength < 0) throw net::Exception("Arithmetic operation resulted in an overflow.");
    if (static_cast<int64_t>(clipIndex) + 4 + clipLength > static_cast<int64_t>(data.size()))
        throw net::Exception("Source array was not long enough. Check the source index, length, and the array's lower bounds. (Parameter 'sourceArray')");
    std::vector<uint8_t> clipData(data.begin() + clipIndex + 4, data.begin() + clipIndex + 4 + clipLength);

    int streamCount = A(clipData, 8);
    int streamOffset = 10;
    for (int streamIndex = 0; streamIndex < streamCount; streamIndex++)
    {
        TSStreamPtr stream;

        uint16_t PID = static_cast<uint16_t>((A(clipData, streamOffset) << 8) + A(clipData, streamOffset + 1));

        streamOffset += 2;

        TSStreamType streamType = static_cast<TSStreamType>(A(clipData, streamOffset + 1));
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
                TSVideoFormat videoFormat = static_cast<TSVideoFormat>(A(clipData, streamOffset + 2) >> 4);
                TSFrameRate frameRate = static_cast<TSFrameRate>(A(clipData, streamOffset + 2) & 0xF);
                TSAspectRatio aspectRatio = static_cast<TSAspectRatio>(A(clipData, streamOffset + 3) >> 4);

                auto video = std::make_shared<TSVideoStream>();
                video->SetVideoFormat(videoFormat);
                video->AspectRatio = aspectRatio;
                video->SetFrameRate(frameRate);
                stream = video;
            }
            break;

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
                int languagePos = streamOffset + 3;
                std::string languageCode = ToolBox::ReadString(clipData, 3, languagePos);

                TSChannelLayout channelLayout = static_cast<TSChannelLayout>(A(clipData, streamOffset + 2) >> 4);
                TSSampleRate sampleRate = static_cast<TSSampleRate>(A(clipData, streamOffset + 2) & 0xF);

                auto audio = std::make_shared<TSAudioStream>();
                audio->SetLanguageCode(languageCode);
                audio->ChannelLayout = channelLayout;
                audio->SampleRate = TSAudioStream::ConvertSampleRate(sampleRate);
                stream = audio;
            }
            break;

            case TSStreamType::INTERACTIVE_GRAPHICS:
            case TSStreamType::PRESENTATION_GRAPHICS:
            {
                int languagePos = streamOffset + 2;
                std::string languageCode = ToolBox::ReadString(clipData, 3, languagePos);

                auto graphics = std::make_shared<TSGraphicsStream>();
                graphics->SetLanguageCode(languageCode);
                stream = graphics;
            }
            break;

            case TSStreamType::SUBTITLE:
            {
                int languagePos = streamOffset + 3;
                std::string languageCode = ToolBox::ReadString(clipData, 3, languagePos);

                auto text = std::make_shared<TSTextStream>();
                text->SetLanguageCode(languageCode);
                stream = text;
            }
            break;

            default:
                break;
        }

        if (stream)
        {
            stream->PID = PID;
            stream->StreamType = streamType;
            Streams.Add(PID, stream);
        }

        streamOffset += A(clipData, streamOffset) + 1;
    }
    IsValid = true;
}

} // namespace bdinfo
