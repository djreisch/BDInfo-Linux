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

// TSStream.cs and ToolBox.cs

#include "bdinfo.h"

#include <cmath>

namespace bdinfo {

namespace {

const TSAudioStream& AsAudio(const TSStream& stream)
{
    auto* audio = dynamic_cast<const TSAudioStream*>(&stream);
    if (audio == nullptr) throw net::Exception("Specified cast is not valid.");
    return *audio;
}

} // namespace

bool TSStream::IsVideoStream() const
{
    switch (StreamType)
    {
        case TSStreamType::MPEG1_VIDEO:
        case TSStreamType::MPEG2_VIDEO:
        case TSStreamType::AVC_VIDEO:
        case TSStreamType::MVC_VIDEO:
        case TSStreamType::VC1_VIDEO:
        case TSStreamType::HEVC_VIDEO:
            return true;
        default:
            return false;
    }
}

bool TSStream::IsAudioStream() const
{
    switch (StreamType)
    {
        case TSStreamType::MPEG1_AUDIO:
        case TSStreamType::MPEG2_AUDIO:
        case TSStreamType::MPEG2_AAC_AUDIO:
        case TSStreamType::MPEG4_AAC_AUDIO:
        case TSStreamType::LPCM_AUDIO:
        case TSStreamType::AC3_AUDIO:
        case TSStreamType::AC3_PLUS_AUDIO:
        case TSStreamType::AC3_PLUS_SECONDARY_AUDIO:
        case TSStreamType::AC3_TRUE_HD_AUDIO:
        case TSStreamType::DTS_AUDIO:
        case TSStreamType::DTS_HD_AUDIO:
        case TSStreamType::DTS_HD_SECONDARY_AUDIO:
        case TSStreamType::DTS_HD_MASTER_AUDIO:
            return true;
        default:
            return false;
    }
}

bool TSStream::IsGraphicsStream() const
{
    return StreamType == TSStreamType::PRESENTATION_GRAPHICS || StreamType == TSStreamType::INTERACTIVE_GRAPHICS;
}

bool TSStream::IsTextStream() const { return StreamType == TSStreamType::SUBTITLE; }

std::string TSStream::CodecName() const
{
    switch (StreamType)
    {
        case TSStreamType::MPEG1_VIDEO: return "MPEG-1 Video";
        case TSStreamType::MPEG2_VIDEO: return "MPEG-2 Video";
        case TSStreamType::AVC_VIDEO: return "MPEG-4 AVC Video";
        case TSStreamType::MVC_VIDEO: return "MPEG-4 MVC Video";
        case TSStreamType::HEVC_VIDEO: return "MPEG-H HEVC Video";
        case TSStreamType::VC1_VIDEO: return "VC-1 Video";
        case TSStreamType::MPEG1_AUDIO:
        case TSStreamType::MPEG2_AUDIO:
        case TSStreamType::MPEG2_AAC_AUDIO:
        case TSStreamType::MPEG4_AAC_AUDIO:
            return AsAudio(*this).ExtendedData.value_or("");
        case TSStreamType::LPCM_AUDIO: return "LPCM Audio";
        case TSStreamType::AC3_AUDIO:
            return AsAudio(*this).AudioMode == TSAudioMode::Extended ? "Dolby Digital EX Audio" : "Dolby Digital Audio";
        case TSStreamType::AC3_PLUS_AUDIO:
            return AsAudio(*this).HasExtensions ? "Dolby Digital Plus/Atmos Audio" : "Dolby Digital Plus Audio";
        case TSStreamType::AC3_PLUS_SECONDARY_AUDIO: return "Dolby Digital Plus Audio";
        case TSStreamType::AC3_TRUE_HD_AUDIO:
            return AsAudio(*this).HasExtensions ? "Dolby TrueHD/Atmos Audio" : "Dolby TrueHD Audio";
        case TSStreamType::DTS_AUDIO:
            return AsAudio(*this).AudioMode == TSAudioMode::Extended ? "DTS-ES Audio" : "DTS Audio";
        case TSStreamType::DTS_HD_AUDIO:
            return AsAudio(*this).HasExtensions ? "DTS:X High-Res Audio" : "DTS-HD High-Res Audio";
        case TSStreamType::DTS_HD_SECONDARY_AUDIO: return "DTS Express";
        case TSStreamType::DTS_HD_MASTER_AUDIO:
            return AsAudio(*this).HasExtensions ? "DTS:X Master Audio" : "DTS-HD Master Audio";
        case TSStreamType::PRESENTATION_GRAPHICS: return "Presentation Graphics";
        case TSStreamType::INTERACTIVE_GRAPHICS: return "Interactive Graphics";
        case TSStreamType::SUBTITLE: return "Subtitle";
        default: return "UNKNOWN";
    }
}

std::string TSStream::CodecAltName() const
{
    switch (StreamType)
    {
        case TSStreamType::MPEG1_VIDEO: return "MPEG-1";
        case TSStreamType::MPEG2_VIDEO: return "MPEG-2";
        case TSStreamType::AVC_VIDEO: return "AVC";
        case TSStreamType::MVC_VIDEO: return "MVC";
        case TSStreamType::HEVC_VIDEO: return "HEVC";
        case TSStreamType::VC1_VIDEO: return "VC-1";
        case TSStreamType::MPEG1_AUDIO: return "MP1";
        case TSStreamType::MPEG2_AUDIO: return "MP2";
        case TSStreamType::MPEG2_AAC_AUDIO: return "MPEG-2 AAC";
        case TSStreamType::MPEG4_AAC_AUDIO: return "MPEG-4 AAC";
        case TSStreamType::LPCM_AUDIO: return "LPCM";
        case TSStreamType::AC3_AUDIO: return "DD AC3";
        case TSStreamType::AC3_PLUS_AUDIO:
        case TSStreamType::AC3_PLUS_SECONDARY_AUDIO: return "DD AC3+";
        case TSStreamType::AC3_TRUE_HD_AUDIO: return AsAudio(*this).HasExtensions ? "Dolby Atmos" : "Dolby TrueHD";
        case TSStreamType::DTS_AUDIO: return "DTS";
        case TSStreamType::DTS_HD_AUDIO: return AsAudio(*this).HasExtensions ? "DTS:X Hi-Res" : "DTS-HD Hi-Res";
        case TSStreamType::DTS_HD_SECONDARY_AUDIO: return "DTS Express";
        case TSStreamType::DTS_HD_MASTER_AUDIO: return AsAudio(*this).HasExtensions ? "DTS:X Master" : "DTS-HD Master";
        case TSStreamType::PRESENTATION_GRAPHICS: return "PGS";
        case TSStreamType::INTERACTIVE_GRAPHICS: return "IGS";
        case TSStreamType::SUBTITLE: return "SUB";
        default: return "UNKNOWN";
    }
}

std::string TSStream::CodecShortName() const
{
    switch (StreamType)
    {
        case TSStreamType::MPEG1_VIDEO: return "MPEG-1";
        case TSStreamType::MPEG2_VIDEO: return "MPEG-2";
        case TSStreamType::AVC_VIDEO: return "AVC";
        case TSStreamType::MVC_VIDEO: return "MVC";
        case TSStreamType::HEVC_VIDEO: return "HEVC";
        case TSStreamType::VC1_VIDEO: return "VC-1";
        case TSStreamType::MPEG1_AUDIO: return "MP1";
        case TSStreamType::MPEG2_AUDIO: return "MP2";
        case TSStreamType::MPEG2_AAC_AUDIO: return "MPEG-2 AAC";
        case TSStreamType::MPEG4_AAC_AUDIO: return "MPEG-4 AAC";
        case TSStreamType::LPCM_AUDIO: return "LPCM";
        case TSStreamType::AC3_AUDIO: return AsAudio(*this).AudioMode == TSAudioMode::Extended ? "AC3-EX" : "AC3";
        case TSStreamType::AC3_PLUS_AUDIO:
        case TSStreamType::AC3_PLUS_SECONDARY_AUDIO: return "AC3+";
        case TSStreamType::AC3_TRUE_HD_AUDIO: return AsAudio(*this).HasExtensions ? "Atmos" : "TrueHD";
        case TSStreamType::DTS_AUDIO: return AsAudio(*this).AudioMode == TSAudioMode::Extended ? "DTS-ES" : "DTS";
        case TSStreamType::DTS_HD_AUDIO: return AsAudio(*this).HasExtensions ? "DTS:X HR" : "DTS-HD HR";
        case TSStreamType::DTS_HD_SECONDARY_AUDIO: return "DTS Express";
        case TSStreamType::DTS_HD_MASTER_AUDIO: return AsAudio(*this).HasExtensions ? "DTS:X MA" : "DTS-HD MA";
        case TSStreamType::PRESENTATION_GRAPHICS: return "PGS";
        case TSStreamType::INTERACTIVE_GRAPHICS: return "IGS";
        case TSStreamType::SUBTITLE: return "SUB";
        default: return "UNKNOWN";
    }
}

void TSStream::CopyTo(TSStream& stream) const
{
    stream.PID = PID;
    stream.StreamType = StreamType;
    stream.IsVBR = IsVBR;
    stream.BitRate = BitRate;
    stream.IsInitialized = IsInitialized;
    stream.SetLanguageCode(languageCode_);

    if (Descriptors)
    {
        stream.Descriptors = std::make_shared<std::vector<TSDescriptor>>(*Descriptors);
    }
}

// ---------------------------------------------------------------------------

void TSVideoStream::SetVideoFormat(TSVideoFormat value)
{
    videoFormat_ = value;
    switch (value)
    {
        case TSVideoFormat::VIDEOFORMAT_480i: Height = 480; IsInterlaced = true; break;
        case TSVideoFormat::VIDEOFORMAT_480p: Height = 480; IsInterlaced = false; break;
        case TSVideoFormat::VIDEOFORMAT_576i: Height = 576; IsInterlaced = true; break;
        case TSVideoFormat::VIDEOFORMAT_576p: Height = 576; IsInterlaced = false; break;
        case TSVideoFormat::VIDEOFORMAT_720p: Height = 720; IsInterlaced = false; break;
        case TSVideoFormat::VIDEOFORMAT_1080i: Height = 1080; IsInterlaced = true; break;
        case TSVideoFormat::VIDEOFORMAT_1080p: Height = 1080; IsInterlaced = false; break;
        case TSVideoFormat::VIDEOFORMAT_2160p: Height = 2160; IsInterlaced = false; break;
        default: break;
    }
}

void TSVideoStream::SetFrameRate(TSFrameRate value)
{
    frameRate_ = value;
    switch (value)
    {
        case TSFrameRate::FRAMERATE_23_976: FrameRateEnumerator = 24000; FrameRateDenominator = 1001; break;
        case TSFrameRate::FRAMERATE_24: FrameRateEnumerator = 24000; FrameRateDenominator = 1000; break;
        case TSFrameRate::FRAMERATE_25: FrameRateEnumerator = 25000; FrameRateDenominator = 1000; break;
        case TSFrameRate::FRAMERATE_29_97: FrameRateEnumerator = 30000; FrameRateDenominator = 1001; break;
        case TSFrameRate::FRAMERATE_50: FrameRateEnumerator = 50000; FrameRateDenominator = 1000; break;
        case TSFrameRate::FRAMERATE_59_94: FrameRateEnumerator = 60000; FrameRateDenominator = 1001; break;
        default: break;
    }
}

std::string TSVideoStream::Description() const
{
    std::string description;

    if (BaseView.has_value())
    {
        description += *BaseView ? "Right Eye" : "Left Eye";
        description += " / ";
    }

    if (Height > 0)
    {
        description += net::D(Height) + (IsInterlaced ? "i" : "p") + " / ";
    }
    if (FrameRateEnumerator > 0 && FrameRateDenominator > 0)
    {
        if (FrameRateEnumerator % FrameRateDenominator == 0)
        {
            description += net::D(FrameRateEnumerator / FrameRateDenominator) + " fps / ";
        }
        else
        {
            description += net::F(static_cast<double>(FrameRateEnumerator) / FrameRateDenominator, 3) + " fps / ";
        }
    }
    if (AspectRatio == TSAspectRatio::ASPECT_4_3)
    {
        description += "4:3 / ";
    }
    else if (AspectRatio == TSAspectRatio::ASPECT_16_9)
    {
        description += "16:9 / ";
    }
    if (EncodingProfile.has_value())
    {
        description += *EncodingProfile + " / ";
    }
    if (StreamType == TSStreamType::HEVC_VIDEO && ExtendedData)
    {
        description += net::Join(" / ", ExtendedData->ExtendedFormatInfo);
    }
    if (net::EndsWith(description, " / "))
    {
        description = description.substr(0, description.size() - 3);
    }
    return description;
}

TSStreamPtr TSVideoStream::Clone() const
{
    auto stream = std::make_shared<TSVideoStream>();
    CopyTo(*stream);

    stream->SetVideoFormat(videoFormat_);
    stream->SetFrameRate(frameRate_);
    stream->Width = Width;
    stream->Height = Height;
    stream->IsInterlaced = IsInterlaced;
    stream->FrameRateEnumerator = FrameRateEnumerator;
    stream->FrameRateDenominator = FrameRateDenominator;
    stream->AspectRatio = AspectRatio;
    stream->EncodingProfile = EncodingProfile;
    stream->ExtendedData = ExtendedData;

    return stream;
}

// ---------------------------------------------------------------------------

int TSAudioStream::ConvertSampleRate(TSSampleRate sampleRate)
{
    switch (sampleRate)
    {
        case TSSampleRate::SAMPLERATE_48: return 48000;
        case TSSampleRate::SAMPLERATE_96:
        case TSSampleRate::SAMPLERATE_48_96: return 96000;
        case TSSampleRate::SAMPLERATE_192:
        case TSSampleRate::SAMPLERATE_48_192: return 192000;
        default: return 0;
    }
}

std::string TSAudioStream::ChannelDescription() const
{
    std::string description;
    if (ChannelCount > 0)
    {
        description += net::D(ChannelCount) + "." + net::D(LFE);
    }
    else
    {
        switch (ChannelLayout)
        {
            case TSChannelLayout::CHANNELLAYOUT_MONO: description += "1.0"; break;
            case TSChannelLayout::CHANNELLAYOUT_STEREO: description += "2.0"; break;
            case TSChannelLayout::CHANNELLAYOUT_MULTI: description += "5.1"; break;
            default: break;
        }
    }
    if (AudioMode == TSAudioMode::Extended)
    {
        if (StreamType == TSStreamType::AC3_AUDIO)
        {
            description += "-EX";
        }
        if (StreamType == TSStreamType::DTS_AUDIO || StreamType == TSStreamType::DTS_HD_AUDIO ||
            StreamType == TSStreamType::DTS_HD_MASTER_AUDIO)
        {
            description += "-ES";
        }
    }
    return description;
}

std::string TSAudioStream::Description() const
{
    std::string description = ChannelDescription();

    if (SampleRate > 0)
    {
        description += " / " + net::D(SampleRate / 1000) + " kHz";
    }
    if (BitRate > 0)
    {
        int64_t coreBitRate = 0;
        if (StreamType == TSStreamType::AC3_TRUE_HD_AUDIO && CoreStream) coreBitRate = CoreStream->BitRate;
        uint32_t kbps = net::ToUInt32(net::Round(static_cast<double>(BitRate - coreBitRate) / 1000));
        description += " / " + net::Pad(net::D(kbps), 5) + " kbps";
    }
    if (BitDepth > 0)
    {
        description += " / " + net::D(BitDepth) + "-bit";
    }
    if (DialNorm != 0)
    {
        description += " / DN " + std::to_string(DialNorm) + "dB";
    }
    if (ChannelCount == 2)
    {
        switch (AudioMode)
        {
            case TSAudioMode::DualMono: description += " / Dual Mono"; break;
            case TSAudioMode::Surround: description += " / Dolby Surround"; break;
            case TSAudioMode::JointStereo: description += " / Joint Stereo"; break;
            default: break;
        }
    }
    if (net::EndsWith(description, " / "))
    {
        description = description.substr(0, description.size() - 3);
    }
    if (CoreStream)
    {
        std::string codec;
        switch (CoreStream->StreamType)
        {
            case TSStreamType::AC3_AUDIO: codec = "AC3 Embedded"; break;
            case TSStreamType::DTS_AUDIO: codec = "DTS Core"; break;
            case TSStreamType::AC3_PLUS_AUDIO: codec = "DD+ Embedded"; break;
            default: break;
        }
        description += " (" + codec + ": " + CoreStream->Description() + ")";
    }
    return description;
}

TSStreamPtr TSAudioStream::Clone() const
{
    auto stream = std::make_shared<TSAudioStream>();
    CopyTo(*stream);

    stream->SampleRate = SampleRate;
    stream->ChannelLayout = ChannelLayout;
    stream->ChannelCount = ChannelCount;
    stream->BitDepth = BitDepth;
    stream->LFE = LFE;
    stream->DialNorm = DialNorm;
    stream->AudioMode = AudioMode;
    stream->ExtendedData = ExtendedData;

    if (CoreStream)
    {
        stream->CoreStream = std::static_pointer_cast<TSAudioStream>(CoreStream->Clone());
    }

    return stream;
}

// ---------------------------------------------------------------------------

TSGraphicsStream::TSGraphicsStream()
{
    IsVBR = true;
    IsInitialized = false;
    CaptionIDs = std::make_shared<net::Dictionary<int, PGSFrame>>();
}

TSStreamPtr TSGraphicsStream::Clone() const
{
    auto stream = std::make_shared<TSGraphicsStream>();
    CopyTo(*stream);
    stream->Width = Width;
    stream->Height = Height;
    stream->Captions = Captions;
    stream->ForcedCaptions = ForcedCaptions;
    stream->CaptionIDs = CaptionIDs;
    stream->LastFrame = LastFrame;
    return stream;
}

std::string TSGraphicsStream::Description() const
{
    std::string description;
    if (Width > 0 || Height > 0)
    {
        description = net::D(Width) + "x" + net::D(Height);
    }
    if (Captions > 0 || ForcedCaptions > 0)
    {
        if (Captions > 0)
        {
            description += " / " + net::D(Captions) + " Caption" + (Captions > 1 ? "s" : "");
        }
        if (ForcedCaptions > 0)
        {
            description += std::string(Captions > 0 ? " ( + " : " / ") + net::D(ForcedCaptions) + " Forced Caption" +
                           (ForcedCaptions > 1 ? "s" : "") + (Captions > 0 ? ")" : "");
        }
    }
    return description;
}

TSTextStream::TSTextStream()
{
    IsVBR = true;
    IsInitialized = true;
}

TSStreamPtr TSTextStream::Clone() const
{
    auto stream = std::make_shared<TSTextStream>();
    CopyTo(*stream);
    return stream;
}

// ---------------------------------------------------------------------------
// ToolBox.cs
// ---------------------------------------------------------------------------

namespace ToolBox {

std::string FormatFileSize(double fSize)
{
    if (fSize <= 0) return "0";
    static const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB"};

    int digitGroups = 0;
    if (BDInfoSettings::MainFormHRSizeFormat)
        digitGroups = net::ToInt32(std::log10(fSize) / std::log10(1024.0));
    if (digitGroups < 0 || digitGroups > 6) net::ThrowIndexOutOfRange();

    return net::N(fSize / std::pow(1024.0, digitGroups), 2) + " " + units[digitGroups];
}

std::string ReadString(const std::vector<uint8_t>& data, int count, int& pos)
{
    if (pos < 0 || count < 0 || static_cast<size_t>(pos) + static_cast<size_t>(count) > data.size())
        throw net::Exception("Index and count must refer to a location within the buffer. (Parameter 'bytes')");
    std::string value;
    for (int i = 0; i < count; i++)
    {
        uint8_t c = data[static_cast<size_t>(pos + i)];
        value.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    pos += count;
    return value;
}

std::string GetSafeFileName(const std::string& fileName)
{
    // Path.GetInvalidFileNameChars() on Linux: '\0' and '/'.
    std::string outFileName;
    for (char c : fileName)
        if (c != '\0' && c != '/') outFileName.push_back(c);
    return outFileName;
}

} // namespace ToolBox

} // namespace bdinfo
