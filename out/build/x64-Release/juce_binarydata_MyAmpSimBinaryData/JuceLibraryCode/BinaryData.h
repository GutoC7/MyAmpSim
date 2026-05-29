/* =========================================================================================

   This is an auto-generated file: Any edits you make may be overwritten!

*/

#pragma once

namespace BinaryData
{
    extern const char*   AC30BMS_MD421_0_km_202257_wav;
    const int            AC30BMS_MD421_0_km_202257_wavSize = 24560;

    extern const char*   AC30BMS_Rear_SM57_km_202257_wav;
    const int            AC30BMS_Rear_SM57_km_202257_wavSize = 24560;

    extern const char*   AC30BMS_SM57_0_km_202257_wav;
    const int            AC30BMS_SM57_0_km_202257_wavSize = 24560;

    extern const char*   AC30BMS_TLM103_0_5_km_202257_wav;
    const int            AC30BMS_TLM103_0_5_km_202257_wavSize = 24560;

    extern const char*   AC30BMS_TLM103_1_km_202257_wav;
    const int            AC30BMS_TLM103_1_km_202257_wavSize = 24560;

    extern const char*   AC30BMS_TLM103_0_km_202257_wav;
    const int            AC30BMS_TLM103_0_km_202257_wavSize = 24560;

    extern const char*   AC30BMS_TLM103_00_km_202257_wav;
    const int            AC30BMS_TLM103_00_km_202257_wavSize = 24560;

    extern const char*   AC30BMS_VORTEX_TLM103_km_202257_wav;
    const int            AC30BMS_VORTEX_TLM103_km_202257_wavSize = 24560;

    extern const char*   cabinet_wav;
    const int            cabinet_wavSize = 24560;

    // Number of elements in the namedResourceList and originalFileNames arrays.
    const int namedResourceListSize = 9;

    // Points to the start of a list of resource names.
    extern const char* namedResourceList[];

    // Points to the start of a list of resource filenames.
    extern const char* originalFilenames[];

    // If you provide the name of one of the binary resource variables above, this function will
    // return the corresponding data and its size (or a null pointer if the name isn't found).
    const char* getNamedResource (const char* resourceNameUTF8, int& dataSizeInBytes);

    // If you provide the name of one of the binary resource variables above, this function will
    // return the corresponding original, non-mangled filename (or a null pointer if the name isn't found).
    const char* getNamedResourceOriginalFilename (const char* resourceNameUTF8);
}
