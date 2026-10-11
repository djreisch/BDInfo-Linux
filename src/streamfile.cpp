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

// TSStreamFile.cs. The field types below match the C# ones exactly (byte
// counters wrap, PTS bits are assembled in 32-bit arithmetic, ...), because
// the scanner's results depend on them.

#include "bdinfo.h"

#include <cstdio>

namespace bdinfo {

using net::A;

struct TSStreamState
{
    uint64_t TransferCount = 0;

    NString StreamTag;

    uint64_t TotalPackets = 0;
    uint64_t WindowPackets = 0;

    uint64_t TotalBytes = 0;
    uint64_t WindowBytes = 0;

    int64_t PeakTransferLength = 0;
    int64_t PeakTransferRate = 0;

    double TransferMarker = 0;
    double TransferInterval = 0;

    TSStreamBuffer StreamBuffer;

    uint32_t Parse = 0;
    bool TransferState = false;
    int32_t TransferLength = 0;
    int32_t PacketLength = 0;
    bool PacketLengthVariable = false;
    uint8_t PacketLengthParse = 0;
    uint8_t PacketParse = 0;

    uint8_t PTSParse = 0;
    BigInteger PTS = 0;
    uint64_t PTSTemp = 0;
    BigInteger PTSLast = 0;
    BigInteger PTSPrev = 0;
    BigInteger PTSDiff = 0;
    uint64_t PTSCount = 0;
    BigInteger PTSTransfer = 0;

    uint8_t DTSParse = 0;
    BigInteger DTSTemp = 0;
    BigInteger DTSPrev = 0;

    uint8_t PESHeaderLength = 0;
    uint8_t PESHeaderFlags = 0;
};

namespace {

struct TSPacketParser
{
    bool SyncState = false;
    uint8_t TimeCodeParse = 4;
    uint8_t PacketLength = 0;
    uint8_t HeaderParse = 0;

    uint32_t TimeCode = 0;
    uint8_t TransportErrorIndicator = 0;
    uint8_t PayloadUnitStartIndicator = 0;
    uint8_t TransportPriority = 0;
    uint16_t PID = 0;
    uint8_t TransportScramblingControl = 0;
    uint8_t AdaptionFieldControl = 0;

    bool AdaptionFieldState = false;
    uint8_t AdaptionFieldParse = 0;
    uint8_t AdaptionFieldLength = 0;
    bool VariablePacketEnd = false;

    uint16_t PCRPID = 0xFFFF;
    uint8_t PCRParse = 0;
    BigInteger PreviousPCR = 0;
    BigInteger PCR = 0;
    BigInteger PCRCount = 0;
    BigInteger PTSFirst = static_cast<BigInteger>(UINT64_MAX);
    BigInteger PTSLast = 0;
    BigInteger PTSDiff = 0;

    std::vector<uint8_t> PAT = std::vector<uint8_t>(1024);
    bool PATSectionStart = false;
    uint8_t PATPointerField = 0;
    uint32_t PATOffset = 0;
    uint8_t PATSectionLengthParse = 0;
    uint16_t PATSectionLength = 0;
    uint32_t PATSectionParse = 0;
    bool PATTransferState = false;
    uint8_t PATSectionNumber = 0;
    uint8_t PATLastSectionNumber = 0;

    uint16_t TransportStreamId = 0xFFFF;

    std::vector<TSDescriptor> PMTProgramDescriptors;
    uint16_t PMTPID = 0xFFFF;
    net::Dictionary<uint16_t, std::shared_ptr<std::vector<uint8_t>>> PMT;
    bool PMTSectionStart = false;
    uint16_t PMTProgramInfoLength = 0;
    uint8_t PMTProgramDescriptor = 0;
    uint8_t PMTProgramDescriptorLengthParse = 0;
    uint8_t PMTProgramDescriptorLength = 0;
    uint16_t PMTStreamInfoLength = 0;
    uint32_t PMTStreamDescriptorLengthParse = 0;
    uint32_t PMTStreamDescriptorLength = 0;
    uint8_t PMTPointerField = 0;
    uint32_t PMTOffset = 0;
    uint32_t PMTSectionLengthParse = 0;
    uint16_t PMTSectionLength = 0;
    uint32_t PMTSectionParse = 0;
    bool PMTTransferState = false;
    uint8_t PMTSectionNumber = 0;
    uint8_t PMTLastSectionNumber = 0;

    uint8_t PMTTemp = 0;

    TSStreamPtr Stream;
    TSStreamState* StreamState = nullptr;

    uint64_t TotalPackets = 0;
};

inline double ToDouble(BigInteger value) { return static_cast<double>(value); }

} // namespace

std::string TSStreamFile::DisplayName() const
{
    if (BDInfoSettings::EnableSSIF && InterleavedFile)
    {
        return InterleavedFile->Name;
    }
    return Name;
}

bool TSStreamFile::ScanStream(TSStream& stream, TSStreamState& streamState, TSStreamBuffer& buffer, bool isFullScan)
{
    streamState.StreamTag = std::nullopt;

    int64_t bitrate = 0;
    if (stream.IsAudioStream() && streamState.PTSTransfer > 0)
    {
        bitrate = net::ToInt64(net::Round((buffer.TransferLength * 8.0) / (ToDouble(streamState.PTSTransfer) / 90000)));

        if (bitrate > streamState.PeakTransferRate)
        {
            streamState.PeakTransferRate = bitrate;
        }
    }
    if (buffer.TransferLength > streamState.PeakTransferLength)
    {
        streamState.PeakTransferLength = buffer.TransferLength;
    }

    buffer.BeginRead();
    switch (stream.StreamType)
    {
        case TSStreamType::MPEG2_VIDEO:
            TSCodecMPEG2::Scan(static_cast<TSVideoStream&>(stream), buffer, streamState.StreamTag);
            break;

        case TSStreamType::AVC_VIDEO:
            TSCodecAVC::Scan(static_cast<TSVideoStream&>(stream), buffer, streamState.StreamTag);
            break;

        case TSStreamType::MVC_VIDEO:
            TSCodecMVC::Scan(static_cast<TSVideoStream&>(stream), buffer, streamState.StreamTag);
            break;

        case TSStreamType::HEVC_VIDEO:
            TSCodecHEVC::Scan(static_cast<TSVideoStream&>(stream), buffer, streamState.StreamTag);
            break;

        case TSStreamType::VC1_VIDEO:
            TSCodecVC1::Scan(static_cast<TSVideoStream&>(stream), buffer, streamState.StreamTag);
            break;

        case TSStreamType::MPEG1_AUDIO:
        case TSStreamType::MPEG2_AUDIO:
            TSCodecMPA::Scan(static_cast<TSAudioStream&>(stream), buffer, streamState.StreamTag);
            break;

        case TSStreamType::MPEG2_AAC_AUDIO:
        case TSStreamType::MPEG4_AAC_AUDIO:
            TSCodecAAC::Scan(static_cast<TSAudioStream&>(stream), buffer, streamState.StreamTag);
            break;

        case TSStreamType::AC3_AUDIO:
        case TSStreamType::AC3_PLUS_AUDIO:
        case TSStreamType::AC3_PLUS_SECONDARY_AUDIO:
            TSCodecAC3::Scan(static_cast<TSAudioStream&>(stream), buffer, streamState.StreamTag);
            break;

        case TSStreamType::AC3_TRUE_HD_AUDIO:
            TSCodecTrueHD::Scan(static_cast<TSAudioStream&>(stream), buffer, streamState.StreamTag);
            break;

        case TSStreamType::LPCM_AUDIO:
            TSCodecLPCM::Scan(static_cast<TSAudioStream&>(stream), buffer, streamState.StreamTag);
            break;

        case TSStreamType::DTS_AUDIO:
            TSCodecDTS::Scan(static_cast<TSAudioStream&>(stream), buffer, bitrate, streamState.StreamTag);
            break;

        case TSStreamType::DTS_HD_AUDIO:
        case TSStreamType::DTS_HD_MASTER_AUDIO:
        case TSStreamType::DTS_HD_SECONDARY_AUDIO:
            TSCodecDTSHD::Scan(static_cast<TSAudioStream&>(stream), buffer, bitrate, streamState.StreamTag);
            break;

        case TSStreamType::PRESENTATION_GRAPHICS:
            if (isFullScan)
                TSCodecPGS::Scan(static_cast<TSGraphicsStream&>(stream), buffer, streamState.StreamTag);
            else
                stream.IsInitialized = true;
            break;

        default:
            stream.IsInitialized = true;
            break;
    }
    buffer.EndRead();
    streamState.StreamBuffer.Reset();

    bool isAVC = false;
    bool isMVC = false;
    for (const auto& entry : Streams)
    {
        const TSStreamPtr& finishedStream = entry.second;
        if (!finishedStream->IsInitialized)
        {
            return false;
        }
        if (finishedStream->StreamType == TSStreamType::AVC_VIDEO)
        {
            isAVC = true;
        }
        if (finishedStream->StreamType == TSStreamType::MVC_VIDEO)
        {
            isMVC = true;
        }
    }
    if (isMVC && !isAVC)
    {
        return false;
    }
    return true;
}

void TSStreamFile::UpdateStreamBitrates(uint16_t PTSPID, BigInteger PTS, BigInteger PTSDiff)
{
    for (const auto& entry : StreamStates)
    {
        uint16_t PID = entry.first;
        if (Streams.ContainsKey(PID) && Streams[PID]->IsVideoStream() && PID != PTSPID)
        {
            continue;
        }
        if (entry.second->WindowPackets == 0)
        {
            continue;
        }
        UpdateStreamBitrate(PID, PTSPID, PTS, PTSDiff);
    }

    for (TSPlaylistFile* playlist : Playlists)
    {
        double packetSeconds = 0;
        for (const auto& clip : playlist->StreamClips)
        {
            if (clip->AngleIndex == 0)
            {
                packetSeconds += clip->PacketSeconds;
            }
        }
        if (packetSeconds > 0)
        {
            for (const auto& playlistStream : playlist->SortedStreams)
            {
                if (playlistStream->IsVBR)
                {
                    playlistStream->BitRate =
                        net::ToInt64(net::Round((playlistStream->PayloadBytes * 8.0) / packetSeconds));
                }
            }
        }
    }
}

void TSStreamFile::UpdateStreamBitrate(uint16_t PID, uint16_t /*PTSPID*/, BigInteger PTS, BigInteger PTSDiff)
{
    TSStreamState& streamState = *StreamStates[PID];
    double streamTime = ToDouble(PTS) / 90000;
    double streamInterval = ToDouble(PTSDiff) / 90000;
    double streamOffset = streamTime + streamInterval;

    for (TSPlaylistFile* playlist : Playlists)
    {
        for (const auto& clip : playlist->StreamClips)
        {
            if (clip->Name != Name) continue;

            if (streamTime == 0 || (streamTime >= clip->TimeIn && streamTime <= clip->TimeOut))
            {
                clip->PayloadBytes += streamState.WindowBytes;
                clip->PacketCount += streamState.WindowPackets;

                if (streamOffset > clip->TimeIn && streamOffset - clip->TimeIn > clip->PacketSeconds)
                {
                    clip->PacketSeconds = streamOffset - clip->TimeIn;
                }

                net::Dictionary<uint16_t, TSStreamPtr>* playlistStreams = &playlist->Streams;
                if (clip->AngleIndex > 0 && clip->AngleIndex < static_cast<int>(playlist->AngleStreams.size()) + 1)
                {
                    playlistStreams = &playlist->AngleStreams[static_cast<size_t>(clip->AngleIndex - 1)];
                }
                if (playlistStreams->ContainsKey(PID))
                {
                    TSStream& stream = *(*playlistStreams)[PID];

                    stream.PayloadBytes += streamState.WindowBytes;
                    stream.PacketCount += streamState.WindowPackets;

                    if (stream.IsVideoStream())
                    {
                        stream.PacketSeconds += streamInterval;

                        stream.ActiveBitRate =
                            net::ToInt64(net::Round((stream.PayloadBytes * 8.0) / stream.PacketSeconds));
                    }

                    if (stream.StreamType == TSStreamType::AC3_TRUE_HD_AUDIO &&
                        static_cast<TSAudioStream&>(stream).CoreStream)
                    {
                        stream.ActiveBitRate -= static_cast<TSAudioStream&>(stream).CoreStream->BitRate;
                    }
                }
            }
        }
    }

    if (Streams.ContainsKey(PID))
    {
        TSStream& stream = *Streams[PID];
        stream.PayloadBytes += streamState.WindowBytes;
        stream.PacketCount += streamState.WindowPackets;

        if (stream.IsVideoStream())
        {
            TSStreamDiagnostics diag;
            diag.Marker = ToDouble(PTS) / 90000;
            diag.Interval = ToDouble(PTSDiff) / 90000;
            diag.Bytes = streamState.WindowBytes;
            diag.Packets = streamState.WindowPackets;
            diag.Tag = streamState.StreamTag;
            StreamDiagnostics[PID]->push_back(diag);

            stream.PacketSeconds += streamInterval;
        }
    }
    streamState.WindowPackets = 0;
    streamState.WindowBytes = 0;
}

void TSStreamFile::Scan(const std::vector<TSPlaylistFile*>& playlists, bool isFullScan)
{
    if (playlists.empty())
    {
        return;
    }

    Playlists = playlists;
    const int dataSize = 5242880;

    const FileRefPtr& file = (BDInfoSettings::EnableSSIF && InterleavedFile) ? InterleavedFile->File : File;
    std::unique_ptr<InputStream> fileStream = file->OpenRead();

    Size = 0;
    Length = 0;

    Streams.Clear();
    StreamStates.Clear();
    StreamDiagnostics.Clear();

    TSPacketParser parser;

    std::vector<uint8_t> bufferStorage(dataSize);
    uint8_t* buffer = bufferStorage.data();
    int bufferLength = 0;

    while ((bufferLength = fileStream->Read(buffer, dataSize)) > 0)
    {
        int offset = 0;
        for (int i = 0; i < bufferLength; i++)
        {
            if (parser.SyncState == false)
            {
                if (parser.TimeCodeParse > 0)
                {
                    parser.TimeCodeParse--;
                    switch (parser.TimeCodeParse)
                    {
                        case 3:
                            parser.TimeCode = 0;
                            parser.TimeCode |= (static_cast<uint32_t>(buffer[i]) & 0x3F) << 24;
                            break;
                        case 2:
                            parser.TimeCode |= (static_cast<uint32_t>(buffer[i]) & 0xFF) << 16;
                            break;
                        case 1:
                            parser.TimeCode |= (static_cast<uint32_t>(buffer[i]) & 0xFF) << 8;
                            break;
                        case 0:
                            parser.TimeCode |= (static_cast<uint32_t>(buffer[i]) & 0xFF);
                            break;
                    }
                }
                else if (buffer[i] == 0x47)
                {
                    parser.SyncState = true;
                    parser.PacketLength = 187;
                    parser.TimeCodeParse = 4;
                    parser.HeaderParse = 3;
                }
            }
            else if (parser.HeaderParse > 0)
            {
                parser.PacketLength--;
                parser.HeaderParse--;

                switch (parser.HeaderParse)
                {
                    case 2:
                    {
                        parser.TransportErrorIndicator = static_cast<uint8_t>((buffer[i] >> 7) & 0x1);
                        parser.PayloadUnitStartIndicator = static_cast<uint8_t>((buffer[i] >> 6) & 0x1);
                        parser.TransportPriority = static_cast<uint8_t>((buffer[i] >> 5) & 0x1);
                        parser.PID = static_cast<uint16_t>((buffer[i] & 0x1f) << 8);
                    }
                    break;

                    case 1:
                    {
                        parser.PID |= static_cast<uint16_t>(buffer[i]);
                        if (Streams.ContainsKey(parser.PID))
                        {
                            parser.Stream = Streams[parser.PID];
                        }
                        else
                        {
                            parser.Stream = nullptr;
                        }
                        if (!StreamStates.ContainsKey(parser.PID))
                        {
                            StreamStates.Set(parser.PID, std::make_shared<TSStreamState>());
                        }
                        parser.StreamState = StreamStates[parser.PID].get();
                        parser.StreamState->TotalPackets++;
                        parser.StreamState->WindowPackets++;
                        parser.TotalPackets++;
                    }
                    break;

                    case 0:
                    {
                        parser.TransportScramblingControl = static_cast<uint8_t>((buffer[i] >> 6) & 0x3);
                        parser.AdaptionFieldControl = static_cast<uint8_t>((buffer[i] >> 4) & 0x3);

                        if ((parser.AdaptionFieldControl & 0x2) == 0x2)
                        {
                            parser.AdaptionFieldState = true;
                        }
                        if (parser.PayloadUnitStartIndicator == 1)
                        {
                            if (parser.PID == 0)
                            {
                                parser.PATSectionStart = true;
                            }
                            else if (parser.PID == parser.PMTPID)
                            {
                                parser.PMTSectionStart = true;
                            }
                            else if (parser.StreamState != nullptr && parser.StreamState->TransferState)
                            {
                                parser.StreamState->TransferState = false;
                                parser.StreamState->TransferCount++;

                                bool isFinished = ScanStream(*net::NN(parser.Stream), *parser.StreamState,
                                                             parser.StreamState->StreamBuffer, isFullScan);

                                if (!isFullScan && isFinished)
                                {
                                    return;
                                }
                            }
                        }
                    }
                    break;
                }
            }
            else if (parser.AdaptionFieldState)
            {
                parser.PacketLength--;
                parser.AdaptionFieldParse = buffer[i];
                parser.AdaptionFieldLength = buffer[i];
                parser.AdaptionFieldState = false;
                parser.VariablePacketEnd = true;
            }
            else if (parser.AdaptionFieldParse > 0)
            {
                parser.PacketLength--;
                parser.AdaptionFieldParse--;
                if ((parser.AdaptionFieldLength - parser.AdaptionFieldParse) == 1)
                {
                    if ((buffer[i] & 0x10) == 0x10)
                    {
                        parser.PCRParse = 6;
                        parser.PCR = 0;
                    }
                }
                else if (parser.PCRParse > 0)
                {
                    parser.PCRParse--;
                    parser.PCR = (parser.PCR << 8) + static_cast<BigInteger>(buffer[i]);
                    if (parser.PCRParse == 0)
                    {
                        parser.PreviousPCR = parser.PCR;
                        parser.PCR = (parser.PCR & 0x1FF) + ((parser.PCR >> 15) * 300);
                    }
                    parser.PCRCount++;
                }
                if (parser.PacketLength <= 0)
                {
                    parser.SyncState = false;
                }
            }
            else if (parser.PID == 0)
            {
                if (parser.PATTransferState)
                {
                    if ((bufferLength - i) > parser.PATSectionLength)
                    {
                        offset = parser.PATSectionLength;
                    }
                    else
                    {
                        offset = (bufferLength - i);
                    }
                    if (parser.PacketLength <= offset)
                    {
                        offset = parser.PacketLength;
                    }

                    for (int k = 0; k < offset; k++)
                    {
                        A(parser.PAT, parser.PATOffset++) = buffer[i++];
                        parser.PATSectionLength--;
                        parser.PacketLength--;
                    }
                    --i;

                    if (parser.PATSectionLength == 0)
                    {
                        parser.PATTransferState = false;
                        if (parser.PATSectionNumber == parser.PATLastSectionNumber)
                        {
                            for (int64_t k = 0; k < static_cast<uint32_t>(parser.PATOffset - 4); k += 4)
                            {
                                uint32_t programNumber =
                                    static_cast<uint32_t>((A(parser.PAT, k) << 8) + A(parser.PAT, k + 1));

                                uint16_t programPID =
                                    static_cast<uint16_t>(((A(parser.PAT, k + 2) & 0x1F) << 8) + A(parser.PAT, k + 3));

                                if (programNumber == 1)
                                {
                                    parser.PMTPID = programPID;
                                }
                            }
                        }
                    }
                }
                else
                {
                    --parser.PacketLength;
                    if (parser.PATSectionStart)
                    {
                        parser.PATPointerField = buffer[i];
                        if (parser.PATPointerField == 0)
                        {
                            parser.PATSectionLengthParse = 3;
                        }
                        parser.PATSectionStart = false;
                    }
                    else if (parser.PATPointerField > 0)
                    {
                        --parser.PATPointerField;
                        if (parser.PATPointerField == 0)
                        {
                            parser.PATSectionLengthParse = 3;
                        }
                    }
                    else if (parser.PATSectionLengthParse > 0)
                    {
                        --parser.PATSectionLengthParse;
                        switch (parser.PATSectionLengthParse)
                        {
                            case 2:
                                break;
                            case 1:
                                parser.PATSectionLength = static_cast<uint16_t>((buffer[i] & 0xF) << 8);
                                break;
                            case 0:
                                parser.PATSectionLength |= buffer[i];
                                if (parser.PATSectionLength > 1021)
                                {
                                    parser.PATSectionLength = 0;
                                }
                                else
                                {
                                    parser.PATSectionParse = 5;
                                }
                                break;
                        }
                    }
                    else if (parser.PATSectionParse > 0)
                    {
                        --parser.PATSectionLength;
                        --parser.PATSectionParse;

                        switch (parser.PATSectionParse)
                        {
                            case 4:
                                parser.TransportStreamId = static_cast<uint16_t>(buffer[i] << 8);
                                break;
                            case 3:
                                parser.TransportStreamId |= buffer[i];
                                break;
                            case 2:
                                break;
                            case 1:
                                parser.PATSectionNumber = buffer[i];
                                if (parser.PATSectionNumber == 0)
                                {
                                    parser.PATOffset = 0;
                                }
                                break;
                            case 0:
                                parser.PATLastSectionNumber = buffer[i];
                                parser.PATTransferState = true;
                                break;
                        }
                    }
                }
                if (parser.PacketLength <= 0)
                {
                    parser.SyncState = false;
                }
            }
            else if (parser.PID == parser.PMTPID)
            {
                if (parser.PMTTransferState)
                {
                    if ((bufferLength - i) >= parser.PMTSectionLength)
                    {
                        offset = parser.PMTSectionLength;
                    }
                    else
                    {
                        offset = (bufferLength - i);
                    }
                    if (parser.PacketLength <= offset)
                    {
                        offset = parser.PacketLength;
                    }
                    if (!parser.PMT.ContainsKey(parser.PID))
                    {
                        parser.PMT.Set(parser.PID, std::make_shared<std::vector<uint8_t>>(1024));
                    }

                    std::vector<uint8_t>& PMT = *parser.PMT[parser.PID];
                    for (int k = 0; k < offset; k++)
                    {
                        A(PMT, parser.PMTOffset++) = buffer[i++];
                        --parser.PMTSectionLength;
                        --parser.PacketLength;
                    }
                    --i;

                    if (parser.PMTSectionLength == 0)
                    {
                        parser.PMTTransferState = false;
                        if (parser.PMTSectionNumber == parser.PMTLastSectionNumber)
                        {
                            try
                            {
                                for (int64_t k = 0; k < static_cast<uint32_t>(parser.PMTOffset - 4); k += 5)
                                {
                                    uint8_t streamType = A(PMT, k);

                                    uint16_t streamPID =
                                        static_cast<uint16_t>(((A(PMT, k + 1) & 0x1F) << 8) + A(PMT, k + 2));

                                    uint16_t streamInfoLength =
                                        static_cast<uint16_t>(((A(PMT, k + 3) & 0xF) << 8) + A(PMT, k + 4));

                                    if (!Streams.ContainsKey(streamPID))
                                    {
                                        auto streamDescriptors = std::make_shared<std::vector<TSDescriptor>>();
                                        CreateStream(streamPID, streamType, streamDescriptors);
                                        if (Streams[streamPID]->IsGraphicsStream())
                                            Streams[streamPID]->IsInitialized = !isFullScan;
                                    }
                                    k += streamInfoLength;
                                }
                            }
                            catch (const std::exception& ex)
                            {
                                // TODO (the C# code writes the message to the console)
                                std::string message = net::MessageOf(ex) + "\n";
                                fwrite(message.data(), 1, message.size(), stdout);
                                fflush(stdout);
                            }
                        }
                    }
                }
                else
                {
                    --parser.PacketLength;
                    if (parser.PMTSectionStart)
                    {
                        parser.PMTPointerField = buffer[i];
                        if (parser.PMTPointerField == 0)
                        {
                            parser.PMTSectionLengthParse = 3;
                        }
                        parser.PMTSectionStart = false;
                    }
                    else if (parser.PMTPointerField > 0)
                    {
                        --parser.PMTPointerField;
                        if (parser.PMTPointerField == 0)
                        {
                            parser.PMTSectionLengthParse = 3;
                        }
                    }
                    else if (parser.PMTSectionLengthParse > 0)
                    {
                        --parser.PMTSectionLengthParse;
                        switch (parser.PMTSectionLengthParse)
                        {
                            case 2:
                                if (buffer[i] != 0x2)
                                {
                                    parser.PMTSectionLengthParse = 0;
                                }
                                break;
                            case 1:
                                parser.PMTSectionLength = static_cast<uint16_t>((buffer[i] & 0xF) << 8);
                                break;
                            case 0:
                                parser.PMTSectionLength |= buffer[i];
                                if (parser.PMTSectionLength > 1021)
                                {
                                    parser.PMTSectionLength = 0;
                                }
                                else
                                {
                                    parser.PMTSectionParse = 9;
                                }
                                break;
                        }
                    }
                    else if (parser.PMTSectionParse > 0)
                    {
                        --parser.PMTSectionLength;
                        --parser.PMTSectionParse;

                        switch (parser.PMTSectionParse)
                        {
                            case 8:
                            case 7:
                                break;
                            case 6:
                                parser.PMTTemp = buffer[i];
                                break;
                            case 5:
                                parser.PMTSectionNumber = buffer[i];
                                if (parser.PMTSectionNumber == 0)
                                {
                                    parser.PMTOffset = 0;
                                }
                                break;
                            case 4:
                                parser.PMTLastSectionNumber = buffer[i];
                                break;
                            case 3:
                                parser.PCRPID = static_cast<uint16_t>((buffer[i] & 0x1F) << 8);
                                break;
                            case 2:
                                parser.PCRPID |= buffer[i];
                                break;
                            case 1:
                                parser.PMTProgramInfoLength = static_cast<uint16_t>((buffer[i] & 0xF) << 8);
                                break;
                            case 0:
                                parser.PMTProgramInfoLength |= buffer[i];
                                if (parser.PMTProgramInfoLength == 0)
                                {
                                    parser.PMTTransferState = true;
                                }
                                else
                                {
                                    parser.PMTProgramDescriptorLengthParse = 2;
                                }
                                break;
                        }
                    }
                    else if (parser.PMTProgramInfoLength > 0)
                    {
                        --parser.PMTSectionLength;
                        --parser.PMTProgramInfoLength;

                        if (parser.PMTProgramDescriptorLengthParse > 0)
                        {
                            --parser.PMTProgramDescriptorLengthParse;
                            switch (parser.PMTProgramDescriptorLengthParse)
                            {
                                case 1:
                                    parser.PMTProgramDescriptor = buffer[i];
                                    break;
                                case 0:
                                    parser.PMTProgramDescriptorLength = buffer[i];
                                    parser.PMTProgramDescriptors.emplace_back(parser.PMTProgramDescriptor,
                                                                              parser.PMTProgramDescriptorLength);
                                    break;
                            }
                        }
                        else if (parser.PMTProgramDescriptorLength > 0)
                        {
                            --parser.PMTProgramDescriptorLength;

                            if (parser.PMTProgramDescriptors.empty()) net::ThrowArgumentOutOfRange();
                            TSDescriptor& descriptor = parser.PMTProgramDescriptors.back();

                            int valueIndex =
                                static_cast<int>(descriptor.Value.size()) - parser.PMTProgramDescriptorLength - 1;

                            A(descriptor.Value, valueIndex) = buffer[i];

                            if (parser.PMTProgramDescriptorLength == 0 && parser.PMTProgramInfoLength > 0)
                            {
                                parser.PMTProgramDescriptorLengthParse = 2;
                            }
                        }
                        if (parser.PMTProgramInfoLength == 0)
                        {
                            parser.PMTTransferState = true;
                        }
                    }
                }
                if (parser.PacketLength <= 0)
                {
                    parser.SyncState = false;
                }
            }
            else if (parser.Stream && parser.StreamState != nullptr && parser.TransportScramblingControl == 0)
            {
                TSStream& stream = *parser.Stream;
                TSStreamState& streamState = *parser.StreamState;

                streamState.Parse = (streamState.Parse << 8) + buffer[i];

                if (streamState.TransferState)
                {
                    if ((bufferLength - i) >= streamState.PacketLength && streamState.PacketLength > 0 &&
                        !streamState.PacketLengthVariable)
                    {
                        offset = streamState.PacketLength;
                    }
                    else if ((bufferLength - i) >= parser.PacketLength && parser.PacketLength > 0 &&
                             streamState.PacketLengthVariable)
                    {
                        offset = parser.PacketLength;
                    }
                    else
                    {
                        offset = (bufferLength - i);
                    }
                    if (parser.PacketLength <= offset)
                    {
                        offset = parser.PacketLength;
                    }
                    streamState.TransferLength = offset;

                    if (!stream.IsInitialized || stream.IsVideoStream() || stream.IsGraphicsStream())
                    {
                        streamState.StreamBuffer.Add(buffer, i, offset);
                    }
                    else
                    {
                        streamState.StreamBuffer.TransferLength += offset;
                    }

                    i += streamState.TransferLength - 1;
                    streamState.PacketLength -= streamState.TransferLength;
                    parser.PacketLength = static_cast<uint8_t>(parser.PacketLength - static_cast<uint8_t>(streamState.TransferLength));

                    streamState.TotalBytes += static_cast<uint64_t>(static_cast<int64_t>(streamState.TransferLength));
                    streamState.WindowBytes += static_cast<uint64_t>(static_cast<int64_t>(streamState.TransferLength));

                    if (parser.VariablePacketEnd && streamState.PacketLengthVariable)
                    {
                        parser.VariablePacketEnd = false;
                        streamState.PacketLengthVariable = false;
                    }

                    if (streamState.PacketLength == 0)
                    {
                        streamState.TransferState = false;
                        streamState.TransferCount++;
                        bool isFinished = ScanStream(stream, streamState, streamState.StreamBuffer, isFullScan);

                        if (!isFullScan && isFinished)
                        {
                            return;
                        }
                    }
                }
                else
                {
                    --parser.PacketLength;

                    bool headerFound = false;
                    if (stream.IsVideoStream() && streamState.Parse == 0x000001FD)
                    {
                        headerFound = true;
                    }
                    if (stream.IsVideoStream() && streamState.Parse >= 0x000001E0 && streamState.Parse <= 0x000001EF)
                    {
                        headerFound = true;
                    }
                    if (stream.IsAudioStream() && streamState.Parse == 0x000001BD)
                    {
                        headerFound = true;
                    }
                    if (stream.IsAudioStream() && streamState.Parse >= 0x000001C0 && streamState.Parse <= 0x000001DF)
                    {
                        headerFound = true;
                    }
                    if (stream.IsAudioStream() && (streamState.Parse == 0x000001FA || streamState.Parse == 0x000001FD))
                    {
                        headerFound = true;
                    }

                    if (!stream.IsVideoStream() && !stream.IsAudioStream() &&
                        (streamState.Parse == 0x000001FA || streamState.Parse == 0x000001FD ||
                         streamState.Parse == 0x000001BD ||
                         (streamState.Parse >= 0x000001E0 && streamState.Parse <= 0x000001EF)))
                    {
                        headerFound = true;
                    }

                    if (headerFound)
                    {
                        streamState.PacketLengthParse = 2;
                    }
                    else if (streamState.PacketLengthParse > 0)
                    {
                        --streamState.PacketLengthParse;
                        switch (streamState.PacketLengthParse)
                        {
                            case 1:
                                break;

                            case 0:
                                streamState.PacketLength = static_cast<int32_t>(streamState.Parse & 0xFFFF);
                                if (streamState.PacketLength == 0)
                                {
                                    parser.VariablePacketEnd = false;
                                    streamState.PacketLengthVariable = true;
                                }

                                streamState.PacketParse = 3;
                                break;
                        }
                    }
                    else if (streamState.PacketParse > 0)
                    {
                        --streamState.PacketLength;
                        --streamState.PacketParse;

                        switch (streamState.PacketParse)
                        {
                            case 2:
                                break;

                            case 1:
                                streamState.PESHeaderFlags = static_cast<uint8_t>(streamState.Parse & 0xFF);
                                break;

                            case 0:
                                streamState.PESHeaderLength = static_cast<uint8_t>(streamState.Parse & 0xFF);
                                if ((streamState.PESHeaderFlags & 0xC0) == 0x80)
                                {
                                    streamState.PTSParse = 5;
                                }
                                else if ((streamState.PESHeaderFlags & 0xC0) == 0xC0)
                                {
                                    streamState.DTSParse = 10;
                                }
                                if (streamState.PESHeaderLength == 0)
                                {
                                    streamState.TransferState = true;
                                }
                                break;
                        }
                    }
                    else if (streamState.PTSParse > 0)
                    {
                        --streamState.PacketLength;
                        --streamState.PESHeaderLength;
                        --streamState.PTSParse;

                        switch (streamState.PTSParse)
                        {
                            case 4:
                                streamState.PTSTemp = ((streamState.Parse & 0xE) << 29);
                                break;

                            case 3:
                                streamState.PTSTemp |= ((streamState.Parse & 0xFF) << 22);
                                break;

                            case 2:
                                streamState.PTSTemp |= ((streamState.Parse & 0xFE) << 14);
                                break;

                            case 1:
                                streamState.PTSTemp |= ((streamState.Parse & 0xFF) << 7);
                                break;

                            case 0:
                                streamState.PTSTemp |= ((streamState.Parse & 0xFE) >> 1);
                                streamState.PTS = streamState.PTSTemp;

                                if (streamState.PTS != streamState.PTSLast)
                                {
                                    if (streamState.PTSLast > 0)
                                    {
                                        streamState.PTSTransfer = (streamState.PTS - streamState.PTSLast);
                                    }
                                    streamState.PTSLast = streamState.PTS;
                                }

                                // TODO: Frame reorder for streams encoded with b-pyramid > 0
                                streamState.PTSDiff = streamState.PTS - streamState.DTSPrev;

                                if (streamState.PTSCount > 0 && stream.IsVideoStream())
                                {
                                    UpdateStreamBitrates(stream.PID, streamState.PTS, streamState.PTSDiff);
                                    if (streamState.DTSTemp < parser.PTSFirst)
                                    {
                                        parser.PTSFirst = streamState.DTSTemp;
                                    }
                                    if (streamState.DTSTemp > parser.PTSLast)
                                    {
                                        parser.PTSLast = streamState.DTSTemp;
                                    }
                                    Length = ToDouble(parser.PTSLast - parser.PTSFirst) / 90000;
                                }

                                streamState.DTSPrev = streamState.PTS;
                                streamState.PTSCount++;
                                if (streamState.PESHeaderLength == 0)
                                {
                                    streamState.TransferState = true;
                                }
                                break;
                        }
                    }
                    else if (streamState.DTSParse > 0)
                    {
                        --streamState.PacketLength;
                        --streamState.PESHeaderLength;
                        --streamState.DTSParse;

                        switch (streamState.DTSParse)
                        {
                            case 9:
                                streamState.PTSTemp = ((streamState.Parse & 0xE) << 29);
                                break;

                            case 8:
                                streamState.PTSTemp |= ((streamState.Parse & 0xFF) << 22);
                                break;

                            case 7:
                                streamState.PTSTemp |= ((streamState.Parse & 0xFE) << 14);
                                break;

                            case 6:
                                streamState.PTSTemp |= ((streamState.Parse & 0xFF) << 7);
                                break;

                            case 5:
                                streamState.PTSTemp |= ((streamState.Parse & 0xFE) >> 1);
                                streamState.PTS = streamState.PTSTemp;
                                if (streamState.PTS > streamState.PTSLast)
                                {
                                    streamState.PTSLast = streamState.PTS;
                                }
                                break;

                            case 4:
                                streamState.DTSTemp = ((streamState.Parse & 0xE) << 29);
                                break;

                            case 3:
                                streamState.DTSTemp |= ((streamState.Parse & 0xFF) << 22);
                                break;

                            case 2:
                                streamState.DTSTemp |= ((streamState.Parse & 0xFE) << 14);
                                break;

                            case 1:
                                streamState.DTSTemp |= ((streamState.Parse & 0xFF) << 7);
                                break;

                            case 0:
                                streamState.DTSTemp |= ((streamState.Parse & 0xFE) >> 1);

                                // TODO: Frame reorder for streams encoded with b-pyramid > 0
                                streamState.PTSDiff = streamState.DTSTemp - streamState.DTSPrev;

                                if (streamState.PTSCount > 0 && stream.IsVideoStream())
                                {
                                    UpdateStreamBitrates(stream.PID, streamState.DTSTemp, streamState.PTSDiff);
                                    if (streamState.DTSTemp < parser.PTSFirst)
                                    {
                                        parser.PTSFirst = streamState.DTSTemp;
                                    }
                                    if (streamState.DTSTemp > parser.PTSLast)
                                    {
                                        parser.PTSLast = streamState.DTSTemp;
                                    }
                                    Length = ToDouble(parser.PTSLast - parser.PTSFirst) / 90000;
                                }
                                streamState.DTSPrev = streamState.DTSTemp;
                                streamState.PTSCount++;
                                if (streamState.PESHeaderLength == 0)
                                {
                                    streamState.TransferState = true;
                                }
                                break;
                        }
                    }
                    else if (streamState.PESHeaderLength > 0)
                    {
                        --streamState.PacketLength;
                        --streamState.PESHeaderLength;
                        if (streamState.PESHeaderLength == 0)
                        {
                            streamState.TransferState = true;
                        }
                    }
                }
                if (parser.PacketLength <= 0)
                {
                    parser.SyncState = false;
                }
            }
            else
            {
                parser.PacketLength--;
                if ((bufferLength - i) >= parser.PacketLength)
                {
                    i = i + parser.PacketLength;
                    parser.PacketLength = 0;
                }
                else
                {
                    parser.PacketLength = static_cast<uint8_t>(parser.PacketLength - static_cast<uint8_t>((bufferLength - i) + 1));
                    i = bufferLength;
                }
                if (parser.PacketLength <= 0)
                {
                    parser.SyncState = false;
                }
            }
        }
        Size += bufferLength;
    }

    // TODO: Frame reorder for streams encoded with b-pyramid > 0
    BigInteger PTSLast = 0;
    BigInteger PTSDiff = 0;
    for (const auto& entry : Streams)
    {
        const TSStreamPtr& stream = entry.second;
        if (!stream->IsVideoStream()) continue;

        if (StreamStates.ContainsKey(stream->PID) && StreamStates[stream->PID]->PTSLast > PTSLast)
        {
            PTSLast = StreamStates[stream->PID]->PTSLast;
            PTSDiff = PTSLast - StreamStates[stream->PID]->DTSPrev;
        }
        UpdateStreamBitrates(stream->PID, PTSLast, PTSDiff);
    }
}

TSStreamPtr TSStreamFile::CreateStream(uint16_t streamPID, uint8_t streamType,
                                       const std::shared_ptr<std::vector<TSDescriptor>>& streamDescriptors)
{
    TSStreamPtr stream;

    switch (static_cast<TSStreamType>(streamType))
    {
        case TSStreamType::MVC_VIDEO:
        case TSStreamType::AVC_VIDEO:
        case TSStreamType::HEVC_VIDEO:
        case TSStreamType::MPEG1_VIDEO:
        case TSStreamType::MPEG2_VIDEO:
        case TSStreamType::VC1_VIDEO:
            stream = std::make_shared<TSVideoStream>();
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
            stream = std::make_shared<TSAudioStream>();
            break;

        case TSStreamType::INTERACTIVE_GRAPHICS:
        case TSStreamType::PRESENTATION_GRAPHICS:
            stream = std::make_shared<TSGraphicsStream>();
            break;

        case TSStreamType::SUBTITLE:
            stream = std::make_shared<TSTextStream>();
            break;

        default:
            break;
    }

    if (stream && !Streams.ContainsKey(streamPID))
    {
        stream->PID = streamPID;
        stream->StreamType = static_cast<TSStreamType>(streamType);
        stream->Descriptors = streamDescriptors;
        Streams.Set(stream->PID, stream);
    }
    if (!StreamDiagnostics.ContainsKey(streamPID))
    {
        StreamDiagnostics.Set(streamPID, std::make_shared<std::vector<TSStreamDiagnostics>>());
    }

    return stream;
}

} // namespace bdinfo
