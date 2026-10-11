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

// TSCodecAAC/AC3/AVC/DTS/DTSHD/LPCM/MPA/MPEG2/MVC/PGS/TrueHD/VC1.cs
// (Release build: the DEBUG-only branches of the C# code are not compiled).

#include "bdinfo.h"

#include <cmath>

namespace bdinfo {

// ---------------------------------------------------------------------------
// TSCodecAAC.cs
// ---------------------------------------------------------------------------

namespace TSCodecAAC {

static const char* AACID[] = {"MPEG-4", "MPEG-2"};

static std::string GetAACProfile(int profileType)
{
    switch (profileType)
    {
        case 0: return "AAC Main";
        case 1: return "AAC LC";
        case 2: return "AAC SSR";
        case 3: return "AAC LTP";
        case 16: return "ER AAC LC";
        case 18: return "ER AAC LTP";
        case 36: return "SLS";
        default: return "";
    }
}

static const int AACSampleRates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
                                     16000, 12000, 11025, 8000,  7350,  0,     0,     57600,
                                     51200, 40000, 38400, 34150, 28800, 25600, 20000, 19200,
                                     17075, 14400, 12800, 9600,  0,     0,     0};

static const int AACChannelsSize = 8;
static const int AACChannels[] = {0, 1, 2, 3, 4, 5, 6, 8};
static const TSAudioMode AACChannelModes[] = {
    TSAudioMode::Unknown,  TSAudioMode::Mono,     TSAudioMode::Stereo,   TSAudioMode::Extended,
    TSAudioMode::Surround, TSAudioMode::Surround, TSAudioMode::Surround, TSAudioMode::Surround,
};

void Scan(TSAudioStream& stream, TSStreamBuffer& buffer, NString& /*tag*/)
{
    if (stream.IsInitialized) return;

    int syncWord = buffer.ReadBits2(12);
    if (syncWord != 0b111111111111) return;

    // fixed header
    int audioVersionID = buffer.ReadBits2(1);
    buffer.ReadBits2(2); // layerIndex
    buffer.ReadBool();   // protectionAbsent
    int profileObjectType = buffer.ReadBits2(2);
    int samplingRateIndex = buffer.ReadBits2(4);
    buffer.ReadBool(); // privateBit
    int channelMode = buffer.ReadBits2(3);
    buffer.ReadBool(); // originalBit
    buffer.ReadBool(); // home

    if (samplingRateIndex <= 13)
        stream.SampleRate = AACSampleRates[samplingRateIndex];
    else
        stream.SampleRate = 0;

    if (channelMode <= AACChannelsSize)
    {
        if (channelMode >= 8) net::ThrowIndexOutOfRange();
        stream.AudioMode = AACChannelModes[channelMode];
        stream.ChannelCount = AACChannels[channelMode];
    }
    else
    {
        stream.ChannelCount = 0;
        stream.AudioMode = TSAudioMode::Unknown;
    }

    if (channelMode >= 7 && channelMode <= 8)
    {
        stream.ChannelCount--;
        stream.LFE = 1;
    }
    else
        stream.LFE = 0;

    stream.ExtendedData = std::string(AACID[audioVersionID]) + " " + GetAACProfile(profileObjectType);

    stream.IsVBR = true;
    stream.IsInitialized = true;
}

} // namespace TSCodecAAC

// ---------------------------------------------------------------------------
// TSCodecAC3.cs
// ---------------------------------------------------------------------------

namespace TSCodecAC3 {

static const int AC3Bitrate[] = {32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 448, 512, 576, 640};
static const uint8_t AC3Channels[] = {2, 1, 2, 3, 3, 4, 4, 5};

static uint8_t AC3ChanMap(int chanMap)
{
    uint8_t channels = 0;
    for (uint8_t i = 0; i < 16; i++)
    {
        if ((chanMap & (1 << (15 - i))) != 0)
        {
            switch (i)
            {
                case 5:
                case 6:
                case 9:
                case 10:
                case 11:
                    channels = static_cast<uint8_t>(channels + 2);
                    break;
            }
        }
    }
    return channels;
}

static void EmdfPayloadConfig(TSStreamBuffer& buffer)
{
    bool sampleOffsetE = buffer.ReadBool();
    if (sampleOffsetE) buffer.BSSkipBits(12);

    if (buffer.ReadBool()) // duratione
        buffer.BSSkipBits(11); // duration

    if (buffer.ReadBool()) // groupide
        buffer.BSSkipBits(2); // groupid

    if (buffer.ReadBool()) buffer.BSSkipBits(8); // reserved

    if (!buffer.ReadBool()) // discard_unknown_payload
    {
        buffer.BSSkipBits(1);

        if (!sampleOffsetE)
        {
            if (buffer.ReadBool()) // payload_frame_aligned
                buffer.BSSkipBits(9);
        }
    }
}

void Scan(TSAudioStream& stream, TSStreamBuffer& buffer, NString& /*tag*/)
{
    if (stream.IsInitialized) return;

    std::vector<uint8_t> sync;
    if (!buffer.ReadBytes(2, sync) || sync[0] != 0x0B || sync[1] != 0x77)
    {
        return;
    }

    bool secondFrame = stream.ChannelCount > 0;

    uint32_t srCode;
    uint32_t frameSize = 0;
    uint32_t frameSizeCode = 0;
    uint32_t channelMode;
    uint32_t lfeOn;
    uint32_t dialNorm = 0;
    uint32_t dialNormExt = 0;
    uint32_t numBlocks = 0;

    std::vector<uint8_t> hdr;
    if (!buffer.ReadBytes(4, hdr)) net::ThrowNullReference();
    uint32_t bsid = static_cast<uint32_t>((hdr[3] & 0xF8) >> 3);
    buffer.Seek(-4, true);
    if (bsid <= 10)
    {
        buffer.BSSkipBytes(2);
        srCode = buffer.ReadBits2(2);
        frameSizeCode = buffer.ReadBits2(6);
        bsid = buffer.ReadBits2(5);
        buffer.BSSkipBits(3);

        channelMode = buffer.ReadBits2(3);
        if (((channelMode & 0x1) > 0) && (channelMode != 0x1))
        {
            buffer.BSSkipBits(2);
        }
        if ((channelMode & 0x4) > 0)
        {
            buffer.BSSkipBits(2);
        }
        if (channelMode == 0x2)
        {
            uint16_t dsurmod = buffer.ReadBits2(2);
            if (dsurmod == 0x2)
            {
                stream.AudioMode = TSAudioMode::Surround;
            }
        }
        lfeOn = buffer.ReadBits2(1);
        dialNorm = buffer.ReadBits2(5);
        if (buffer.ReadBool()) buffer.BSSkipBits(8);
        if (buffer.ReadBool()) buffer.BSSkipBits(8);
        if (buffer.ReadBool()) buffer.BSSkipBits(7);
        if (channelMode == 0)
        {
            buffer.BSSkipBits(5);
            if (buffer.ReadBool()) buffer.BSSkipBits(8);
            if (buffer.ReadBool()) buffer.BSSkipBits(8);
            if (buffer.ReadBool()) buffer.BSSkipBits(7);
        }
        buffer.BSSkipBits(2);
        if (bsid == 6)
        {
            if (buffer.ReadBool())
            {
                buffer.BSSkipBits(14);
            }
            if (buffer.ReadBool())
            {
                uint32_t dsurexmod = buffer.ReadBits2(2);
                buffer.ReadBits2(2); // dheadphonmod
                buffer.BSSkipBits(10);
                if (dsurexmod == 2)
                {
                    stream.AudioMode = TSAudioMode::Extended;
                }
            }
        }
    }
    else
    {
        uint32_t frameType = buffer.ReadBits2(2);
        buffer.BSSkipBits(3);

        frameSize = (buffer.ReadBits4(11) + 1) << 1;

        srCode = buffer.ReadBits2(2);
        if (srCode == 3)
        {
            srCode = buffer.ReadBits2(2);
            numBlocks = 3;
        }
        else
        {
            numBlocks = buffer.ReadBits2(2);
        }
        channelMode = buffer.ReadBits2(3);
        lfeOn = buffer.ReadBits2(1);
        bsid = buffer.ReadBits2(5);
        dialNormExt = buffer.ReadBits2(5);

        if (buffer.ReadBool())
        {
            buffer.BSSkipBits(8);
        }
        if (channelMode == 0) // 1+1
        {
            buffer.BSSkipBits(5);
            if (buffer.ReadBool())
            {
                buffer.BSSkipBits(8);
            }
        }
        if (frameType == 1) // dependent stream
        {
            stream.CoreStream = std::static_pointer_cast<TSAudioStream>(stream.Clone());
            stream.CoreStream->StreamType = TSStreamType::AC3_AUDIO;

            if (buffer.ReadBool()) // channel remapping
            {
                uint32_t chanmap = buffer.ReadBits4(16);

                stream.ChannelCount = stream.CoreStream->ChannelCount;
                stream.ChannelCount += AC3ChanMap(static_cast<int>(chanmap));
                lfeOn = static_cast<uint32_t>(stream.CoreStream->LFE);
            }
        }

        bool emdfFound = false;

        do
        {
            uint32_t emdfSync = buffer.ReadBits4(16);
            if (emdfSync == 0x5838)
            {
                emdfFound = true;
                break;
            }
            buffer.Seek(-2, true);
            buffer.BSSkipBits(1); // skip 1 bit
        } while (buffer.Position() < buffer.Length());

        if (emdfFound)
        {
            uint32_t emdfContainerSize = buffer.ReadBits4(16);
            int64_t remainAfterEmdf = buffer.DataBitStreamRemain() - static_cast<uint32_t>(emdfContainerSize * 8);

            uint32_t emdfVersion = buffer.ReadBits2(2); // emdf_version
            if (emdfVersion == 3) emdfVersion += buffer.ReadBits2(2);

            if (emdfVersion > 0)
            {
                buffer.BSSkipBits(static_cast<int>(buffer.DataBitStreamRemain() - remainAfterEmdf));
            }
            else
            {
                uint16_t temp = buffer.ReadBits2(3);
                if (temp == 0x7) buffer.BSSkipBits(2); // skip 3 bits

                uint16_t emdfPayloadID = buffer.ReadBits2(5);

                if (emdfPayloadID > 0 && emdfPayloadID < 16)
                {
                    if (emdfPayloadID == 0x1F) buffer.BSSkipBits(5); // skip 5 bits

                    EmdfPayloadConfig(buffer);

                    int emdfPayloadSize = buffer.ReadBits2(8) * 8;
                    buffer.BSSkipBits(emdfPayloadSize + 1);
                }

                while ((emdfPayloadID = buffer.ReadBits2(5)) != 14 && buffer.Position() < buffer.Length())
                {
                    if (emdfPayloadID == 0x1F) buffer.BSSkipBits(5); // skip 5 bits

                    EmdfPayloadConfig(buffer);

                    int emdfPayloadSize = buffer.ReadBits2(8) * 8;
                    buffer.ReadBits4(emdfPayloadSize + 1);
                }

                if (buffer.Position() < buffer.Length() && emdfPayloadID == 14)
                {
                    EmdfPayloadConfig(buffer);

                    buffer.BSSkipBits(12);

                    uint32_t jocNumObjectsBits = buffer.ReadBits2(6);

                    if (jocNumObjectsBits > 0) stream.HasExtensions = true;
                }
            }
        }
    }

    if ((channelMode < 8) && stream.ChannelCount == 0) stream.ChannelCount = AC3Channels[channelMode];

    if (stream.AudioMode == TSAudioMode::Unknown)
    {
        switch (channelMode)
        {
            case 0: // 1+1
                stream.AudioMode = TSAudioMode::DualMono;
                break;
            case 2: // 2/0
                stream.AudioMode = TSAudioMode::Stereo;
                break;
            default:
                stream.AudioMode = TSAudioMode::Unknown;
                break;
        }
    }

    switch (srCode)
    {
        case 0: stream.SampleRate = 48000; break;
        case 1: stream.SampleRate = 44100; break;
        case 2: stream.SampleRate = 32000; break;
        default: stream.SampleRate = 0; break;
    }

    if (bsid <= 10)
    {
        uint32_t fSize = frameSizeCode >> 1;
        if (fSize < 19) stream.BitRate = AC3Bitrate[fSize] * 1000;
    }
    else
    {
        stream.BitRate = net::ToInt64(4.0 * frameSize * stream.SampleRate / static_cast<double>(numBlocks * 256));
        if (stream.CoreStream) stream.BitRate += stream.CoreStream->BitRate;
    }

    stream.LFE = static_cast<int>(lfeOn);
    if (stream.StreamType != TSStreamType::AC3_PLUS_SECONDARY_AUDIO)
    {
        if ((stream.StreamType == TSStreamType::AC3_PLUS_AUDIO && bsid == 6) ||
            (stream.StreamType == TSStreamType::AC3_AUDIO))
            stream.DialNorm = static_cast<int>(dialNorm) * -1;
        else if (stream.StreamType == TSStreamType::AC3_PLUS_AUDIO && secondFrame)
        {
            stream.DialNorm = static_cast<int>(dialNormExt) * -1;
        }
    }
    stream.IsVBR = false;
    if (stream.StreamType == TSStreamType::AC3_PLUS_AUDIO && bsid == 6 && !secondFrame)
        stream.IsInitialized = false;
    else
        stream.IsInitialized = true;
}

} // namespace TSCodecAC3

// ---------------------------------------------------------------------------
// TSCodecAVC.cs
// ---------------------------------------------------------------------------

namespace TSCodecAVC {

void Scan(TSVideoStream& stream, TSStreamBuffer& buffer, NString& tag)
{
    uint32_t parse = 0;
    uint8_t accessUnitDelimiterParse = 0;
    uint8_t sequenceParameterSetParse = 0;
    NString profile;
    std::string level;
    uint8_t constraintSet3Flag = 0;

    for (int i = 0; i < buffer.Length(); i++)
    {
        parse = (parse << 8) + buffer.ReadByte(true);

        if (parse == 0x00000109)
        {
            accessUnitDelimiterParse = 1;
        }
        else if (accessUnitDelimiterParse > 0)
        {
            --accessUnitDelimiterParse;
            if (accessUnitDelimiterParse == 0)
            {
                switch ((parse & 0xFF) >> 5)
                {
                    case 0: // I
                    case 3: // SI
                    case 5: // I, SI
                        tag = "I";
                        break;

                    case 1: // I, P
                    case 4: // SI, SP
                    case 6: // I, SI, P, SP
                        tag = "P";
                        break;

                    case 2: // I, P, B
                    case 7: // I, SI, P, SP, B
                        tag = "B";
                        break;
                }
                if (stream.IsInitialized) return;
            }
        }
        else if (parse == 0x00000127 || parse == 0x00000167)
        {
            sequenceParameterSetParse = 3;
        }
        else if (sequenceParameterSetParse > 0)
        {
            --sequenceParameterSetParse;
            if (!stream.IsInitialized)
            {
                switch (sequenceParameterSetParse)
                {
                    case 2:
                        switch (parse & 0xFF)
                        {
                            case 66: profile = "Baseline Profile"; break;
                            case 77: profile = "Main Profile"; break;
                            case 88: profile = "Extended Profile"; break;
                            case 100: profile = "High Profile"; break;
                            case 110: profile = "High 10 Profile"; break;
                            case 122: profile = "High 4:2:2 Profile"; break;
                            case 144: profile = "High 4:4:4 Profile"; break;
                            default: profile = "Unknown Profile"; break;
                        }
                        break;

                    case 1:
                        constraintSet3Flag = static_cast<uint8_t>((parse & 0x10) >> 4);
                        break;

                    case 0:
                    {
                        uint8_t b = static_cast<uint8_t>(parse & 0xFF);
                        if (b == 11 && constraintSet3Flag == 1)
                        {
                            level = "1b";
                        }
                        else
                        {
                            level = net::D(b / 10) + "." + net::D(b - ((b / 10) * 10));
                        }
                        stream.EncodingProfile = profile.value_or("") + " " + level;
                        stream.IsVBR = true;
                        stream.IsInitialized = true;
                        break;
                    }
                }
            }
        }
    }
}

} // namespace TSCodecAVC

// ---------------------------------------------------------------------------
// TSCodecDTS.cs
// ---------------------------------------------------------------------------

namespace TSCodecDTS {

static const int DcaSampleRates[] = {0, 8000, 16000, 32000, 0, 0, 11025, 22050, 44100, 0, 0, 12000, 24000, 48000, 96000, 192000};

static const int DcaBitRates[] = {32000,   56000,   64000,   96000,   112000,  128000,  192000,  224000,
                                  256000,  320000,  384000,  448000,  512000,  576000,  640000,  768000,
                                  896000,  1024000, 1152000, 1280000, 1344000, 1408000, 1411200, 1472000,
                                  1509000, 1920000, 2048000, 3072000, 3840000, 1 /*open*/, 2 /*variable*/, 3 /*lossless*/};

static const int DcaBitsPerSample[] = {16, 16, 20, 20, 0, 24, 24};

void Scan(TSAudioStream& stream, TSStreamBuffer& buffer, int64_t bitrate, NString& /*tag*/)
{
    if (stream.IsInitialized) return;

    bool syncFound = false;
    uint32_t sync = 0;
    for (int i = 0; i < buffer.Length(); i++)
    {
        sync = (sync << 8) + buffer.ReadByte();
        if (sync == 0x7FFE8001)
        {
            syncFound = true;
            break;
        }
    }
    if (!syncFound) return;

    buffer.BSSkipBits(6);
    uint32_t crcPresent = buffer.ReadBits4(1);
    buffer.BSSkipBits(7);
    uint32_t frameSize = buffer.ReadBits4(14);
    if (frameSize < 95)
    {
        return;
    }
    buffer.BSSkipBits(6);
    uint32_t sampleRate = buffer.ReadBits4(4);
    if (sampleRate >= 16)
    {
        return;
    }
    uint32_t bitRate = buffer.ReadBits4(5);
    if (bitRate >= 32)
    {
        return;
    }
    buffer.BSSkipBits(8);
    uint32_t extCoding = buffer.ReadBits4(1);
    buffer.BSSkipBits(1);
    uint32_t lfe = buffer.ReadBits4(2);
    buffer.BSSkipBits(1);
    if (crcPresent == 1)
    {
        buffer.BSSkipBits(16);
    }
    buffer.BSSkipBits(7);
    uint32_t sourcePcmRes = buffer.ReadBits4(3);
    buffer.BSSkipBits(2);
    uint32_t dialogNorm = buffer.ReadBits4(4);
    if (sourcePcmRes >= 7)
    {
        return;
    }
    buffer.BSSkipBits(4);
    uint32_t totalChannels = buffer.ReadBits4(3) + 1 + extCoding;

    stream.SampleRate = DcaSampleRates[sampleRate];
    stream.ChannelCount = static_cast<int>(totalChannels);
    stream.LFE = (lfe > 0 ? 1 : 0);
    stream.BitDepth = DcaBitsPerSample[sourcePcmRes];
    stream.DialNorm = static_cast<int>(-static_cast<int64_t>(dialogNorm));
    if ((sourcePcmRes & 0x1) == 0x1)
    {
        stream.AudioMode = TSAudioMode::Extended;
    }

    stream.BitRate = static_cast<uint32_t>(DcaBitRates[bitRate]);
    switch (stream.BitRate)
    {
        case 1:
            if (bitrate > 0)
            {
                stream.BitRate = bitrate;
                stream.IsVBR = false;
                stream.IsInitialized = true;
            }
            else
            {
                stream.BitRate = 0;
            }
            break;

        case 2:
        case 3:
            stream.IsVBR = true;
            stream.IsInitialized = true;
            break;

        default:
            stream.IsVBR = false;
            stream.IsInitialized = true;
            break;
    }
}

} // namespace TSCodecDTS

// ---------------------------------------------------------------------------
// TSCodecDTSHD.cs
// ---------------------------------------------------------------------------

namespace TSCodecDTSHD {

static const int SampleRates[] = {0x1F40, 0x3E80, 0x7D00,  0x0FA00, 0x1F400, 0x5622,  0x0AC44, 0x15888,
                                  0x2B110, 0x56220, 0x2EE0, 0x5DC0,  0x0BB80, 0x17700, 0x2EE00, 0x5DC00};

void Scan(TSAudioStream& stream, TSStreamBuffer& buffer, int64_t bitrate, NString& tag)
{
    if (stream.IsInitialized &&
        (stream.StreamType == TSStreamType::DTS_HD_SECONDARY_AUDIO ||
         (stream.CoreStream && stream.CoreStream->IsInitialized)))
        return;

    bool syncFound = false;
    uint32_t sync = 0;
    for (int i = 0; i < buffer.Length(); i++)
    {
        sync = (sync << 8) + buffer.ReadByte();
        if (sync == 0x64582025)
        {
            syncFound = true;
            break;
        }
    }

    if (!syncFound)
    {
        tag = "CORE";
        if (!stream.CoreStream)
        {
            stream.CoreStream = std::make_shared<TSAudioStream>();
            stream.CoreStream->StreamType = TSStreamType::DTS_AUDIO;
        }
        if (!stream.CoreStream->IsInitialized)
        {
            buffer.BeginRead();
            TSCodecDTS::Scan(*stream.CoreStream, buffer, bitrate, tag);
        }
        return;
    }

    tag = "HD";
    buffer.BSSkipBits(8);
    uint32_t nuSubStreamIndex = buffer.ReadBits4(2);
    bool bBlownUpHeader = buffer.ReadBool();

    buffer.BSSkipBits(bBlownUpHeader ? 32 : 24);

    int nuNumAssets = 1;
    bool bStaticFieldsPresent = buffer.ReadBool();
    if (bStaticFieldsPresent)
    {
        buffer.BSSkipBits(5);

        if (buffer.ReadBool())
        {
            buffer.BSSkipBits(36);
        }
        int nuNumAudioPresent = buffer.ReadBits2(3) + 1;
        nuNumAssets = buffer.ReadBits2(3) + 1;
        for (int i = 0; i < nuNumAudioPresent; i++)
        {
            buffer.ReadBits4(static_cast<int>(nuSubStreamIndex + 1)); // nuActiveExSsMask[i]
        }
        for (int i = 0; i < nuNumAudioPresent; i++)
        {
            for (int64_t j = 0; j < static_cast<int64_t>(nuSubStreamIndex) + 1; j++)
            {
                if (((j + 1) % 2) == 1)
                {
                    buffer.BSSkipBits(8);
                }
            }
        }
        if (buffer.ReadBool())
        {
            buffer.BSSkipBits(2);
            int nuBits4MixOutMask = buffer.ReadBits2(2) * 4 + 4;
            int nuNumMixOutConfigs = buffer.ReadBits2(2) + 1;
            for (int i = 0; i < nuNumMixOutConfigs; i++)
            {
                buffer.ReadBits4(nuBits4MixOutMask); // nuMixOutChMask[i]
            }
        }
    }
    for (int i = 0; i < nuNumAssets; i++)
    {
        if (bBlownUpHeader)
            buffer.ReadBits4(20); // assetSizes[i]
        else
            buffer.ReadBits4(16);
    }
    for (int i = 0; i < nuNumAssets; i++)
    {
        buffer.BSSkipBits(12);
        if (bStaticFieldsPresent)
        {
            if (buffer.ReadBool())
            {
                buffer.BSSkipBits(4);
            }
            if (buffer.ReadBool())
            {
                buffer.BSSkipBits(24);
            }
            if (buffer.ReadBool())
            {
                int nuInfoTextByteSize = buffer.ReadBits2(10) + 1;
                for (int j = 0; j < nuInfoTextByteSize; j++)
                {
                    buffer.ReadBits2(8); // infoText[j]
                }
            }
            int nuBitResolution = buffer.ReadBits2(5) + 1;
            int nuMaxSampleRate = buffer.ReadBits2(4);
            int nuTotalNumChs = buffer.ReadBits2(8) + 1;
            uint32_t nuSpkrActivityMask = 0;
            if (buffer.ReadBool())
            {
                if (nuTotalNumChs > 2)
                {
                    buffer.BSSkipBits(1);
                }
                if (nuTotalNumChs > 6)
                {
                    buffer.BSSkipBits(1);
                }
                if (buffer.ReadBool())
                {
                    int nuNumBits4SAMask = buffer.ReadBits2(2);
                    nuNumBits4SAMask = nuNumBits4SAMask * 4 + 4;
                    nuSpkrActivityMask = buffer.ReadBits4(nuNumBits4SAMask);
                }
            }
            stream.SampleRate = SampleRates[nuMaxSampleRate];
            stream.BitDepth = nuBitResolution;

            stream.LFE = 0;
            if ((nuSpkrActivityMask & 0x8) == 0x8)
            {
                ++stream.LFE;
            }
            if ((nuSpkrActivityMask & 0x1000) == 0x1000)
            {
                ++stream.LFE;
            }
            stream.ChannelCount = nuTotalNumChs - stream.LFE;
        }
        if (nuNumAssets > 1)
        {
            break;
        }
    }

    uint32_t temp2 = 0;

    while (buffer.Position() < buffer.Length())
    {
        temp2 = (temp2 << 8) + buffer.ReadByte();
        switch (temp2)
        {
            case 0x41A29547: // XLL Extended data
            case 0x655E315E: // XBR Extended data
            case 0x0A801921: // XSA Extended data
            case 0x1D95F262: // X96k
            case 0x47004A03: // XXch
            case 0x5A5A5A5A: // Xch
            {
                int32_t temp3 = 0;
                for (int i = static_cast<int>(buffer.Position()); i < buffer.Length(); i++)
                {
                    temp3 = static_cast<int32_t>((static_cast<uint32_t>(temp3) << 8) + buffer.ReadByte());

                    if (temp3 == 0x02000850) // DTS:X Pattern
                    {
                        stream.HasExtensions = true;
                        break;
                    }
                }
                break;
            }
        }

        if (stream.HasExtensions) break;
    }

    if (stream.CoreStream)
    {
        if (stream.CoreStream->AudioMode == TSAudioMode::Extended && stream.ChannelCount == 5)
        {
            stream.AudioMode = TSAudioMode::Extended;
        }
    }

    if (stream.StreamType == TSStreamType::DTS_HD_MASTER_AUDIO)
    {
        stream.IsVBR = true;
        stream.IsInitialized = true;
    }
    else if (bitrate > 0)
    {
        stream.IsVBR = false;
        stream.BitRate = bitrate;
        if (stream.CoreStream)
        {
            stream.BitRate += stream.CoreStream->BitRate;
            stream.IsInitialized = true;
        }
        stream.IsInitialized = (stream.BitRate > 0);
    }
}

} // namespace TSCodecDTSHD

// ---------------------------------------------------------------------------
// TSCodecLPCM.cs
// ---------------------------------------------------------------------------

namespace TSCodecLPCM {

void Scan(TSAudioStream& stream, TSStreamBuffer& buffer, NString& /*tag*/)
{
    if (stream.IsInitialized) return;

    std::vector<uint8_t> header;
    if (!buffer.ReadBytes(4, header)) net::ThrowNullReference();
    int flags = (header[2] << 8) + header[3];

    switch ((flags & 0xF000) >> 12)
    {
        case 1: stream.ChannelCount = 1; stream.LFE = 0; break;  // 1/0/0
        case 3: stream.ChannelCount = 2; stream.LFE = 0; break;  // 2/0/0
        case 4: stream.ChannelCount = 3; stream.LFE = 0; break;  // 3/0/0
        case 5: stream.ChannelCount = 3; stream.LFE = 0; break;  // 2/1/0
        case 6: stream.ChannelCount = 4; stream.LFE = 0; break;  // 3/1/0
        case 7: stream.ChannelCount = 4; stream.LFE = 0; break;  // 2/2/0
        case 8: stream.ChannelCount = 5; stream.LFE = 0; break;  // 3/2/0
        case 9: stream.ChannelCount = 5; stream.LFE = 1; break;  // 3/2/1
        case 10: stream.ChannelCount = 7; stream.LFE = 0; break; // 3/4/0
        case 11: stream.ChannelCount = 7; stream.LFE = 1; break; // 3/4/1
        default: stream.ChannelCount = 0; stream.LFE = 0; break;
    }

    switch ((flags & 0xC0) >> 6)
    {
        case 1: stream.BitDepth = 16; break;
        case 2: stream.BitDepth = 20; break;
        case 3: stream.BitDepth = 24; break;
        default: stream.BitDepth = 0; break;
    }

    switch ((flags & 0xF00) >> 8)
    {
        case 1: stream.SampleRate = 48000; break;
        case 4: stream.SampleRate = 96000; break;
        case 5: stream.SampleRate = 192000; break;
        default: stream.SampleRate = 0; break;
    }

    stream.BitRate = static_cast<uint32_t>(stream.SampleRate * stream.BitDepth * (stream.ChannelCount + stream.LFE));

    stream.IsVBR = false;
    stream.IsInitialized = true;
}

} // namespace TSCodecLPCM

// ---------------------------------------------------------------------------
// TSCodecMPA.cs
// ---------------------------------------------------------------------------

namespace TSCodecMPA {

static const int MPABitrate[4][4][16] = {
    {
        // MPEG Version 2.5
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0},
    },
    {
        // reserved
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    },
    {
        // MPEG Version 2
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0},
    },
    {
        // MPEG Version 1
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0},
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0},
        {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0},
    }};

static const int MPASampleRate[4][4] = {
    {11025, 12000, 8000, 0}, // MPEG Version 2.5
    {0, 0, 0, 0},            // reserved
    {22050, 24000, 16000, 0}, // MPEG Version 2
    {44100, 48000, 32000, 0}  // MPEG Version 1
};

static const TSAudioMode MPAChannelModes[] = {TSAudioMode::Stereo, TSAudioMode::JointStereo, TSAudioMode::DualMono,
                                              TSAudioMode::Mono};
static const char* MPAVersion[] = {"MPEG 2.5", "Unknown MPEG", "MPEG 2", "MPEG 1"};
static const char* MPALayer[] = {"Unknown Layer", "Layer III", "Layer II", "Layer I"};
static const uint8_t MPAChannels[] = {2, 2, 2, 1};

void Scan(TSAudioStream& stream, TSStreamBuffer& buffer, NString& /*tag*/)
{
    if (stream.IsInitialized) return;

    int syncWord = buffer.ReadBits2(11) << 5;
    if (syncWord != 0b1111111111100000) return;

    int audioVersionID = buffer.ReadBits2(2);
    int layerIndex = buffer.ReadBits2(2);
    buffer.ReadBool(); // protectionBit
    int bitrateIndex = buffer.ReadBits2(4);
    int samplingRateIndex = buffer.ReadBits2(2);
    buffer.ReadBool(); // padding
    buffer.ReadBool(); // privateBit
    int channelMode = buffer.ReadBits2(2);
    buffer.ReadBits2(2); // modeExtension
    buffer.ReadBool();   // copyrightBit
    buffer.ReadBool();   // originalBit
    buffer.ReadBits2(2); // emphasis

    stream.BitRate = MPABitrate[audioVersionID][layerIndex][bitrateIndex] * 1000;
    stream.SampleRate = MPASampleRate[audioVersionID][samplingRateIndex];
    stream.AudioMode = MPAChannelModes[channelMode];

    stream.ChannelCount = MPAChannels[channelMode];
    stream.LFE = 0;

    stream.ExtendedData = std::string(MPAVersion[audioVersionID]) + " " + MPALayer[layerIndex];

    stream.IsVBR = false;
    stream.IsInitialized = true;
}

} // namespace TSCodecMPA

// ---------------------------------------------------------------------------
// TSCodecMPEG2.cs
// ---------------------------------------------------------------------------

namespace TSCodecMPEG2 {

void Scan(TSVideoStream& stream, TSStreamBuffer& buffer, NString& tag)
{
    int32_t parse = 0;
    int pictureParse = 0;
    int sequenceHeaderParse = 0;
    int extensionParse = 0;
    int sequenceExtensionParse = 0;

    for (int i = 0; i < buffer.Length(); i++)
    {
        parse = static_cast<int32_t>((static_cast<uint32_t>(parse) << 8) + buffer.ReadByte());

        if (parse == 0x00000100)
        {
            pictureParse = 2;
        }
        else if (parse == 0x000001B3)
        {
            sequenceHeaderParse = 7;
        }
        else if (sequenceHeaderParse > 0)
        {
            --sequenceHeaderParse;
            if (sequenceHeaderParse == 0)
            {
                stream.IsVBR = true;
                stream.IsInitialized = true;
            }
        }
        else if (pictureParse > 0)
        {
            --pictureParse;
            if (pictureParse == 0)
            {
                switch ((parse & 0x38) >> 3)
                {
                    case 1: tag = "I"; break;
                    case 2: tag = "P"; break;
                    case 3: tag = "B"; break;
                    default: break;
                }
                if (stream.IsInitialized) return;
            }
        }
        else if (parse == 0x000001B5)
        {
            extensionParse = 1;
        }
        else if (extensionParse > 0)
        {
            --extensionParse;
            if (extensionParse == 0)
            {
                if ((parse & 0xF0) == 0x10)
                {
                    sequenceExtensionParse = 1;
                }
            }
        }
        else if (sequenceExtensionParse > 0)
        {
            --sequenceExtensionParse;
        }
    }
}

} // namespace TSCodecMPEG2

// ---------------------------------------------------------------------------
// TSCodecMVC.cs
// ---------------------------------------------------------------------------

namespace TSCodecMVC {

void Scan(TSVideoStream& stream, TSStreamBuffer& /*buffer*/, NString& /*tag*/)
{
    stream.IsVBR = true;
    stream.IsInitialized = true;
}

} // namespace TSCodecMVC

// ---------------------------------------------------------------------------
// TSCodecPGS.cs
// ---------------------------------------------------------------------------

namespace TSCodecPGS {

static std::string ReadODS(TSGraphicsStream& stream, TSStreamBuffer& buffer)
{
    std::string tag;
    buffer.ReadBits2(16, false); // segmentSize
    buffer.ReadBits2(16, false); // object ID

    if (!stream.LastFrame.Finished)
    {
        if (stream.LastFrame.Forced)
        {
            stream.ForcedCaptions++;
            tag = "F";
        }
        else
        {
            stream.Captions++;
            tag = "N";
        }
    }

    return tag;
}

static void ReadPCS(TSGraphicsStream& stream, TSStreamBuffer& buffer)
{
    buffer.ReadBits2(16, false); // segmentSize
    if (!stream.IsInitialized)
    {
        stream.Width = buffer.ReadBits2(16, false);
        stream.Height = buffer.ReadBits2(16, false);
        stream.IsInitialized = true;
    }
    else
    {
        buffer.ReadBits2(16, false);
        buffer.ReadBits2(16, false);
    }

    buffer.ReadByte();
    int compositionNumber = buffer.ReadBits2(16, false);
    buffer.ReadByte(false); // compositionState
    buffer.ReadBits2(16, false);
    int numCompositionObjects = buffer.ReadByte(false);

    for (int i = 0; i < numCompositionObjects; i++)
    {
        buffer.ReadBits2(16, false);             // object ID
        buffer.ReadByte(false);                  // Window ID
        uint8_t forced = buffer.ReadByte(false); // Object Cropped Flag
        buffer.ReadBits2(16, false);             // Object Horizontal Position
        buffer.ReadBits2(16, false);             // Object Vertical Position
        buffer.ReadBits2(16, false);             // Object Cropping Horizontal Position
        buffer.ReadBits2(16, false);             // Object Cropping Vertical Position
        buffer.ReadBits2(16, false);             // Object Cropping Width
        buffer.ReadBits2(16, false);             // Object Cropping Height Position

        stream.LastFrame = PGSFrame{true, (forced & 0x40) == 0x40, false};

        if (!stream.CaptionIDs->ContainsKey(compositionNumber))
        {
            stream.CaptionIDs->Set(compositionNumber, stream.LastFrame);
        }
    }
}

void Scan(TSGraphicsStream& stream, TSStreamBuffer& buffer, NString& tag)
{
    uint8_t segmentType = buffer.ReadByte(false);

    switch (segmentType)
    {
        case 0x15: // ODS: Object Definition Segment
            tag = ReadODS(stream, buffer);
            break;
        case 0x16: // PCS: Presentation Composition Segment
            ReadPCS(stream, buffer);
            break;
        case 0x80:
            if (!stream.LastFrame.Finished) stream.LastFrame.Finished = true;
            break;
        default:
            break;
    }
    stream.IsVBR = true;
}

} // namespace TSCodecPGS

// ---------------------------------------------------------------------------
// TSCodecTrueHD.cs
// ---------------------------------------------------------------------------

namespace TSCodecTrueHD {

void Scan(TSAudioStream& stream, TSStreamBuffer& buffer, NString& tag)
{
    if (stream.IsInitialized && stream.CoreStream && stream.CoreStream->IsInitialized) return;

    bool syncFound = false;
    uint32_t sync = 0;
    for (int i = 0; i < buffer.Length(); i++)
    {
        sync = (sync << 8) + buffer.ReadByte();
        if (sync == 0xF8726FBA)
        {
            syncFound = true;
            break;
        }
    }

    if (!syncFound)
    {
        tag = "CORE";
        if (!stream.CoreStream)
        {
            stream.CoreStream = std::make_shared<TSAudioStream>();
            stream.CoreStream->StreamType = TSStreamType::AC3_AUDIO;
        }
        if (!stream.CoreStream->IsInitialized)
        {
            buffer.BeginRead();
            TSCodecAC3::Scan(*stream.CoreStream, buffer, tag);
        }
        return;
    }

    tag = "HD";
    int ratebits = buffer.ReadBits2(4);
    if (ratebits != 0xF)
    {
        stream.SampleRate = (((ratebits & 8) > 0 ? 44100 : 48000) << (ratebits & 7));
    }
    buffer.BSSkipBits(15);

    stream.ChannelCount = 0;
    stream.LFE = 0;
    if (buffer.ReadBool()) stream.LFE += 1;
    if (buffer.ReadBool()) stream.ChannelCount += 1;
    if (buffer.ReadBool()) stream.ChannelCount += 2;
    if (buffer.ReadBool()) stream.ChannelCount += 2;
    if (buffer.ReadBool()) stream.ChannelCount += 1;
    if (buffer.ReadBool()) stream.ChannelCount += 1;
    if (buffer.ReadBool()) stream.ChannelCount += 2;
    if (buffer.ReadBool()) stream.ChannelCount += 2;
    if (buffer.ReadBool()) stream.ChannelCount += 2;
    if (buffer.ReadBool()) stream.ChannelCount += 2;
    if (buffer.ReadBool()) stream.LFE += 1;
    if (buffer.ReadBool()) stream.ChannelCount += 1;
    if (buffer.ReadBool()) stream.ChannelCount += 2;

    buffer.BSSkipBits(49);

    uint32_t peakBitrate = buffer.ReadBits4(15);
    // uint * int is a 64-bit multiplication in C#.
    peakBitrate = static_cast<uint32_t>((static_cast<int64_t>(peakBitrate) * stream.SampleRate) >> 4);

    double peakBitdepth = static_cast<double>(peakBitrate) / (stream.ChannelCount + stream.LFE) / stream.SampleRate;

    stream.BitDepth = peakBitdepth > 14 ? 24 : 16;

    buffer.BSSkipBits(79);

    bool hasExtensions = buffer.ReadBool();
    int numExtensions = (buffer.ReadBits2(4) * 2) + 1;
    bool hasContent = buffer.ReadBits4(4) != 0;

    if (hasExtensions)
    {
        for (int idx = 0; idx < numExtensions; ++idx)
        {
            if (buffer.ReadBits2(8) != 0) hasContent = true;
        }

        if (hasContent) stream.HasExtensions = true;
    }

    stream.IsVBR = true;
    if (stream.CoreStream && stream.CoreStream->IsInitialized) stream.IsInitialized = true;
}

} // namespace TSCodecTrueHD

// ---------------------------------------------------------------------------
// TSCodecVC1.cs
// ---------------------------------------------------------------------------

namespace TSCodecVC1 {

void Scan(TSVideoStream& stream, TSStreamBuffer& buffer, NString& tag)
{
    int32_t parse = 0;
    uint8_t frameHeaderParse = 0;
    uint8_t sequenceHeaderParse = 0;
    bool isInterlaced = false;

    for (int i = 0; i < buffer.Length(); i++)
    {
        parse = static_cast<int32_t>((static_cast<uint32_t>(parse) << 8) + buffer.ReadByte());

        if (parse == 0x0000010D)
        {
            frameHeaderParse = 4;
        }
        else if (frameHeaderParse > 0)
        {
            --frameHeaderParse;
            if (frameHeaderParse == 0)
            {
                // `parse & 0x80000000` promotes the int to long in C#.
                int64_t wide = parse;
                uint32_t pictureType = 0;
                if (isInterlaced)
                {
                    if ((wide & 0x80000000LL) == 0)
                    {
                        pictureType = static_cast<uint32_t>((parse & 0x78000000) >> 13);
                    }
                    else
                    {
                        pictureType = static_cast<uint32_t>((parse & 0x3c000000) >> 12);
                    }
                }
                else
                {
                    pictureType = static_cast<uint32_t>((wide & 0xf0000000LL) >> 14);
                }

                if ((pictureType & 0x20000) == 0)
                {
                    tag = "P";
                }
                else if ((pictureType & 0x10000) == 0)
                {
                    tag = "B";
                }
                else if ((pictureType & 0x8000) == 0)
                {
                    tag = "I";
                }
                else if ((pictureType & 0x4000) == 0)
                {
                    tag = "BI";
                }
                else
                {
                    tag = std::nullopt;
                }
                if (stream.IsInitialized) return;
            }
        }
        else if (parse == 0x0000010F)
        {
            sequenceHeaderParse = 6;
        }
        else if (sequenceHeaderParse > 0)
        {
            --sequenceHeaderParse;
            switch (sequenceHeaderParse)
            {
                case 5:
                {
                    int profileLevel = ((parse & 0x38) >> 3);
                    if (((parse & 0xC0) >> 6) == 3)
                    {
                        stream.EncodingProfile = "Advanced Profile " + std::to_string(profileLevel);
                    }
                    else
                    {
                        stream.EncodingProfile = "Main Profile " + std::to_string(profileLevel);
                    }
                    break;
                }

                case 0:
                    isInterlaced = ((parse & 0x40) >> 6) > 0;
                    break;
            }
            stream.IsVBR = true;
            stream.IsInitialized = true;
        }
    }
}

} // namespace TSCodecVC1

} // namespace bdinfo
