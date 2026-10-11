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

// TSCodecHEVC.cs. The C# parser keeps its state in static fields (some of
// which, like IsHdr10Plus, carry over between streams); the globals below
// mirror them one to one.

#include "bdinfo.h"

#include <cmath>

namespace bdinfo {
namespace TSCodecHEVC {

using namespace hevc;

namespace {

struct MasteringDisplayColorVolumeValue
{
    uint8_t Code;
    uint16_t Values[8];
};

const MasteringDisplayColorVolumeValue MasteringDisplayColorVolumeValues[] = {
    {1, {15000, 30000, 7500, 3000, 32000, 16500, 15635, 16450}},  // BT.709
    {9, {8500, 39850, 6550, 2300, 35400, 14600, 15635, 16450}},   // BT.2020
    {11, {13250, 34500, 7500, 3000, 34000, 16000, 15700, 17550}}, // DCI P3
    {12, {13250, 34500, 7500, 3000, 34000, 16000, 15635, 16450}}, // Display P3
};

std::string ColourPrimaries(uint8_t colourPrimaries)
{
    switch (colourPrimaries)
    {
        case 1: return "BT.709";
        case 4: return "BT.470 System M";
        case 5: return "BT.601 PAL";
        case 6: return "BT.601 NTSC";
        case 7: return "SMPTE 240M";
        case 8: return "Generic film";
        case 9: return "BT.2020";
        case 10: return "XYZ";
        case 11: return "DCI P3";
        case 12: return "Display P3";
        case 22: return "EBU Tech 3213";
        default: return "";
    }
}

std::string TransferCharacteristics(uint8_t transferCharacteristics)
{
    switch (transferCharacteristics)
    {
        case 1: return "BT.709";
        case 4: return "BT.470 System M";
        case 5: return "BT.470 System B/G";
        case 6: return "BT.601";
        case 7: return "SMPTE 240M";
        case 8: return "Linear";
        case 9: return "Logarithmic (100:1)";
        case 10: return "Logarithmic (316.22777:1)";
        case 11: return "xvYCC";
        case 12: return "BT.1361";
        case 13: return "sRGB/sYCC";
        case 14: return "BT.2020 (10-bit)";
        case 15: return "BT.2020 (12-bit)";
        case 16: return "PQ";
        case 17: return "SMPTE 428M";
        case 18: return "HLG";
        default: return "";
    }
}

std::string MatrixCoefficients(uint8_t matrixCoefficients)
{
    switch (matrixCoefficients)
    {
        case 0: return "Identity";
        case 1: return "BT.709";
        case 4: return "FCC 73.682";
        case 5: return "BT.470 System B/G";
        case 6: return "BT.601";
        case 7: return "SMPTE 240M";
        case 8: return "YCgCo";
        case 9: return "BT.2020 non-constant";
        case 10: return "BT.2020 constant";
        case 11: return "Y'D'zD'x";
        case 12: return "Chromaticity-derived non-constant";
        case 13: return "Chromaticity-derived constant";
        case 14: return "ICtCp";
        default: return "";
    }
}

// Static state of TSCodecHEVC.
uint32_t _profileSpace;
bool _tierFlag;
uint16_t _profileIDC;
uint16_t _levelIDC;
bool _generalProgressiveSourceFlag;
bool _generalInterlacedSourceFlag;
bool _generalFrameOnlyConstraintFlag;

std::vector<VideoParamSetStruct>* VideoParamSets;
std::vector<SeqParameterSetStruct>* SeqParameterSets;
std::vector<PicParameterSetStruct>* PicParameterSets;

std::string MasteringDisplayColorPrimaries;
std::string MasteringDisplayLuminance;
uint32_t MaximumContentLightLevel;
uint32_t MaximumFrameAverageLightLevel;

std::vector<std::string>* ExtendedFormatInfo;
bool LightLevelAvailable;

uint8_t PreferredTransferCharacteristics;

bool IsHdr10Plus;

bool _firstSliceSegmentInPicFlag;
uint32_t _slicePicParameterSetId;

bool _isInitialized;

void ProfileTierLevel(TSStreamBuffer& buffer, uint32_t subLayerCount)
{
    _profileSpace = buffer.ReadBits2(2, true);
    _tierFlag = buffer.ReadBool(true);
    _profileIDC = buffer.ReadBits2(5, true);

    buffer.BSSkipBits(32, true); // general_profile_compatibility_flags

    _generalProgressiveSourceFlag = buffer.ReadBool(true);
    _generalInterlacedSourceFlag = buffer.ReadBool(true);
    buffer.BSSkipBits(1, true); // general_non_packed_constraint_flag
    _generalFrameOnlyConstraintFlag = buffer.ReadBool(true);

    buffer.BSSkipBits(44, true); // general_reserved_zero_44bits

    _levelIDC = buffer.ReadBits2(8, true);

    std::vector<bool> subLayerProfilePresentFlags;
    std::vector<bool> subLayerLevelPresentFlags;

    for (int64_t subLayerPos = 0; subLayerPos < subLayerCount; subLayerPos++)
    {
        bool subLayerProfilePresentFlag = buffer.ReadBool(true);
        bool subLayerLevelPresentFlag = buffer.ReadBool(true);

        subLayerProfilePresentFlags.push_back(subLayerProfilePresentFlag);
        subLayerLevelPresentFlags.push_back(subLayerLevelPresentFlag);
    }

    if (subLayerCount > 0)
    {
        buffer.BSSkipBits(2, true); // reserved_zero_2bits
    }

    for (int64_t subLayerPos = 0; subLayerPos < subLayerCount; subLayerPos++)
    {
        if (subLayerProfilePresentFlags[static_cast<size_t>(subLayerPos)])
        {
            buffer.BSSkipBits(88, true); // sub layer profile data
        }
        if (subLayerLevelPresentFlags[static_cast<size_t>(subLayerPos)])
        {
            buffer.BSSkipBits(8, true); // sub_layer_level_idc
        }
    }
}

void SubLayerHrdParameters(TSStreamBuffer& buffer, const XXLCommon& xxlCommon, uint8_t bitRateScale,
                           uint8_t cpbSizeScale, uint32_t cpbCntMinus1, XXL& hrdParametersItem)
{
    std::vector<XXLData> schedSel;
    for (uint8_t schedSelIdx = 0; schedSelIdx <= cpbCntMinus1; ++schedSelIdx)
    {
        uint32_t bitRateValueMinus1 = buffer.ReadExp(true);
        uint64_t bitRateValue = net::ToUInt64((bitRateValueMinus1 + 1) * std::pow(2.0, 6 + bitRateScale));
        uint32_t cpbSizeValueMinus1 = buffer.ReadExp(true);
        uint64_t cpbSizeValue = net::ToUInt64((cpbSizeValueMinus1 + 1) * std::pow(2.0, 4 + cpbSizeScale));
        if (xxlCommon.SubPicHRDParamsPresentFlag)
        {
            buffer.SkipExpMulti(2, true); // cpb_size_du_value_minus1, bit_rate_du_value_minus1
        }
        bool cbrFlag = buffer.ReadBool(true);
        schedSel.push_back(XXLData{bitRateValue, cpbSizeValue, cbrFlag});
    }

    hrdParametersItem = XXL{schedSel};
}

void HRDParameters(TSStreamBuffer& buffer, bool commonInfPresentFlag, uint32_t maxNumSubLayersMinus1,
                   XXLCommon& xxlCommon, XXL& nal, XXL& vcl)
{
    uint8_t bitRateScale = 0, cpbSizeScale = 0, duCPBRemovalDelayIncrementLengthMinus1 = 0;
    uint8_t dpbOutputDelayDULengthMinus1 = 0, initialCPBRemovalDelayLengthMinus1 = 0;
    uint8_t auCPBRemovalDelayLengthMinus1 = 0, dpbOutputDelayLengthMinus1 = 0;
    bool nalHRDParametersPresentFlag = false, vclHRDParametersPresentFlag = false;
    bool subPicHRDParamsPresentFlag = false;

    if (commonInfPresentFlag)
    {
        nalHRDParametersPresentFlag = buffer.ReadBool(true);
        vclHRDParametersPresentFlag = buffer.ReadBool(true);
        if (nalHRDParametersPresentFlag || vclHRDParametersPresentFlag)
        {
            subPicHRDParamsPresentFlag = buffer.ReadBool(true);
            if (subPicHRDParamsPresentFlag)
            {
                buffer.BSSkipBits(8, true); // tick_divisor_minus2
                duCPBRemovalDelayIncrementLengthMinus1 = static_cast<uint8_t>(buffer.ReadBits2(5, true));
                buffer.BSSkipBits(1, true); // sub_pic_cpb_params_in_pic_timing_sei_flag
                dpbOutputDelayDULengthMinus1 = static_cast<uint8_t>(buffer.ReadBits2(5, true));
            }
            bitRateScale = static_cast<uint8_t>(buffer.ReadBits2(4, true));
            cpbSizeScale = static_cast<uint8_t>(buffer.ReadBits2(4, true));
            if (subPicHRDParamsPresentFlag) buffer.BSSkipBits(4, true); // cpb_size_du_scale
            initialCPBRemovalDelayLengthMinus1 = static_cast<uint8_t>(buffer.ReadBits2(5, true));
            auCPBRemovalDelayLengthMinus1 = static_cast<uint8_t>(buffer.ReadBits2(5, true));
            dpbOutputDelayLengthMinus1 = static_cast<uint8_t>(buffer.ReadBits2(5, true));
        }
    }

    // `byte numSubLayer` wraps at 256, exactly like the C# loop.
    for (uint8_t numSubLayer = 0; numSubLayer <= maxNumSubLayersMinus1; numSubLayer++)
    {
        uint32_t cpbCntMinus1 = 0;
        bool fixedPicRateWithinCvsFlag = true, lowDelayHRDFlag = false;
        bool fixedPicRateGeneralFlag = buffer.ReadBool(true);
        if (!fixedPicRateGeneralFlag) fixedPicRateWithinCvsFlag = buffer.ReadBool(true);
        if (fixedPicRateWithinCvsFlag)
            buffer.SkipExp(true); // elemental_duration_in_tc_minus1
        else
            lowDelayHRDFlag = buffer.ReadBool(true);
        if (!lowDelayHRDFlag)
        {
            cpbCntMinus1 = buffer.ReadExp(true);
            if (cpbCntMinus1 > 31)
            {
                return;
            }
        }
        if (nalHRDParametersPresentFlag || vclHRDParametersPresentFlag)
        {
            xxlCommon = XXLCommon{subPicHRDParamsPresentFlag,       duCPBRemovalDelayIncrementLengthMinus1,
                                  dpbOutputDelayDULengthMinus1,     initialCPBRemovalDelayLengthMinus1,
                                  auCPBRemovalDelayLengthMinus1,    dpbOutputDelayLengthMinus1};
        }
        if (nalHRDParametersPresentFlag)
        {
            SubLayerHrdParameters(buffer, xxlCommon, bitRateScale, cpbSizeScale, cpbCntMinus1, nal);
        }
        if (vclHRDParametersPresentFlag)
        {
            SubLayerHrdParameters(buffer, xxlCommon, bitRateScale, cpbSizeScale, cpbCntMinus1, vcl);
        }
    }
}

void VUIParameters(TSStreamBuffer& buffer, const VideoParamSetStruct& videoParamSetItem,
                   VUIParametersStruct& vuiParametersItem)
{
    XXLCommon xxlCommon;
    XXL nal, vcl;

    uint32_t numUnitsInTick = UINT32_MAX, timeScale = UINT32_MAX;
    uint16_t sarWidth = UINT16_MAX, sarHeight = UINT16_MAX;
    uint8_t aspectRatioIDC = 0, videoFormat = 5, videoFullRangeFlag = 0, colourPrimaries = 2,
            transferCharacteristics = 2, matrixCoefficients = 2;
    bool colourDescriptionPresentFlag = false;

    bool aspectRatioInfoPresentFlag = buffer.ReadBool(true);
    if (aspectRatioInfoPresentFlag)
    {
        aspectRatioIDC = static_cast<uint8_t>(buffer.ReadBits2(8, true));
        if (aspectRatioIDC == 0xFF)
        {
            sarWidth = static_cast<uint16_t>(buffer.ReadBits4(16, true));
            sarHeight = static_cast<uint16_t>(buffer.ReadBits4(16, true));
        }
    }
    if (buffer.ReadBool(true)) // overscan_info_present_flag
        buffer.BSSkipBits(1, true);
    bool videoSignalTypePresentFlag = buffer.ReadBool(true);
    if (videoSignalTypePresentFlag)
    {
        videoFormat = static_cast<uint8_t>(buffer.ReadBits2(3, true));
        videoFullRangeFlag = static_cast<uint8_t>(buffer.ReadBits2(1, true));
        colourDescriptionPresentFlag = buffer.ReadBool(true);
        if (colourDescriptionPresentFlag)
        {
            colourPrimaries = static_cast<uint8_t>(buffer.ReadBits2(8, true));
            transferCharacteristics = static_cast<uint8_t>(buffer.ReadBits2(8, true));
            matrixCoefficients = static_cast<uint8_t>(buffer.ReadBits2(8, true));
        }
    }
    if (buffer.ReadBool(true)) // chroma_loc_info_present_flag
    {
        buffer.ReadExp(true); // _chromaSampleLocTypeTopField
        buffer.ReadExp(true); // _chromaSampleLocTypeBottomField
    }
    buffer.BSSkipBits(2, true);
    bool frameFieldInfoPresentFlag = buffer.ReadBool(true);
    if (buffer.ReadBool(true)) // default_display_window_flag
    {
        buffer.SkipExpMulti(4, true);
    }
    bool timingInfoPresentFlag = buffer.ReadBool(true);
    if (timingInfoPresentFlag)
    {
        numUnitsInTick = static_cast<uint32_t>(buffer.ReadBits8(32, true));
        timeScale = static_cast<uint32_t>(buffer.ReadBits8(32, true));
        if (buffer.ReadBool(true)) // vui_poc_proportional_to_timing_flag
        {
            buffer.SkipExp(true);
        }
        if (buffer.ReadBool(true)) // hrd_parameters_present_flag
        {
            HRDParameters(buffer, true, videoParamSetItem.VPSMaxSubLayers, xxlCommon, nal, vcl);
        }
    }
    if (buffer.ReadBool(true)) // bitstream_restriction_flag
    {
        buffer.BSSkipBits(3, true);
        buffer.SkipExpMulti(5, true);
    }

    vuiParametersItem = VUIParametersStruct{nal,
                                            vcl,
                                            xxlCommon,
                                            numUnitsInTick,
                                            timeScale,
                                            sarWidth,
                                            sarHeight,
                                            aspectRatioIDC,
                                            videoFormat,
                                            videoFullRangeFlag,
                                            colourPrimaries,
                                            transferCharacteristics,
                                            matrixCoefficients,
                                            aspectRatioInfoPresentFlag,
                                            videoSignalTypePresentFlag,
                                            frameFieldInfoPresentFlag,
                                            colourDescriptionPresentFlag,
                                            timingInfoPresentFlag};
}

void ShortTermRefPicSets(TSStreamBuffer& buffer, uint32_t numShortTermRefPicSets)
{
    uint32_t numPics = 0;
    // `int stRpsIdx < uint` compares as long in C#; the int wraps.
    for (int32_t stRpsIdx = 0; static_cast<int64_t>(stRpsIdx) < numShortTermRefPicSets; stRpsIdx++)
    {
        bool interRefPicSetPredictionFlag = false;
        if (stRpsIdx > 0) interRefPicSetPredictionFlag = buffer.ReadBool(true);

        if (interRefPicSetPredictionFlag)
        {
            uint32_t deltaIdxMinus1 = 0;
            if (static_cast<int64_t>(stRpsIdx) == numShortTermRefPicSets) deltaIdxMinus1 = buffer.ReadExp(true);
            if (static_cast<int64_t>(static_cast<uint32_t>(deltaIdxMinus1 + 1)) > stRpsIdx)
            {
                return;
            }

            buffer.BSSkipBits(1, true);
            buffer.SkipExp(true);

            uint32_t numPicsNew = 0;
            for (uint32_t picPos = 0; picPos <= numPics; picPos++)
            {
                if (buffer.ReadBool(true)) // used_by_curr_pic_flag
                    numPicsNew++;
                else
                {
                    if (buffer.ReadBool(true)) // use_delta_flag
                        numPicsNew++;
                }
            }
            numPics = numPicsNew;
        }
        else
        {
            uint32_t numNegativePics = buffer.ReadExp(true);
            uint32_t numPositivePics = buffer.ReadExp(true);
            numPics = numNegativePics + numPositivePics;
            for (int32_t i = 0; static_cast<int64_t>(i) < numNegativePics; i++)
            {
                buffer.SkipExp(true);
                buffer.BSSkipBits(1, true);
            }
            for (int32_t i = 0; static_cast<int64_t>(i) < numPositivePics; i++)
            {
                buffer.SkipExp(true);
                buffer.BSSkipBits(1, true);
            }
        }
    }
}

void ScalingListData(TSStreamBuffer& buffer)
{
    for (int sizeId = 0; sizeId < 4; sizeId++)
    {
        for (int matrixId = 0; matrixId < ((sizeId == 3) ? 2 : 6); matrixId++)
        {
            if (!buffer.ReadBool(true)) // scaling_list_pred_mode_flag
                buffer.SkipExp(true);
            else
            {
                int coefNum = std::min(64, (1 << (4 + (sizeId << 1))));
                if (sizeId > 1) buffer.SkipExp(true);
                for (int i = 0; i < coefNum; i++) buffer.SkipExp(true);
            }
        }
    }
}

std::optional<std::string> SliceSegmentLayer(TSStreamBuffer& buffer, int64_t nalUnitType)
{
    std::optional<std::string> tag;

    // slice headers
    bool dependentSliceSegmentFlag = false;

    _firstSliceSegmentInPicFlag = buffer.ReadBool();

    if (nalUnitType >= 16 && nalUnitType <= 23) buffer.ReadBool(); // no_output_of_prior_pics_flag

    _slicePicParameterSetId = buffer.ReadExp(true);

    if (_slicePicParameterSetId >= PicParameterSets->size())
    {
        _slicePicParameterSetId = UINT32_MAX;
        return tag;
    }

    if (!_firstSliceSegmentInPicFlag)
    {
        if (net::L(*PicParameterSets, _slicePicParameterSetId).DependentSliceSegmentsEnabledFlag)
        {
            dependentSliceSegmentFlag = buffer.ReadBool(true);
        }
        return tag;
    }

    if (!dependentSliceSegmentFlag)
    {
        buffer.BSSkipBits(net::L(*PicParameterSets, _slicePicParameterSetId).NumExtraSliceHeaderBits);
        uint32_t sliceType = buffer.ReadExp(true);
        switch (sliceType)
        {
            case 0: tag = "P"; break;
            case 1: tag = "B"; break;
            case 2: tag = "I"; break;
            default: break;
        }
    }
    return tag;
}

// packet 32
void VideoParameterSet(TSStreamBuffer& buffer)
{
    if (_isInitialized) return;

    int vpsVideoParameterSetID = buffer.ReadBits2(4, true);

    buffer.BSSkipBits(8, true); // vps_reserved_three_2bits, vps_reserved_zero_6bits

    uint16_t maxSubLayers = buffer.ReadBits2(3, true); // vps_max_sub_layers_minus1

    buffer.BSSkipBits(17, true); // vps_temporal_id_nesting_flag, vps_reserved_0xffff_16bits

    ProfileTierLevel(buffer, maxSubLayers);

    bool tempB = buffer.ReadBool(true); // vps_sub_layer_ordering_info_present_flag
    for (int subLayerPos = (tempB ? 0 : maxSubLayers); subLayerPos <= maxSubLayers; subLayerPos++)
    {
        buffer.SkipExpMulti(3, true);
    }

    uint16_t vpsMaxLayerID = buffer.ReadBits2(6, true);
    uint32_t vpsNumLayerSetsMinus1 = buffer.ReadExp(true); // vps_num_layer_sets_minus1

    for (int32_t layerSetPos = 1; static_cast<int64_t>(layerSetPos) <= vpsNumLayerSetsMinus1; layerSetPos++)
        for (int layerId = 0; layerId <= vpsMaxLayerID; layerId++) buffer.BSSkipBits(1, true); // layer_id_included_flag

    bool vpsTimingInfoPresentFlag = buffer.ReadBool(true);
    if (vpsTimingInfoPresentFlag)
    {
        buffer.BSSkipBits(64, true); // vps_num_units_in_tick, vps_time_scale

        bool vpsPocProportionalToTimingFlag = buffer.ReadBool(true);
        if (!vpsPocProportionalToTimingFlag) buffer.SkipExp(true); // vps_num_ticks_poc_diff_one_minus1

        int vpsNumHRDParameters = static_cast<int>(buffer.ReadExp(true));
        if (vpsNumHRDParameters > 1024) vpsNumHRDParameters = 0;
        for (int hrdPos = 0; hrdPos < vpsNumHRDParameters; hrdPos++)
        {
            XXLCommon xxlCommon;
            XXL nal, vcl;

            buffer.SkipExp(true); // hrd_layer_sed_idx
            bool cprmsPresentFlag = hrdPos <= 0 || buffer.ReadBool(true);
            HRDParameters(buffer, cprmsPresentFlag, vpsNumLayerSetsMinus1, xxlCommon, nal, vcl);
        }
    }
    buffer.BSSkipBits(1, true); // vps_extension_flag

    if (vpsVideoParameterSetID >= static_cast<int>(VideoParamSets->size()))
        for (int i = static_cast<int>(VideoParamSets->size()) - 1; i < vpsVideoParameterSetID; i++)
        {
            VideoParamSets->push_back(VideoParamSetStruct{0});
        }
    net::L(*VideoParamSets, vpsVideoParameterSetID) = VideoParamSetStruct{static_cast<uint16_t>(vpsNumLayerSetsMinus1)};
}

// packet 33
void SeqParameterSet(TSStreamBuffer& buffer)
{
    if (_isInitialized) return;

    VUIParametersStruct vuiParametersItem;
    VideoParamSetStruct videoParamSetItem;

    uint32_t confWinLeftOffset = 0, confWinRightOffset = 0, confWinTopOffset = 0, confWinBottomOffset = 0;
    bool separateColourPlaneFlag = false;

    uint32_t videoParameterSetID = buffer.ReadBits2(4, true);

    if (videoParameterSetID >= VideoParamSets->size())
    {
        return;
    }

    uint32_t maxSubLayersMinus1 = buffer.ReadBits2(3, true);
    buffer.BSSkipBits(1, true);
    ProfileTierLevel(buffer, maxSubLayersMinus1);
    uint32_t spsSeqParameterSetID = buffer.ReadExp(true);
    uint32_t chromaFormatIDC = buffer.ReadExp(true);
    if (chromaFormatIDC >= 4) return;
    if (chromaFormatIDC == 3) separateColourPlaneFlag = buffer.ReadBool(true);
    uint32_t picWidthInLumaSamples = buffer.ReadExp(true);
    uint32_t picHeightInLumaSamples = buffer.ReadExp(true);
    if (buffer.ReadBool(true)) // conformance_window_flag
    {
        confWinLeftOffset = buffer.ReadExp(true);
        confWinRightOffset = buffer.ReadExp(true);
        confWinTopOffset = buffer.ReadExp(true);
        confWinBottomOffset = buffer.ReadExp(true);
    }

    uint32_t bitDepthLumaMinus8 = buffer.ReadExp(true);
    if (bitDepthLumaMinus8 > 6) return;
    uint32_t bitDepthChromaMinus8 = buffer.ReadExp(true);
    if (bitDepthChromaMinus8 > 6) return;
    uint32_t log2MaxPicOrderCntLsbMinus4 = buffer.ReadExp(true);
    if (log2MaxPicOrderCntLsbMinus4 > 12) return;
    bool spsSubLayerOrderingInfoPresentFlag = buffer.ReadBool(true);
    for (uint32_t subLayerPos = (spsSubLayerOrderingInfoPresentFlag ? 0 : maxSubLayersMinus1);
         subLayerPos <= maxSubLayersMinus1; subLayerPos++)
    {
        buffer.SkipExpMulti(3, true);
    }
    buffer.SkipExpMulti(6, true);

    if (buffer.ReadBool(true))     // scaling_list_enabled_flag
        if (buffer.ReadBool(true)) // sps_scaling_list_data_present_flag
            ScalingListData(buffer);

    buffer.BSSkipBits(2, true);
    if (buffer.ReadBool(true)) // pcm_enabled_flag
    {
        buffer.BSSkipBits(8, true);
        buffer.SkipExpMulti(2, true);
        buffer.BSSkipBits(1, true);
    }
    uint32_t numShortTermRefPicSets = buffer.ReadExp(true);
    ShortTermRefPicSets(buffer, numShortTermRefPicSets);
    if (buffer.ReadBool(true)) // long_term_ref_pics_present_flag
    {
        uint32_t numLongTermRefPicsSps = buffer.ReadExp(true);
        for (int32_t i = 0; static_cast<int64_t>(i) < numLongTermRefPicsSps; i++)
        {
            buffer.BSSkipBits(static_cast<int>(log2MaxPicOrderCntLsbMinus4 + 4), true);
            buffer.BSSkipBits(1, true);
        }
    }
    buffer.BSSkipBits(2, true);
    if (buffer.ReadBool(true)) // vui_parameters_present_flag
    {
        VUIParameters(buffer, videoParamSetItem, vuiParametersItem);
    }

    if (spsSeqParameterSetID >= SeqParameterSets->size())
        for (int64_t i = static_cast<int64_t>(SeqParameterSets->size()) - 1; i < spsSeqParameterSetID; i++)
        {
            SeqParameterSets->push_back(SeqParameterSetStruct{});
        }

    SeqParameterSetStruct sps;
    sps.VUIParameters = vuiParametersItem;
    sps.ProfileSpace = _profileSpace;
    sps.TierFlag = _tierFlag;
    sps.ProfileIDC = _profileIDC;
    sps.LevelIDC = _levelIDC;
    sps.PicWidthInLumaSamples = picWidthInLumaSamples;
    sps.PicHeightInLumaSamples = picHeightInLumaSamples;
    sps.ConfWinLeftOffset = confWinLeftOffset;
    sps.ConfWinRightOffset = confWinRightOffset;
    sps.ConfWinTopOffset = confWinTopOffset;
    sps.ConfWinBottomOffset = confWinBottomOffset;
    sps.VideoParameterSetID = static_cast<uint8_t>(videoParameterSetID);
    sps.ChromaFormatIDC = static_cast<uint8_t>(chromaFormatIDC);
    sps.SeparateColourPlaneFlag = separateColourPlaneFlag;
    sps.Log2MaxPicOrderCntLsbMinus4 = static_cast<uint8_t>(log2MaxPicOrderCntLsbMinus4);
    sps.BitDepthLumaMinus8 = static_cast<uint8_t>(bitDepthLumaMinus8);
    sps.BitDepthChromaMinus8 = static_cast<uint8_t>(bitDepthChromaMinus8);
    sps.GeneralProgressiveSourceFlag = _generalProgressiveSourceFlag;
    sps.GeneralInterlacedSourceFlag = _generalInterlacedSourceFlag;
    sps.GeneralFrameOnlyConstraintFlag = _generalFrameOnlyConstraintFlag;
    net::L(*SeqParameterSets, static_cast<int32_t>(spsSeqParameterSetID)) = sps;
}

// packet 34
void PicParameterSet(TSStreamBuffer& buffer)
{
    if (_isInitialized) return;

    uint32_t ppsPicParameterSetID = buffer.ReadExp(true);
    if (ppsPicParameterSetID >= 64) return;
    uint32_t ppsSeqParameterSetID = buffer.ReadExp(true);
    if (ppsSeqParameterSetID >= 16) return;
    if (ppsSeqParameterSetID >= SeqParameterSets->size()) return;

    bool dependentSliceSegmentsEnabledFlag = buffer.ReadBool(true);
    buffer.BSSkipBits(1, true);
    uint8_t numExtraSliceHeaderBits = static_cast<uint8_t>(buffer.ReadBits2(3, true));
    buffer.BSSkipBits(2, true);
    uint32_t numRefIdxL0DefaultActiveMinus1 = buffer.ReadExp(true);
    uint32_t numRefIdxL1DefaultActiveMinus1 = buffer.ReadExp(true);
    buffer.SkipExp(true);
    buffer.BSSkipBits(2, true);
    if (buffer.ReadBool(true))  // cu_qp_delta_enabled_flag
        buffer.SkipExp(true);   // diff_cu_qp_delta_depth
    buffer.SkipExpMulti(2, true);
    buffer.BSSkipBits(4, true);
    bool tilesEnabledFlag = buffer.ReadBool(true);
    buffer.BSSkipBits(1, true);
    if (tilesEnabledFlag)
    {
        uint32_t numTileColumnsMinus1 = buffer.ReadExp(true);
        uint32_t numTileRowsMinus1 = buffer.ReadExp(true);
        bool uniformSpacingFlag = buffer.ReadBool(true);
        if (!uniformSpacingFlag)
        {
            buffer.SkipExpMulti(static_cast<int>(numTileColumnsMinus1), true);
            buffer.SkipExpMulti(static_cast<int>(numTileRowsMinus1), true);
        }
        buffer.BSSkipBits(1, true);
    }
    buffer.BSSkipBits(1, true);
    if (buffer.ReadBool(true)) // deblocking_filter_control_present_flag
    {
        buffer.BSSkipBits(1, true);
        if (!buffer.ReadBool(true)) // pps_disable_deblocking_filter_flag
        {
            buffer.SkipExpMulti(2, true);
        }
    }
    if (buffer.ReadBool(true)) // pps_scaling_list_data_present_flag
        ScalingListData(buffer);
    buffer.BSSkipBits(1, true);
    buffer.SkipExp(true);
    buffer.BSSkipBits(1, true);
    if (buffer.ReadBool(true)) // pps_extension_flag
        buffer.BSSkipNextByte();

    if (ppsPicParameterSetID >= PicParameterSets->size())
        for (int64_t i = static_cast<int64_t>(PicParameterSets->size()) - 1; i < ppsPicParameterSetID; i++)
            PicParameterSets->push_back(PicParameterSetStruct{});
    net::L(*PicParameterSets, static_cast<int32_t>(ppsPicParameterSetID)) =
        PicParameterSetStruct{static_cast<uint8_t>(ppsSeqParameterSetID),
                              static_cast<uint8_t>(numRefIdxL0DefaultActiveMinus1),
                              static_cast<uint8_t>(numRefIdxL1DefaultActiveMinus1), numExtraSliceHeaderBits,
                              dependentSliceSegmentsEnabledFlag};
}

// packet 35
void AccessUnitDelimiter(TSStreamBuffer& buffer) { buffer.BSSkipBits(3, true); }

// SEI - 4 SEI_USER_DATA_REGISTERED_ITU_T_T35
void SeiUserDataRegisteredItuTT35(TSStreamBuffer& buffer, uint32_t payloadSize)
{
    uint16_t country_code = buffer.ReadBits2(8, true);
    uint16_t terminal_provider_code = buffer.ReadBits2(16, true);
    uint16_t terminal_provider_oriented_code = buffer.ReadBits2(16, true);
    uint32_t application_id = buffer.ReadBits4(8, true);
    uint32_t application_version = buffer.ReadBits4(8, true);
    uint32_t num_windows = buffer.ReadBits4(2, true);
    buffer.BSSkipBits(6, true);
    // ST 2094-40 page 4
    if (country_code == 0xB5 && terminal_provider_code == 0x003C && terminal_provider_oriented_code == 0x0001)
    {
        // ST 2094-40 application #4 page 4
        if (application_id == 4 && (application_version == 0 || application_version == 1) && num_windows == 1)
        {
            IsHdr10Plus = true;
        }
    }
    payloadSize -= 8;
    buffer.BSSkipBytes(static_cast<int>(payloadSize), true);
}

// SEI - 0
void SeiMessageBufferingPeriodXXL(TSStreamBuffer& buffer, const XXLCommon& xxlCommon, bool irapCPBParamsPresentFlag,
                                  const XXL& xxl)
{
    for (size_t schedSelIdx = 0; schedSelIdx < xxl.SchedSel.size(); schedSelIdx++)
    {
        buffer.BSSkipBits(xxlCommon.InitialCPBRemovalDelayLengthMinus1 + 1, true); // initial_cpb_removal_delay
        buffer.BSSkipBits(xxlCommon.InitialCPBRemovalDelayLengthMinus1 + 1, true); // initial_cpb_removal_delay_offset
        if (xxlCommon.SubPicHRDParamsPresentFlag || irapCPBParamsPresentFlag)
        {
            buffer.BSSkipBits(xxlCommon.InitialCPBRemovalDelayLengthMinus1 + 1, true); // initial_alt_cpb_removal_delay
            buffer.BSSkipBits(xxlCommon.InitialCPBRemovalDelayLengthMinus1 + 1, true); // initial_alt_cpb_removal_delay_offset
        }
    }
}

void SeiMessageBufferingPeriod(TSStreamBuffer& buffer, uint32_t& seqParameterSetID, uint32_t payloadSize)
{
    seqParameterSetID = buffer.ReadExp(true);
    if (seqParameterSetID >= SeqParameterSets->size())
    {
        buffer.BSSkipBits(static_cast<int>(payloadSize * 8), true);
        return;
    }
    const SeqParameterSetStruct seqParameterSetItem = (*SeqParameterSets)[seqParameterSetID];
    bool subPicHRDParamsPresentFlag = false; // Default
    bool irapCPBParamsPresentFlag = seqParameterSetItem.VUIParameters.XXLCommon_.SubPicHRDParamsPresentFlag;
    if (!subPicHRDParamsPresentFlag) irapCPBParamsPresentFlag = buffer.ReadBool(true);
    uint8_t auCPBRemovalDelayLengthMinus1 =
        static_cast<uint8_t>(seqParameterSetItem.VUIParameters.XXLCommon_.AUCPBRemovalDelayLengthMinus1);
    uint8_t dpbOutputDelayLengthMinus1 =
        static_cast<uint8_t>(seqParameterSetItem.VUIParameters.XXLCommon_.DPBOutputDelayLengthMinus1);
    if (irapCPBParamsPresentFlag)
    {
        buffer.BSSkipBits(auCPBRemovalDelayLengthMinus1 + dpbOutputDelayLengthMinus1 + 2, true);
    }
    buffer.BSSkipBits(auCPBRemovalDelayLengthMinus1 + 2, true); // concatenation_flag, au_cpb_removal_delay_delta_minus1
    if (seqParameterSetItem.NalHrdBpPresentFlag())
        SeiMessageBufferingPeriodXXL(buffer, seqParameterSetItem.VUIParameters.XXLCommon_, irapCPBParamsPresentFlag,
                                     seqParameterSetItem.VUIParameters.NAL);
    if (seqParameterSetItem.VclHrdPbPresentFlag())
        SeiMessageBufferingPeriodXXL(buffer, seqParameterSetItem.VUIParameters.XXLCommon_, irapCPBParamsPresentFlag,
                                     seqParameterSetItem.VUIParameters.VCL);
}

// SEI - 1
void SeiMessagePicTiming(TSStreamBuffer& buffer, uint32_t& seqParameterSetID, uint32_t payloadSize)
{
    if (seqParameterSetID == UINT32_MAX && SeqParameterSets->size() == 1) seqParameterSetID = 0;
    if (seqParameterSetID >= SeqParameterSets->size())
    {
        buffer.BSSkipBits(static_cast<int>(payloadSize * 8), true);
        return;
    }
    const SeqParameterSetStruct seqParameterSetItem = (*SeqParameterSets)[seqParameterSetID];

    if (seqParameterSetItem.VUIParameters.FrameFieldInfoPresentFlag)
    {
        buffer.BSSkipBits(7, true); // pic_struct, source_scan_type, duplicate_flag
    }
    if (seqParameterSetItem.CpbDpbDelaysPresentFlag())
    {
        uint8_t auCPBRemovalDelayLengthMinus1 =
            static_cast<uint8_t>(seqParameterSetItem.VUIParameters.XXLCommon_.AUCPBRemovalDelayLengthMinus1);
        uint8_t dpbOutputDelayLengthMinus1 =
            static_cast<uint8_t>(seqParameterSetItem.VUIParameters.XXLCommon_.DPBOutputDelayLengthMinus1);
        bool subPicHRDParamsPresentFlag = seqParameterSetItem.VUIParameters.XXLCommon_.SubPicHRDParamsPresentFlag;
        buffer.BSSkipBits(auCPBRemovalDelayLengthMinus1 + dpbOutputDelayLengthMinus1 + 2, true);
        if (subPicHRDParamsPresentFlag)
        {
            uint8_t dpbOutputDelayDULengthMinus1 =
                static_cast<uint8_t>(seqParameterSetItem.VUIParameters.XXLCommon_.DPBOutputDelayDULengthMinus1);
            buffer.BSSkipBits(dpbOutputDelayDULengthMinus1 + 1, true);
        }
    }
}

// SEI - 6
void SeiMessageRecoveryPoint(TSStreamBuffer& buffer)
{
    buffer.SkipExp(true);       // recovery_poc_cnt
    buffer.BSSkipBits(2, true); // exact_match_flag, broken_link_flag
}

// SEI - 129
void SeiMessageActiveParametersSets(TSStreamBuffer& buffer)
{
    buffer.BSSkipBits(6, true);
    uint32_t numSpsIdsMinus1 = buffer.ReadExp(true);
    buffer.SkipExpMulti(static_cast<int>(numSpsIdsMinus1 + 1), true);
}

// SEI - 137
void SeiMessageMasteringDisplayColourVolume(TSStreamBuffer& buffer)
{
    uint16_t primaries[8] = {};
    uint32_t luminance[2] = {};
    buffer.BSResetBits();
    for (int i = 0; i < 3; i++)
    {
        primaries[i * 2] = buffer.ReadBits2(16, true);       // x
        primaries[(i * 2) + 1] = buffer.ReadBits2(16, true); // y
    }
    primaries[3 * 2] = buffer.ReadBits2(16, true);
    primaries[(3 * 2) + 1] = buffer.ReadBits2(16, true);

    luminance[1] = buffer.ReadBits4(32, true);
    luminance[0] = buffer.ReadBits4(32, true);

    // Reordering to RGB
    int R = 4, G = 4, B = 4;
    for (int c = 0; c < 3; c++)
    {
        if (primaries[c * 2] < 17500 && primaries[(c * 2) + 1] < 17500)
            B = c;
        else if (static_cast<int>(primaries[(c * 2) + 1]) - static_cast<int>(primaries[c * 2]) >= 0)
            G = c;
        else
            R = c;
    }
    if ((R | B | G) >= 4)
    {
        // Order not automatically detected, betting on GBR order
        G = 0;
        B = 1;
        R = 2;
    }

    bool notValid = false;
    for (int c = 0; c < 8; c++)
    {
        if (primaries[c] == UINT16_MAX) notValid = true;
    }

    MasteringDisplayColorPrimaries = std::string();
    bool humanReadablePrimaries = false;

    if (!notValid)
    {
        for (const auto& value : MasteringDisplayColorVolumeValues)
        {
            uint8_t code = value.Code;
            for (int j = 0; j < 2; j++)
            {
                // +/- 0.0005 (3 digits after comma)
                if (primaries[G * 2 + j] < value.Values[0 * 2 + j] - 25 ||
                    (primaries[G * 2 + j] >= value.Values[0 * 2 + j] + 25))
                    code = 0;
                if (primaries[B * 2 + j] < value.Values[1 * 2 + j] - 25 ||
                    (primaries[B * 2 + j] >= value.Values[1 * 2 + j] + 25))
                    code = 0;
                if (primaries[R * 2 + j] < value.Values[2 * 2 + j] - 25 ||
                    (primaries[R * 2 + j] >= value.Values[2 * 2 + j] + 25))
                    code = 0;
                // +/- 0.00005 (4 digits after comma)
                if (primaries[3 * 2 + j] < value.Values[3 * 2 + j] - 2 ||
                    (primaries[3 * 2 + j] >= value.Values[3 * 2 + j] + 3))
                    code = 0;
            }
            if (code > 0)
            {
                MasteringDisplayColorPrimaries = ColourPrimaries(code);
                humanReadablePrimaries = true;
                break;
            }
        }

        if (!humanReadablePrimaries)
        {
            auto p = [&](int index) { return net::Custom(static_cast<double>(primaries[index]) / 50000, 6, 15); };
            MasteringDisplayColorPrimaries += "R: x=" + p(R * 2) + " y=" + p((R * 2) + 1) + ", G: x=" + p(G * 2) +
                                              " y=" + p((G * 2) + 1) + ", B: x=" + p(B * 2) + " y=" + p((B * 2) + 1) +
                                              ", White point: x=" + p(3 * 2) + " y=" + p((3 * 2) + 1);
        }
    }

    // `Luminance[1] - (int)Luminance[1] == 0` in C# (uint minus a wrapped int).
    bool wholeMax = static_cast<int64_t>(luminance[1]) - static_cast<int32_t>(luminance[1]) == 0;
    MasteringDisplayLuminance = "min: " + net::Custom(static_cast<double>(luminance[0]) / 10000, 4, 15) + " cd/m2, max: " +
                                (wholeMax ? net::Custom(static_cast<double>(luminance[1]) / 10000, 0, 15)
                                          : net::Custom(static_cast<double>(luminance[1]) / 10000, 4, 15)) +
                                " cd/m2";
}

// SEI - 144
void SeiMessageLightLevel(TSStreamBuffer& buffer)
{
    MaximumContentLightLevel = buffer.ReadBits2(16, true);
    MaximumFrameAverageLightLevel = buffer.ReadBits2(16, true);
    LightLevelAvailable = true;
}

// SEI - 147
void SeiAlternativeTransferCharacteristics(TSStreamBuffer& buffer)
{
    PreferredTransferCharacteristics = static_cast<uint8_t>(buffer.ReadBits2(8, true));
}

// packet 39 & 40
void Sei(TSStreamBuffer& buffer)
{
    if (_isInitialized) return;

    int64_t elementStart = buffer.Position();

    int numBytes;

    // find element end
    do
    {
        int64_t streamPos = buffer.Position();
        numBytes = 0;
        if (buffer.ReadByte() == 0x0 && buffer.ReadByte() == 0x0 && buffer.ReadByte() == 0x0 && buffer.ReadByte() == 0x1)
        {
            numBytes = 4;
            break;
        }

        buffer.BSSkipBytes(static_cast<int>(streamPos - buffer.Position()));
        if (buffer.ReadByte() == 0x0 && buffer.ReadByte() == 0x0 && buffer.ReadByte() == 0x1)
        {
            numBytes = 3;
            break;
        }
        buffer.BSSkipBytes(static_cast<int>(streamPos - buffer.Position() + 1));
    } while (buffer.Position() < buffer.Length());

    int64_t elementSize = buffer.Position() - elementStart;

    buffer.BSSkipBytes(static_cast<int>(elementSize * -1));

    elementSize -= numBytes + 1;

    do
    {
        uint32_t seqParameterSetID = UINT32_MAX;
        uint32_t payloadType = 0, payloadSize = 0;
        uint8_t payloadTypeByte, payloadSizeByte;

        do
        {
            payloadTypeByte = buffer.ReadByte(true);
            payloadType += payloadTypeByte;
        } while (payloadTypeByte == 0xFF);
        do
        {
            payloadSizeByte = buffer.ReadByte(true);
            payloadSize += payloadSizeByte;
        } while (payloadSizeByte == 0xFF);

        uint64_t savedPos = static_cast<uint64_t>(buffer.Position()) + payloadSize;

        if (savedPos > static_cast<uint64_t>(buffer.Length())) // wrong length
            return;

        switch (payloadType)
        {
            case 0: SeiMessageBufferingPeriod(buffer, seqParameterSetID, payloadSize); break;
            case 1: SeiMessagePicTiming(buffer, seqParameterSetID, payloadSize); break;
            case 6: SeiMessageRecoveryPoint(buffer); break;
            case 129: SeiMessageActiveParametersSets(buffer); break;
            case 137: SeiMessageMasteringDisplayColourVolume(buffer); break;
            case 144: SeiMessageLightLevel(buffer); break;
            case 147: SeiAlternativeTransferCharacteristics(buffer); break;
            case 4: // SEI_USER_DATA_REGISTERED_ITU_T_T35
                SeiUserDataRegisteredItuTT35(buffer, payloadSize);
                break;
            default: buffer.BSSkipBytes(static_cast<int>(payloadSize), true); break;
        }
        if (savedPos > static_cast<uint64_t>(buffer.Position()))
            buffer.BSSkipBytes(static_cast<int>(savedPos - static_cast<uint64_t>(buffer.Position())), true);
    } while (buffer.Position() < elementStart + elementSize);
}

} // namespace

void Scan(TSVideoStream& stream, TSStreamBuffer& buffer, NString& tag)
{
    _isInitialized = stream.IsInitialized;

    if (!stream.ExtendedData) stream.ExtendedData = std::make_shared<ExtendedDataSet>();

    ExtendedDataSet& extendedData = *stream.ExtendedData;

    VideoParamSets = &extendedData.VideoParamSets;
    SeqParameterSets = &extendedData.SeqParameterSets;
    PicParameterSets = &extendedData.PicParameterSets;

    MasteringDisplayColorPrimaries = extendedData.MasteringDisplayColorPrimaries;
    MasteringDisplayLuminance = extendedData.MasteringDisplayLuminance;

    MaximumContentLightLevel = extendedData.MaximumContentLightLevel;
    MaximumFrameAverageLightLevel = extendedData.MaximumFrameAverageLightLevel;

    LightLevelAvailable = extendedData.LightLevelAvailable;

    PreferredTransferCharacteristics = extendedData.PreferredTransferCharacteristics;

    ExtendedFormatInfo = &extendedData.ExtendedFormatInfo;

    bool frameTypeRead = false;

    do
    {
        bool syncByteFound = false;
        do
        {
            int64_t streamPos = buffer.Position();
            if (buffer.ReadByte() == 0x0 && buffer.ReadByte() == 0x0 && buffer.ReadByte() == 0x0 &&
                buffer.ReadByte() == 0x1)
            {
                syncByteFound = true;
                break;
            }

            buffer.BSSkipBytes(static_cast<int>(streamPos - buffer.Position()));

            if (buffer.ReadByte() == 0x0 && buffer.ReadByte() == 0x0 && buffer.ReadByte() == 0x1)
            {
                syncByteFound = true;
                break;
            }

            buffer.BSSkipBytes(static_cast<int>(streamPos - buffer.Position() + 1));

        } while ((buffer.Position() < buffer.Length() - 3) && (!_isInitialized || (_isInitialized && !frameTypeRead)));

        if (buffer.Position() < buffer.Length() && syncByteFound)
        {
            int64_t lastStreamPos = buffer.Position();

            buffer.BSSkipBits(1, true); // skip 1 bit

            int64_t nalUnitType = buffer.ReadBits2(6, true);

            buffer.BSSkipBits(9, true); // nuh_layer_id (6), nuh_temporal_id_plus1 (3)

            switch (nalUnitType)
            {
                case 0:
                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                case 6:
                case 7:
                case 8:
                case 9:
                case 16:
                case 17:
                case 18:
                case 19:
                case 20:
                case 21:
                    tag = SliceSegmentLayer(buffer, nalUnitType);
                    frameTypeRead = tag.has_value();
                    break;
                case 32: VideoParameterSet(buffer); break;
                case 33: SeqParameterSet(buffer); break;
                case 34: PicParameterSet(buffer); break;
                case 35: AccessUnitDelimiter(buffer); break;
                case 39:
                case 40: Sei(buffer); break;
            }

            buffer.BSSkipNextByte();
            buffer.BSSkipBytes(static_cast<int>(lastStreamPos - buffer.Position()), true);
        }
    } while ((buffer.Position() < buffer.Length() - 3) && (!_isInitialized || (_isInitialized && !frameTypeRead)));

    extendedData.PreferredTransferCharacteristics = PreferredTransferCharacteristics;

    extendedData.LightLevelAvailable = LightLevelAvailable;

    extendedData.MaximumContentLightLevel = MaximumContentLightLevel;
    extendedData.MaximumFrameAverageLightLevel = MaximumFrameAverageLightLevel;

    extendedData.MasteringDisplayColorPrimaries = MasteringDisplayColorPrimaries;
    extendedData.MasteringDisplayLuminance = MasteringDisplayLuminance;

    extendedData.IsHdr10Plus = IsHdr10Plus;

    // TODO: profile to string
    if (!SeqParameterSets->empty() && !stream.IsInitialized)
    {
        const SeqParameterSetStruct& seqParameterSet = (*SeqParameterSets)[0];
        if (seqParameterSet.ProfileSpace == 0)
        {
            std::string profile;
            if (seqParameterSet.ProfileIDC > 0)
            {
                switch (seqParameterSet.ProfileIDC)
                {
                    case 0: profile = "No profile"; break;
                    case 1: profile = "Main"; break;
                    case 2: profile = "Main 10"; break;
                    case 3: profile = "Main Still"; break;
                    default: profile = ""; break;
                }
            }
            if (seqParameterSet.LevelIDC > 0)
            {
                float calcLevel = static_cast<float>(seqParameterSet.LevelIDC) / 30;
                uint32_t dec = seqParameterSet.LevelIDC % 10;
                profile += " @ Level " + net::Custom(calcLevel, dec >= 1 ? 1 : 0, 7) + " @ ";
                if (seqParameterSet.TierFlag)
                    profile += "High";
                else
                    profile += "Main";
            }

            stream.EncodingProfile = profile;

            if (seqParameterSet.ChromaFormatIDC > 0)
            {
                std::string chromaFormat;
                switch (seqParameterSet.ChromaFormatIDC)
                {
                    case 1: chromaFormat = "4:2:0"; break;
                    case 2: chromaFormat = "4:2:2"; break;
                    case 3: chromaFormat = "4:4:4"; break;
                }
                if (!chromaFormat.empty() && BDInfoSettings::ExtendedStreamDiagnostics)
                    ExtendedFormatInfo->push_back(chromaFormat);
            }

            if (seqParameterSet.BitDepthLumaMinus8 == seqParameterSet.BitDepthChromaMinus8)
                ExtendedFormatInfo->push_back(std::to_string(seqParameterSet.BitDepthLumaMinus8 + 8) + " bits");

            const VUIParametersStruct& vui = seqParameterSet.VUIParameters;
            if (seqParameterSet.BitDepthLumaMinus8 + 8 == 10 && // 10 bit
                seqParameterSet.ChromaFormatIDC == 1 &&         // ChromaFormat 4:2:0
                vui.VideoSignalTypePresentFlag && vui.ColourDescriptionPresentFlag &&
                vui.ColourPrimaries == 9 &&         // ColourPrimaries BT.2020
                vui.TransferCharacteristics == 16 && // TransferCharacteristics PQ
                (vui.MatrixCoefficients == 9 ||      // MatrixCoefficients BT.2020 non-constant
                 vui.MatrixCoefficients == 10) &&    // MatrixCoefficients BT.2020 constant
                !MasteringDisplayColorPrimaries.empty())
            {
                ExtendedFormatInfo->push_back(stream.PID >= 4117 ? "Dolby Vision" : IsHdr10Plus ? "HDR10+" : "HDR10");
            }

            if (vui.VideoSignalTypePresentFlag)
            {
                if (BDInfoSettings::ExtendedStreamDiagnostics)
                    ExtendedFormatInfo->push_back(vui.VideoFullRangeFlag == 1 ? "Full Range" : "Limited Range");

                if (vui.ColourDescriptionPresentFlag)
                {
                    ExtendedFormatInfo->push_back(ColourPrimaries(vui.ColourPrimaries));
                    if (BDInfoSettings::ExtendedStreamDiagnostics)
                    {
                        ExtendedFormatInfo->push_back(TransferCharacteristics(vui.TransferCharacteristics));
                        ExtendedFormatInfo->push_back(MatrixCoefficients(vui.MatrixCoefficients));
                    }
                }
            }
        }
    }

    if (BDInfoSettings::ExtendedStreamDiagnostics && !stream.IsInitialized)
    {
        if (!MasteringDisplayColorPrimaries.empty())
        {
            ExtendedFormatInfo->push_back("Mastering display color primaries: " + MasteringDisplayColorPrimaries);
        }
        if (!MasteringDisplayLuminance.empty())
        {
            ExtendedFormatInfo->push_back("Mastering display luminance: " + MasteringDisplayLuminance);
        }

        if (LightLevelAvailable && MaximumContentLightLevel > 0)
        {
            ExtendedFormatInfo->push_back("Maximum Content Light Level: " + std::to_string(MaximumContentLightLevel) +
                                          " cd / m2");
            ExtendedFormatInfo->push_back("Maximum Frame-Average Light Level: " +
                                          std::to_string(MaximumFrameAverageLightLevel) + " cd/m2");
        }
    }

    stream.IsVBR = true;
    if (!SeqParameterSets->empty()) stream.IsInitialized = true;
}

} // namespace TSCodecHEVC
} // namespace bdinfo
