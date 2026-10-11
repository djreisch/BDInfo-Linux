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

// Native port of the BDInfo BDROM engine (BDInfo's C# BDROM/*.cs sources).
// Class, member and method names follow the C# sources so the two can be read
// side by side.

#pragma once

#include "fs.h"
#include "net.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bdinfo {

// C# strings that may be null.
using NString = std::optional<std::string>;

// .NET BigInteger is only used for PTS/PCR arithmetic, which fits in 128 bits.
using BigInteger = __int128;

// ---------------------------------------------------------------------------
// Settings (BDInfoSettings); defaults match the Windows GUI.
// ---------------------------------------------------------------------------

namespace BDInfoSettings {
inline bool MainFormHRSizeFormat = true;
inline bool GenerateStreamDiagnostics = true;
inline bool ExtendedStreamDiagnostics = false;
inline bool EnableSSIF = true;
inline bool DisplayChapterCount = false;
inline bool FilterLoopingPlaylists = true;
inline bool FilterShortPlaylists = true;
inline int FilterShortPlaylistsValue = 20;
inline bool KeepStreamOrder = true;
inline bool GenerateTextSummary = true;
} // namespace BDInfoSettings

// ---------------------------------------------------------------------------
// TSStream.cs
// ---------------------------------------------------------------------------

enum class TSStreamType : uint8_t
{
    Unknown = 0,
    MPEG1_VIDEO = 0x01,
    MPEG2_VIDEO = 0x02,
    AVC_VIDEO = 0x1b,
    MVC_VIDEO = 0x20,
    HEVC_VIDEO = 0x24,
    VC1_VIDEO = 0xea,
    MPEG1_AUDIO = 0x03,
    MPEG2_AUDIO = 0x04,
    MPEG2_AAC_AUDIO = 0x0F,
    MPEG4_AAC_AUDIO = 0x11,
    LPCM_AUDIO = 0x80,
    AC3_AUDIO = 0x81,
    AC3_PLUS_AUDIO = 0x84,
    AC3_PLUS_SECONDARY_AUDIO = 0xA1,
    AC3_TRUE_HD_AUDIO = 0x83,
    DTS_AUDIO = 0x82,
    DTS_HD_AUDIO = 0x85,
    DTS_HD_SECONDARY_AUDIO = 0xA2,
    DTS_HD_MASTER_AUDIO = 0x86,
    PRESENTATION_GRAPHICS = 0x90,
    INTERACTIVE_GRAPHICS = 0x91,
    SUBTITLE = 0x92
};

enum class TSVideoFormat : uint8_t
{
    Unknown = 0,
    VIDEOFORMAT_480i = 1,
    VIDEOFORMAT_576i = 2,
    VIDEOFORMAT_480p = 3,
    VIDEOFORMAT_1080i = 4,
    VIDEOFORMAT_720p = 5,
    VIDEOFORMAT_1080p = 6,
    VIDEOFORMAT_576p = 7,
    VIDEOFORMAT_2160p = 8,
};

enum class TSFrameRate : uint8_t
{
    Unknown = 0,
    FRAMERATE_23_976 = 1,
    FRAMERATE_24 = 2,
    FRAMERATE_25 = 3,
    FRAMERATE_29_97 = 4,
    FRAMERATE_50 = 6,
    FRAMERATE_59_94 = 7
};

enum class TSChannelLayout : uint8_t
{
    Unknown = 0,
    CHANNELLAYOUT_MONO = 1,
    CHANNELLAYOUT_STEREO = 3,
    CHANNELLAYOUT_MULTI = 6,
    CHANNELLAYOUT_COMBO = 12
};

enum class TSSampleRate : uint8_t
{
    Unknown = 0,
    SAMPLERATE_48 = 1,
    SAMPLERATE_96 = 4,
    SAMPLERATE_192 = 5,
    SAMPLERATE_48_192 = 12,
    SAMPLERATE_48_96 = 14
};

enum class TSAspectRatio : int
{
    Unknown = 0,
    ASPECT_4_3 = 2,
    ASPECT_16_9 = 3,
    ASPECT_2_21 = 4
};

enum class TSAudioMode : int
{
    Unknown,
    DualMono,
    Stereo,
    Surround,
    Extended,
    JointStereo,
    Mono
};

struct TSDescriptor
{
    TSDescriptor(uint8_t name, uint8_t length) : Name(name), Value(length) {}
    uint8_t Name;
    std::vector<uint8_t> Value;
};

namespace LanguageCodes {
// Returns the code itself for unknown codes.
std::string GetName(const std::string& code);
}

class TSStream
{
public:
    virtual ~TSStream() = default;

    uint16_t PID = 0;
    TSStreamType StreamType = TSStreamType::Unknown;
    std::shared_ptr<std::vector<TSDescriptor>> Descriptors;
    int64_t BitRate = 0;
    int64_t ActiveBitRate = 0;
    bool IsVBR = false;
    bool IsInitialized = false;
    std::string LanguageName;
    bool IsHidden = false;

    uint64_t PayloadBytes = 0;
    uint64_t PacketCount = 0;
    double PacketSeconds = 0;
    int AngleIndex = 0;

    std::optional<bool> BaseView;

    uint64_t PacketSize() const { return PacketCount * 192; }

    const std::string& LanguageCode() const { return languageCode_; }
    void SetLanguageCode(const std::string& code)
    {
        languageCode_ = code;
        LanguageName = LanguageCodes::GetName(code);
    }

    bool IsVideoStream() const;
    bool IsAudioStream() const;
    bool IsGraphicsStream() const;
    bool IsTextStream() const;

    std::string CodecName() const;
    std::string CodecAltName() const;
    std::string CodecShortName() const;

    virtual std::string Description() const { return ""; }
    virtual std::shared_ptr<TSStream> Clone() const = 0;

protected:
    void CopyTo(TSStream& stream) const;

private:
    std::string languageCode_;
};

using TSStreamPtr = std::shared_ptr<TSStream>;

namespace hevc {

struct VideoParamSetStruct
{
    uint16_t VPSMaxSubLayers = 0;
};

struct XXLData
{
    uint64_t BitRateValue;
    uint64_t CPBSizeValue;
    bool CBRFlag;
};

struct XXL
{
    std::vector<XXLData> SchedSel;
};

struct XXLCommon
{
    bool SubPicHRDParamsPresentFlag = false;
    uint16_t DuCPBRemovalDelayIncrementLengthMinus1 = 0;
    uint16_t DPBOutputDelayDULengthMinus1 = 0;
    uint16_t InitialCPBRemovalDelayLengthMinus1 = 0;
    uint16_t AUCPBRemovalDelayLengthMinus1 = 0;
    uint16_t DPBOutputDelayLengthMinus1 = 0;
};

struct VUIParametersStruct
{
    XXL NAL;
    XXL VCL;
    XXLCommon XXLCommon_;
    uint32_t NumUnitsInTick = 0;
    uint32_t TimeScale = 0;
    uint16_t SarWidth = 0;
    uint16_t SarHeight = 0;
    uint8_t AspectRatioIDC = 0;
    uint8_t VideoFormat = 0;
    uint8_t VideoFullRangeFlag = 0;
    uint8_t ColourPrimaries = 0;
    uint8_t TransferCharacteristics = 0;
    uint8_t MatrixCoefficients = 0;
    bool AspectRatioInfoPresentFlag = false;
    bool VideoSignalTypePresentFlag = false;
    bool FrameFieldInfoPresentFlag = false;
    bool ColourDescriptionPresentFlag = false;
    bool TimingInfoPresentFlag = false;
};

struct SeqParameterSetStruct
{
    VUIParametersStruct VUIParameters;
    uint32_t ProfileSpace = 0;
    bool TierFlag = false;
    uint32_t ProfileIDC = 0;
    uint32_t LevelIDC = 0;
    uint32_t PicWidthInLumaSamples = 0;
    uint32_t PicHeightInLumaSamples = 0;
    uint32_t ConfWinLeftOffset = 0;
    uint32_t ConfWinRightOffset = 0;
    uint32_t ConfWinTopOffset = 0;
    uint32_t ConfWinBottomOffset = 0;
    uint8_t VideoParameterSetID = 0;
    uint8_t ChromaFormatIDC = 0;
    bool SeparateColourPlaneFlag = false;
    uint8_t Log2MaxPicOrderCntLsbMinus4 = 0;
    uint8_t BitDepthLumaMinus8 = 0;
    uint8_t BitDepthChromaMinus8 = 0;
    bool GeneralProgressiveSourceFlag = false;
    bool GeneralInterlacedSourceFlag = false;
    bool GeneralFrameOnlyConstraintFlag = false;

    // The VUI parameter objects are never null in the C# code.
    bool NalHrdBpPresentFlag() const { return true; }
    bool VclHrdPbPresentFlag() const { return true; }
    bool CpbDpbDelaysPresentFlag() const { return true; }
};

struct PicParameterSetStruct
{
    uint8_t SeqParameterSetID = 0;
    uint8_t NumRefIdxL0DefaultActiveMinus1 = 0;
    uint8_t NumRefIdxL1DefaultActiveMinus1 = 0;
    uint8_t NumExtraSliceHeaderBits = 0;
    bool DependentSliceSegmentsEnabledFlag = false;
};

struct ExtendedDataSet
{
    std::vector<VideoParamSetStruct> VideoParamSets;
    std::vector<VUIParametersStruct> VUIParameterSets;
    std::vector<SeqParameterSetStruct> SeqParameterSets;
    std::vector<PicParameterSetStruct> PicParameterSets;

    std::string MasteringDisplayColorPrimaries;
    std::string MasteringDisplayLuminance;
    uint32_t MaximumContentLightLevel = 0;
    uint32_t MaximumFrameAverageLightLevel = 0;

    bool LightLevelAvailable = false;

    std::vector<std::string> ExtendedFormatInfo;

    uint8_t PreferredTransferCharacteristics = 2;

    bool IsHdr10Plus = false;
};

} // namespace hevc

class TSVideoStream : public TSStream
{
public:
    int Width = 0;
    int Height = 0;
    bool IsInterlaced = false;
    int FrameRateEnumerator = 0;
    int FrameRateDenominator = 0;
    TSAspectRatio AspectRatio = TSAspectRatio::Unknown;
    NString EncodingProfile;

    std::shared_ptr<hevc::ExtendedDataSet> ExtendedData;

    TSVideoFormat VideoFormat() const { return videoFormat_; }
    void SetVideoFormat(TSVideoFormat value);
    TSFrameRate FrameRate() const { return frameRate_; }
    void SetFrameRate(TSFrameRate value);

    std::string Description() const override;
    TSStreamPtr Clone() const override;

private:
    TSVideoFormat videoFormat_ = TSVideoFormat::Unknown;
    TSFrameRate frameRate_ = TSFrameRate::Unknown;
};

class TSAudioStream : public TSStream
{
public:
    int SampleRate = 0;
    int ChannelCount = 0;
    int BitDepth = 0;
    int LFE = 0;
    int DialNorm = 0;

    bool HasExtensions = false;

    NString ExtendedData;

    TSAudioMode AudioMode = TSAudioMode::Unknown;
    std::shared_ptr<TSAudioStream> CoreStream;
    TSChannelLayout ChannelLayout = TSChannelLayout::Unknown;

    static int ConvertSampleRate(TSSampleRate sampleRate);

    std::string ChannelDescription() const;
    std::string Description() const override;
    TSStreamPtr Clone() const override;
};

struct PGSFrame
{
    bool Started = false;
    bool Forced = false;
    bool Finished = false;
};

class TSGraphicsStream : public TSStream
{
public:
    TSGraphicsStream();

    int Width = 0;
    int Height = 0;
    int Captions = 0;
    int ForcedCaptions = 0;
    std::shared_ptr<net::Dictionary<int, PGSFrame>> CaptionIDs;
    PGSFrame LastFrame;

    std::string Description() const override;
    TSStreamPtr Clone() const override;
};

class TSTextStream : public TSStream
{
public:
    TSTextStream();
    TSStreamPtr Clone() const override;
};

// ---------------------------------------------------------------------------
// TSStreamBuffer.cs (Release build semantics over a 5 MB MemoryStream)
// ---------------------------------------------------------------------------

class TSStreamBuffer
{
public:
    TSStreamBuffer();

    int TransferLength = 0;

    int64_t Length() const { return bufferLength_; }
    int64_t Position() const { return position_; }

    void Add(const uint8_t* buffer, int offset, int length);
    void Seek(int64_t offset, bool fromCurrent);
    void Reset();
    void BeginRead();
    void EndRead() {}

    // Returns false where the C# method returns null.
    bool ReadBytes(int bytes, std::vector<uint8_t>& value);
    uint8_t ReadByte(bool skipH26xEmulationByte = false);
    bool ReadBool(bool skipH26xEmulationByte = false);
    uint16_t ReadBits2(int bits, bool skipH26xEmulationByte = false);
    uint32_t ReadBits4(int bits, bool skipH26xEmulationByte = false);
    uint64_t ReadBits8(int bits, bool skipH26xEmulationByte = false);
    void BSSkipBits(int bits, bool skipH26xEmulationByte = false);
    void BSSkipNextByte();
    void BSResetBits();
    void BSSkipBytes(int bytes, bool skipH26xEmulationByte = false);
    uint32_t ReadExp(bool skipH26xEmulationByte = false);
    void SkipExp(bool skipH26xEmulationByte = false);
    void SkipExpMulti(int num, bool skipH26xEmulationByte = false);
    int64_t DataBitStreamRemain() const;
    int64_t DataBitStreamRemainBytes() const;

private:
    static constexpr int64_t Capacity = 5242880;

    int StreamReadByte();

    struct FreeDeleter
    {
        void operator()(uint8_t* p) const { std::free(p); }
    };
    std::unique_ptr<uint8_t, FreeDeleter> buffer_;
    int64_t position_ = 0;
    int skipBits_ = 0;
    int skippedBytes_ = 0;
    int bufferLength_ = 0;
};

// ---------------------------------------------------------------------------
// Codecs (TSCodec*.cs)
// ---------------------------------------------------------------------------

namespace TSCodecAAC { void Scan(TSAudioStream& stream, TSStreamBuffer& buffer, NString& tag); }
namespace TSCodecAC3 { void Scan(TSAudioStream& stream, TSStreamBuffer& buffer, NString& tag); }
namespace TSCodecAVC { void Scan(TSVideoStream& stream, TSStreamBuffer& buffer, NString& tag); }
namespace TSCodecDTS { void Scan(TSAudioStream& stream, TSStreamBuffer& buffer, int64_t bitrate, NString& tag); }
namespace TSCodecDTSHD { void Scan(TSAudioStream& stream, TSStreamBuffer& buffer, int64_t bitrate, NString& tag); }
namespace TSCodecHEVC { void Scan(TSVideoStream& stream, TSStreamBuffer& buffer, NString& tag); }
namespace TSCodecLPCM { void Scan(TSAudioStream& stream, TSStreamBuffer& buffer, NString& tag); }
namespace TSCodecMPA { void Scan(TSAudioStream& stream, TSStreamBuffer& buffer, NString& tag); }
namespace TSCodecMPEG2 { void Scan(TSVideoStream& stream, TSStreamBuffer& buffer, NString& tag); }
namespace TSCodecMVC { void Scan(TSVideoStream& stream, TSStreamBuffer& buffer, NString& tag); }
namespace TSCodecPGS { void Scan(TSGraphicsStream& stream, TSStreamBuffer& buffer, NString& tag); }
namespace TSCodecTrueHD { void Scan(TSAudioStream& stream, TSStreamBuffer& buffer, NString& tag); }
namespace TSCodecVC1 { void Scan(TSVideoStream& stream, TSStreamBuffer& buffer, NString& tag); }

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

class BDROM;
class TSPlaylistFile;
class TSStreamFile;
class TSStreamClipFile;

using TSPlaylistFilePtr = std::shared_ptr<TSPlaylistFile>;
using TSStreamFilePtr = std::shared_ptr<TSStreamFile>;
using TSStreamClipFilePtr = std::shared_ptr<TSStreamClipFile>;

// TSInterleavedFile.cs
class TSInterleavedFile
{
public:
    explicit TSInterleavedFile(FileRefPtr file) : File(std::move(file)), Name(net::ToUpper(File->Name)) {}

    FileRefPtr File;
    std::string Name;
};

using TSInterleavedFilePtr = std::shared_ptr<TSInterleavedFile>;

// TSStreamClipFile.cs
class TSStreamClipFile
{
public:
    explicit TSStreamClipFile(FileRefPtr file) : File(std::move(file)), Name(net::ToUpper(File->Name)) {}

    FileRefPtr File;
    std::string FileType;
    bool IsValid = false;
    std::string Name;

    net::Dictionary<uint16_t, TSStreamPtr> Streams;

    void Scan();
};

struct TSStreamDiagnostics
{
    uint64_t Bytes = 0;
    uint64_t Packets = 0;
    double Marker = 0;
    double Interval = 0;
    NString Tag;
};

struct TSStreamState;

// TSStreamFile.cs
class TSStreamFile
{
public:
    explicit TSStreamFile(FileRefPtr file) : File(std::move(file)), Name(net::ToUpper(File->Name)) {}

    FileRefPtr File;
    std::string Name;
    // Updated during a scan and read by the progress display thread.
    std::atomic<int64_t> Size{0};
    double Length = 0;

    TSInterleavedFilePtr InterleavedFile;

    net::Dictionary<uint16_t, TSStreamPtr> Streams;
    net::Dictionary<uint16_t, std::shared_ptr<std::vector<TSStreamDiagnostics>>> StreamDiagnostics;

    std::string DisplayName() const;

    void Scan(const std::vector<TSPlaylistFile*>& playlists, bool isFullScan);

private:
    bool ScanStream(TSStream& stream, TSStreamState& streamState, TSStreamBuffer& buffer, bool isFullScan);
    void UpdateStreamBitrates(uint16_t PTSPID, BigInteger PTS, BigInteger PTSDiff);
    void UpdateStreamBitrate(uint16_t PID, uint16_t PTSPID, BigInteger PTS, BigInteger PTSDiff);
    TSStreamPtr CreateStream(uint16_t streamPID, uint8_t streamType,
                             const std::shared_ptr<std::vector<TSDescriptor>>& streamDescriptors);

    net::Dictionary<uint16_t, std::shared_ptr<TSStreamState>> StreamStates;
    std::vector<TSPlaylistFile*> Playlists;
    bool hasPlaylists_ = false;
};

// TSStreamClip.cs
class TSStreamClip
{
public:
    TSStreamClip(const TSStreamFilePtr& streamFile, const TSStreamClipFilePtr& streamClipFile);

    int AngleIndex = 0;
    std::string Name;
    double TimeIn = 0;
    double TimeOut = 0;
    double RelativeTimeIn = 0;
    double RelativeTimeOut = 0;
    double Length = 0;
    double RelativeLength = 0;

    uint64_t FileSize = 0;
    uint64_t InterleavedFileSize = 0;
    uint64_t PayloadBytes = 0;
    uint64_t PacketCount = 0;
    double PacketSeconds = 0;

    std::vector<double> Chapters;

    TSStreamFilePtr StreamFile;
    TSStreamClipFilePtr StreamClipFile;

    std::string DisplayName() const;
    uint64_t PacketSize() const { return PacketCount * 192; }
    uint64_t PacketBitRate() const;
};

using TSStreamClipPtr = std::shared_ptr<TSStreamClip>;

// TSPlaylistFile.cs
class TSPlaylistFile
{
public:
    TSPlaylistFile(BDROM* bdrom, FileRefPtr file) : File(std::move(file)), Name(net::ToUpper(File->Name)), BDROM_(bdrom) {}

    FileRefPtr File;
    std::string FileType;
    bool IsInitialized = false;
    std::string Name;
    BDROM* BDROM_ = nullptr;
    bool HasHiddenTracks = false;
    bool HasLoops = false;
    bool IsCustom = false;

    bool MVCBaseViewR = false;

    std::vector<double> Chapters;

    net::Dictionary<uint16_t, TSStreamPtr> Streams;
    net::Dictionary<uint16_t, TSStreamPtr> PlaylistStreams;
    std::vector<TSStreamClipPtr> StreamClips;
    std::vector<net::Dictionary<uint16_t, TSStreamPtr>> AngleStreams;
    std::vector<net::Dictionary<double, TSStreamClipPtr>> AngleClips;
    int AngleCount = 0;

    std::vector<TSStreamPtr> SortedStreams;
    std::vector<std::shared_ptr<TSVideoStream>> VideoStreams;
    std::vector<std::shared_ptr<TSAudioStream>> AudioStreams;
    std::vector<std::shared_ptr<TSTextStream>> TextStreams;
    std::vector<std::shared_ptr<TSGraphicsStream>> GraphicsStreams;

    uint64_t InterleavedFileSize() const;
    uint64_t FileSize() const;
    double TotalLength() const;
    double TotalAngleLength() const;
    uint64_t TotalSize() const;
    uint64_t TotalAngleSize() const;
    uint64_t TotalBitRate() const;
    uint64_t TotalAngleBitRate() const;

    void Scan(net::Dictionary<std::string, TSStreamFilePtr>& streamFiles,
              net::Dictionary<std::string, TSStreamClipFilePtr>& streamClipFiles);
    void Initialize();
    void ClearBitrates();
    bool IsValid() const;

private:
    TSStreamPtr CreatePlaylistStream(std::vector<uint8_t>& data, int& pos);
    void LoadStreamClips();
};

// BDROM.cs
class BDROM
{
public:
    explicit BDROM(const std::string& path);
    ~BDROM();

    // Local folders (empty when absent) ...
    std::string DirectoryRoot, DirectoryBDMV, DirectoryBDJO, DirectoryCLIPINF, DirectoryPLAYLIST, DirectorySNP,
        DirectorySSIF, DirectorySTREAM, DirectoryMETA;
    // ... or directories inside a disc image (null when absent).
    UdfNodePtr DiscDirectoryRoot, DiscDirectoryBDMV, DiscDirectoryBDJO, DiscDirectoryCLIPINF, DiscDirectoryPLAYLIST,
        DiscDirectorySNP, DiscDirectorySSIF, DiscDirectorySTREAM, DiscDirectoryMETA;

    std::string VolumeLabel;
    std::string DiscTitle; // empty when absent
    uint64_t Size = 0;
    bool IsBDPlus = false;
    bool IsBDJava = false;
    bool IsDBOX = false;
    bool IsPSP = false;
    bool Is3D = false;
    bool Is50Hz = false;
    bool IsUHD = false;

    bool IsImage = false;
    std::unique_ptr<UdfReader> CdReader;

    net::Dictionary<std::string, TSPlaylistFilePtr> PlaylistFiles;
    net::Dictionary<std::string, TSStreamClipFilePtr> StreamClipFiles;
    net::Dictionary<std::string, TSStreamFilePtr> StreamFiles;
    net::Dictionary<std::string, TSInterleavedFilePtr> InterleavedFiles;

    // Scan error handlers: return true to continue with the next file.
    std::function<bool(TSStreamClipFile&, const std::exception&)> StreamClipFileScanError;
    std::function<bool(TSStreamFile&, const std::exception&)> StreamFileScanError;
    std::function<bool(TSPlaylistFile&, const std::exception&)> PlaylistFileScanError;
    std::function<void(TSStreamFile&, int, int)> StreamFileScanStarted;

    void Scan();
    void CloseDiscImage();

    static int CompareStreamFiles(const TSStreamFilePtr& x, const TSStreamFilePtr& y);

private:
    void ReadIndexVersion(InputStream& stream);
    void ReadDiscTitle(InputStream& stream);
};

// ---------------------------------------------------------------------------
// Scan result and report generation (FormMain.cs / FormReport.cs)
// ---------------------------------------------------------------------------

struct ScanBDROMResult
{
    // Null when the scan succeeded.
    NString ScanException = std::string("Scan has not been run.");
    net::Dictionary<std::string, std::string> FileExceptions;
};

namespace ReportGenerator {
void Generate(BDROM& bdrom, const std::vector<TSPlaylistFilePtr>& playlists, const ScanBDROMResult& scanResult,
              const std::string& productVersion, const std::function<void(const std::string&)>& output);
}

namespace ToolBox {
std::string FormatFileSize(double fSize);
std::string ReadString(const std::vector<uint8_t>& data, int count, int& pos);
std::string GetSafeFileName(const std::string& fileName);
} // namespace ToolBox

} // namespace bdinfo
