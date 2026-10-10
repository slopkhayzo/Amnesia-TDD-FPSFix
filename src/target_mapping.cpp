#include "target_mapping.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace atdd::target {
namespace {

struct Pattern {
    std::span<const std::uint8_t> bytes;
    std::string_view mask;
    const wchar_t* name;
};

constexpr std::array<std::uint8_t, 64> kEngineRunBytes = {
    0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x00, 0x00,
    0x00, 0x00, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00,
    0x50, 0x83, 0xEC, 0x38, 0xA1, 0x00, 0x00, 0x00,
    0x00, 0x33, 0xC5, 0x89, 0x45, 0xF0, 0x53, 0x56,
    0x57, 0x50, 0x8D, 0x45, 0xF4, 0x64, 0xA3, 0x00,
    0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00, 0x00,
    0x8B, 0xF1, 0xE8, 0x99, 0x3B, 0xFE, 0xFF, 0xD9,
    0xEE, 0x8B, 0x4E, 0x14, 0xDD, 0x5D, 0xBC, 0xD9,
};
constexpr std::array<std::uint8_t, 24> kUpdaterRunMessageBytes = {
    0x55, 0x8B, 0xEC, 0x51, 0x53, 0x8B, 0x5D, 0x08,
    0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x47, 0x34, 0x8B,
    0x30, 0x83, 0xFB, 0x05, 0x74, 0x67, 0x3B, 0xF0,
};
constexpr std::array<std::uint8_t, 24> kSceneRenderBytes = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0xFF, 0x05,
    0x00, 0x00, 0x00, 0x00, 0x8B, 0x41, 0x44, 0x89,
    0x4D, 0xFC, 0x8B, 0x08, 0x89, 0x4D, 0xF0, 0x3B,
};
constexpr std::array<std::uint8_t, 24> kCameraSetPositionBytes = {
    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0xD9, 0x00,
    0x51, 0xD9, 0x19, 0x81, 0xC1, 0xCC, 0x01, 0x00,
    0x00, 0xD9, 0x40, 0x04, 0xD9, 0x99, 0x38, 0xFE,
};
constexpr std::array<std::uint8_t, 24> kCameraSetPitchBytes = {
    0x55, 0x8B, 0xEC, 0xD9, 0x45, 0x08, 0xD9, 0x51,
    0x28, 0xD9, 0x41, 0x34, 0xD9, 0xEE, 0xD9, 0xC0,
    0xDD, 0xEA, 0xDF, 0xE0, 0xDD, 0xD9, 0xF6, 0xC4,
};
constexpr std::array<std::uint8_t, 24> kCameraSetYawBytes = {
    0x55, 0x8B, 0xEC, 0xD9, 0x45, 0x08, 0xD9, 0x51,
    0x2C, 0xD9, 0x41, 0x3C, 0xD9, 0xEE, 0xD9, 0xC0,
    0xDD, 0xEA, 0xDF, 0xE0, 0xDD, 0xD9, 0xF6, 0xC4,
};
constexpr std::array<std::uint8_t, 24> kCameraSetRollBytes = {
    0x55, 0x8B, 0xEC, 0xD9, 0x45, 0x08, 0xB0, 0x01,
    0xD9, 0x59, 0x30, 0x88, 0x81, 0x51, 0x08, 0x00,
    0x00, 0x66, 0xC7, 0x81, 0x53, 0x08, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 32> kCameraAddPitchBytes = {
    0x55, 0x8B, 0xEC, 0xD9, 0x45, 0x08, 0xD8, 0x41,
    0x28, 0xD9, 0x5D, 0x08, 0xD9, 0x45, 0x08, 0xD9,
    0x51, 0x28, 0xD9, 0x41, 0x34, 0xD9, 0xEE, 0xD9,
    0xC0, 0xDD, 0xEA, 0xDF, 0xE0, 0xDD, 0xD9, 0xF6,
};
constexpr std::array<std::uint8_t, 32> kCameraAddYawBytes = {
    0x55, 0x8B, 0xEC, 0xD9, 0x45, 0x08, 0xD8, 0x41,
    0x2C, 0xD9, 0x5D, 0x08, 0xD9, 0x45, 0x08, 0xD9,
    0x51, 0x2C, 0xD9, 0x41, 0x3C, 0xD9, 0xEE, 0xD9,
    0xC0, 0xDD, 0xEA, 0xDF, 0xE0, 0xDD, 0xD9, 0xF6,
};
constexpr std::array<std::uint8_t, 32> kLuxPlayerAddYawBytes = {
    0x55, 0x8B, 0xEC, 0x56, 0x8B, 0xF1, 0x80, 0xBE,
    0xB4, 0x02, 0x00, 0x00, 0x00, 0x74, 0x28, 0xD9,
    0x45, 0x08, 0x51, 0x8B, 0x8E, 0x78, 0x02, 0x00,
    0x00, 0xDC, 0xC0, 0xD9, 0x5D, 0x08, 0xD9, 0x45,
};
constexpr std::array<std::uint8_t, 32> kLuxPlayerAddPitchBytes = {
    0x55, 0x8B, 0xEC, 0x56, 0x8B, 0xF1, 0x80, 0xBE,
    0xB4, 0x02, 0x00, 0x00, 0x00, 0x74, 0x0D, 0xD9,
    0x45, 0x08, 0xDC, 0xC0, 0xD9, 0x5D, 0x08, 0xD9,
    0x45, 0x08, 0xEB, 0x52, 0xD9, 0x86, 0xC4, 0x00,
};
constexpr std::array<std::uint8_t, 19> kPlayerHandsPostUpdateBytes = {
    0x55, 0x8B, 0xEC, 0xD9, 0x45, 0x08, 0x51, 0xD9,
    0x1C, 0x24, 0xE8, 0xBA, 0xFC, 0xFF, 0xFF, 0x5D,
    0xC2, 0x04, 0x00,
};
constexpr std::array<std::uint8_t, 32> kLuxPropOnUpdateBytes = {
    0x55, 0x8B, 0xEC, 0xD9, 0x45, 0x08, 0x56, 0x57,
    0x51, 0xD9, 0x1C, 0x24, 0x8B, 0xF1, 0xE8, 0x5E,
    0xED, 0xFF, 0xFF, 0xD9, 0x45, 0x08, 0x51, 0x8B,
    0xCE, 0xD9, 0x1C, 0x24, 0xE8, 0x15, 0xFD, 0xFF,
};
constexpr std::array<std::uint8_t, 32> kLuxEnemyOnUpdateBytes = {
    0x55, 0x8B, 0xEC, 0x51, 0x51, 0x56, 0x8B, 0xF1,
    0x80, 0xBE, 0x0C, 0x01, 0x00, 0x00, 0x00, 0x0F,
    0x85, 0x30, 0x01, 0x00, 0x00, 0xA1, 0x74, 0xA8,
    0xAF, 0x00, 0xD9, 0x45, 0x08, 0x8B, 0x08, 0xDD,
};
constexpr std::array<std::uint8_t, 13> kEntityGetWorldMatrixBytes = {
    0x56, 0x8B, 0xF1, 0xE8, 0x78, 0x00, 0x00, 0x00,
    0x8D, 0x46, 0x6C, 0x5E, 0xC3,
};
constexpr std::array<std::uint8_t, 7> kEntityGetEntityParentBytes = {
    0x8B, 0x81, 0x88, 0x02, 0x00, 0x00, 0xC3,
};
constexpr std::array<std::uint8_t, 22> kPhysicsBodyGetEnabledBytes = {
    0x8B, 0x81, 0xB0, 0x03, 0x00, 0x00, 0x50, 0xFF,
    0x15, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x04,
    0x85, 0xC0, 0x0F, 0x94, 0xC0, 0xC3,
};
constexpr std::array<std::uint8_t, 32> kNewtonTransformCallbackBytes = {
    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x56, 0x50,
    0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x4D,
    0x0C, 0x83, 0xC4, 0x04, 0x8B, 0xF0, 0x51, 0x8D,
    0x4E, 0x2C, 0xE8, 0xB1, 0x04, 0x00, 0x00, 0x6A,
};
constexpr std::array<std::uint8_t, 24> kMeshEntityUpdateGraphicsBytes = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x8C, 0x00, 0x00,
    0x00, 0x53, 0x57, 0x8B, 0xF9, 0x33, 0xDB, 0x89,
    0x7D, 0xF4, 0x38, 0x9F, 0xBC, 0x02, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 24> kParticleEmitterUpdateGraphicsBytes = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x1C, 0x02, 0x00,
    0x00, 0xA1, 0x20, 0x90, 0xAF, 0x00, 0x33, 0xC5,
    0x89, 0x45, 0xFC, 0x53, 0x56, 0x8B, 0xF1, 0x8B,
};
constexpr std::array<std::uint8_t, 32> kImageTrailRenderEffectBytes = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0xD9, 0xE8,
    0x56, 0xD9, 0x55, 0xF8, 0x57, 0xD9, 0x5D, 0xFC,
    0x83, 0xEC, 0x08, 0xD9, 0x05, 0x00, 0x00, 0x00,
    0x00, 0x8B, 0xF1, 0x8B, 0x4E, 0x18, 0xD9, 0x5C,
};
constexpr std::array<std::uint8_t, 32> kSubMeshGetModelMatrixBytes = {
    0x56, 0x8B, 0xF1, 0x80, 0xBE, 0x3D, 0x03, 0x00,
    0x00, 0x00, 0x74, 0x38, 0x8A, 0x86, 0x9A, 0x03,
    0x00, 0x00, 0x84, 0xC0, 0x78, 0x2E, 0x75, 0x24,
    0x68, 0xC0, 0xE8, 0xAE, 0x00, 0xE8, 0xEE, 0x95,
};
constexpr std::array<std::uint8_t, 32> kMouseSdlUpdateBytes = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0x53, 0x56,
    0x8B, 0xF1, 0x33, 0xDB, 0x57, 0x38, 0x5E, 0x24,
    0x74, 0x1E, 0x8D, 0x7B, 0x0A, 0xE8, 0x7E, 0x0F,
    0x0A, 0x00, 0x8D, 0x45, 0xF4, 0x50, 0x8D, 0x4D,
};
constexpr std::array<std::uint8_t, 29> kMouseSdlGetRelPositionBytes = {
    0x55, 0x8B, 0xEC, 0x8B, 0x51, 0x30, 0x8B, 0x45,
    0x08, 0x89, 0x10, 0x8B, 0x51, 0x34, 0x89, 0x50,
    0x04, 0x33, 0xD2, 0x89, 0x51, 0x30, 0x89, 0x51,
    0x34, 0x5D, 0xC2, 0x04, 0x00,
};
constexpr std::array<std::uint8_t, 32> kGuiSendMousePosBytes = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x5C, 0x56, 0x8B,
    0xF1, 0x83, 0x7E, 0x34, 0x00, 0x75, 0x09, 0x32,
    0xC0, 0x5E, 0x8B, 0xE5, 0x5D, 0xC2, 0x08, 0x00,
    0x8B, 0x46, 0x24, 0x8B, 0x48, 0x20, 0x8B, 0x45,
};
constexpr std::array<std::uint8_t, 32> kLuxUpdateGamePlayerInputBytes = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x38, 0x53, 0x56,
    0x8B, 0xF1, 0x57, 0x8B, 0x7E, 0x38, 0x8B, 0xCF,
    0xE8, 0x8E, 0x34, 0xFB, 0xFF, 0x84, 0xC0, 0x74,
    0x5D, 0x8B, 0x4E, 0x34, 0x6A, 0x28, 0xE8, 0xD6,
};

bool Matches(const std::uint8_t* candidate, const Pattern& pattern) {
    for (std::size_t index = 0; index < pattern.bytes.size(); ++index) {
        if (pattern.mask[index] == 'x' && candidate[index] != pattern.bytes[index]) {
            return false;
        }
    }
    return true;
}

bool ValidatePattern(const std::uint8_t* imageBase,
                     const IMAGE_SECTION_HEADER& text,
                     const Pattern& pattern,
                     void*& result,
                     std::wstring& error) {
    if (pattern.mask.size() != pattern.bytes.size()) {
        error = std::wstring(pattern.name) + L": internal signature length mismatch";
        return false;
    }
    const auto* textStart = imageBase + text.VirtualAddress;
    const std::size_t textSize = text.Misc.VirtualSize;
    if (textSize < pattern.bytes.size()) {
        error = std::wstring(pattern.name) + L": .text is smaller than the signature";
        return false;
    }

    std::size_t matches = 0;
    const std::uint8_t* match = nullptr;
    for (std::size_t offset = 0; offset <= textSize - pattern.bytes.size(); ++offset) {
        if (Matches(textStart + offset, pattern)) {
            ++matches;
            match = textStart + offset;
            if (matches > 1) {
                break;
            }
        }
    }

    if (matches != 1) {
        error = std::wstring(pattern.name) + L": expected one signature match, found " +
                std::to_wstring(matches);
        return false;
    }
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(match, &memory, sizeof(memory)) != sizeof(memory) ||
        (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                           PAGE_EXECUTE_WRITECOPY)) == 0) {
        error = std::wstring(pattern.name) + L": target address is not executable memory";
        return false;
    }
    result = const_cast<std::uint8_t*>(match);
    return true;
}

bool RangeIsInImage(const std::size_t imageSize,
                    const std::size_t offset,
                    const std::size_t size) {
    return offset <= imageSize && size <= imageSize - offset;
}

bool ResolveImportedFunction(const std::uint8_t* imageBase,
                             const IMAGE_NT_HEADERS32& nt,
                             const char* importedName,
                             const wchar_t* diagnosticName,
                             void*& result,
                             std::wstring& error) {
    const std::size_t imageSize = nt.OptionalHeader.SizeOfImage;
    const IMAGE_DATA_DIRECTORY imports =
        nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imports.VirtualAddress == 0 || imports.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR) ||
        !RangeIsInImage(imageSize, imports.VirtualAddress, imports.Size)) {
        error = std::wstring(diagnosticName) + L": executable import directory is invalid";
        return false;
    }

    const auto* descriptors = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(
        imageBase + imports.VirtualAddress);
    const std::size_t descriptorCount = imports.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
    std::size_t matchCount = 0;
    void* match = nullptr;
    bool terminated = false;

    for (std::size_t descriptorIndex = 0;
         descriptorIndex < descriptorCount; ++descriptorIndex) {
        const IMAGE_IMPORT_DESCRIPTOR& descriptor = descriptors[descriptorIndex];
        if (descriptor.Name == 0 && descriptor.FirstThunk == 0 &&
            descriptor.OriginalFirstThunk == 0) {
            terminated = true;
            break;
        }
        if (descriptor.FirstThunk == 0 || descriptor.OriginalFirstThunk == 0) {
            continue;
        }

        for (std::size_t thunkIndex = 0;; ++thunkIndex) {
            const std::size_t nameThunkOffset =
                static_cast<std::size_t>(descriptor.OriginalFirstThunk) +
                thunkIndex * sizeof(IMAGE_THUNK_DATA32);
            const std::size_t iatThunkOffset =
                static_cast<std::size_t>(descriptor.FirstThunk) +
                thunkIndex * sizeof(IMAGE_THUNK_DATA32);
            if (!RangeIsInImage(imageSize, nameThunkOffset, sizeof(IMAGE_THUNK_DATA32)) ||
                !RangeIsInImage(imageSize, iatThunkOffset, sizeof(IMAGE_THUNK_DATA32))) {
                error = std::wstring(diagnosticName) + L": import thunk lies outside the image";
                return false;
            }

            const auto* nameThunk = reinterpret_cast<const IMAGE_THUNK_DATA32*>(
                imageBase + nameThunkOffset);
            const auto* iatThunk = reinterpret_cast<const IMAGE_THUNK_DATA32*>(
                imageBase + iatThunkOffset);
            if (nameThunk->u1.AddressOfData == 0) {
                break;
            }
            if (IMAGE_SNAP_BY_ORDINAL32(nameThunk->u1.Ordinal)) {
                continue;
            }

            const std::size_t importByNameOffset = nameThunk->u1.AddressOfData;
            if (!RangeIsInImage(imageSize, importByNameOffset,
                                sizeof(IMAGE_IMPORT_BY_NAME))) {
                error = std::wstring(diagnosticName) + L": import name lies outside the image";
                return false;
            }
            const auto* importByName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                imageBase + importByNameOffset);
            const char* name = reinterpret_cast<const char*>(importByName->Name);
            const std::size_t nameOffset =
                static_cast<std::size_t>(reinterpret_cast<const std::uint8_t*>(name) -
                                         imageBase);
            const void* terminator = std::memchr(name, '\0', imageSize - nameOffset);
            if (terminator == nullptr) {
                error = std::wstring(diagnosticName) + L": unterminated import name";
                return false;
            }
            if (std::strcmp(name, importedName) == 0) {
                match = reinterpret_cast<void*>(
                    static_cast<std::uintptr_t>(iatThunk->u1.Function));
                ++matchCount;
            }
        }
    }

    if (!terminated || matchCount != 1) {
        error = std::wstring(diagnosticName) + L": expected one named import, found " +
                std::to_wstring(matchCount);
        return false;
    }
    result = match;
    MEMORY_BASIC_INFORMATION memory{};
    if (result == nullptr ||
        VirtualQuery(result, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0) {
        error = std::wstring(diagnosticName) + L": resolved import target is not executable";
        result = nullptr;
        return false;
    }
    return true;
}

} // namespace

bool ResolveAndValidate(const HMODULE executable, Mapping& mapping, std::wstring& error) {
    if (executable == nullptr) {
        error = L"Main executable module is unavailable";
        return false;
    }

    const auto* imageBase = reinterpret_cast<const std::uint8_t*>(executable);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(imageBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        error = L"Invalid DOS header in main executable mapping";
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(imageBase + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        error = L"Invalid PE32 header in main executable mapping";
        return false;
    }

    const IMAGE_SECTION_HEADER* text = nullptr;
    const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
    for (unsigned int index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        if (std::memcmp(sections[index].Name, ".text", 5) == 0) {
            text = &sections[index];
            break;
        }
    }
    if (text == nullptr || (text->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
        error = L"Executable .text section was not found";
        return false;
    }

    const Pattern enginePattern{
        kEngineRunBytes,
        "xxxxxx????xxxxxxxxxxx????xxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxx",
        L"cEngine::Run"};
    const Pattern updaterPattern{
        kUpdaterRunMessageBytes, "xxxxxxxxxxxxxxxxxxxxxxxx",
        L"cUpdater::RunMessage"};
    const Pattern scenePattern{
        kSceneRenderBytes, "xxxxxxxx????xxxxxxxxxxxx",
        L"cScene::Render"};
    const Pattern cameraSetPositionPattern{
        kCameraSetPositionBytes, "xxxxxxxxxxxxxxxxxxxxxxxx",
        L"cCamera::SetPosition"};
    const Pattern cameraSetPitchPattern{
        kCameraSetPitchBytes, "xxxxxxxxxxxxxxxxxxxxxxxx",
        L"cCamera::SetPitch"};
    const Pattern cameraSetYawPattern{
        kCameraSetYawBytes, "xxxxxxxxxxxxxxxxxxxxxxxx",
        L"cCamera::SetYaw"};
    const Pattern cameraSetRollPattern{
        kCameraSetRollBytes, "xxxxxxxxxxxxxxxxxxxxxxxx",
        L"cCamera::SetRoll"};
    const Pattern cameraAddPitchPattern{
        kCameraAddPitchBytes, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
        L"cCamera::AddPitch"};
    const Pattern cameraAddYawPattern{
        kCameraAddYawBytes, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
        L"cCamera::AddYaw"};
    const Pattern luxPlayerAddYawPattern{
        kLuxPlayerAddYawBytes, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
        L"cLuxPlayer::AddYaw"};
    const Pattern luxPlayerAddPitchPattern{
        kLuxPlayerAddPitchBytes, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
        L"cLuxPlayer::AddPitch"};
    const Pattern playerHandsPostUpdatePattern{
        kPlayerHandsPostUpdateBytes, "xxxxxxxxxxxxxxxxxxx",
        L"cLuxPlayerHands::PostUpdate"};
    const Pattern luxPropOnUpdatePattern{
        kLuxPropOnUpdateBytes, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
        L"iLuxProp::OnUpdate"};
    const Pattern luxEnemyOnUpdatePattern{
        kLuxEnemyOnUpdateBytes, "xxxxxxxxxxxxxxxxxxxxxx????xxxxxx",
        L"iLuxEnemy::OnUpdate"};
    const Pattern entityGetWorldMatrixPattern{
        kEntityGetWorldMatrixBytes, "xxxxxxxxxxxxx",
        L"iEntity3D::GetWorldMatrix"};
    const Pattern entityGetEntityParentPattern{
        kEntityGetEntityParentBytes, "xxxxxxx",
        L"iEntity3D::GetEntityParent"};
    const Pattern physicsBodyGetEnabledPattern{
        kPhysicsBodyGetEnabledBytes, "xxxxxxxxx????xxxxxxxxx",
        L"cPhysicsBodyNewton::GetEnabled"};
    const Pattern newtonTransformCallbackPattern{
        kNewtonTransformCallbackBytes, "xxxxxxxxxx????xxxxxxxxxxxxxxxxxx",
        L"cPhysicsBodyNewton transform callback"};
    const Pattern meshEntityUpdateGraphicsPattern{
        kMeshEntityUpdateGraphicsBytes, "xxxxxxxxxxxxxxxxxxxxxxxx",
        L"cMeshEntity::UpdateGraphicsForFrame"};
    const Pattern particleEmitterUpdateGraphicsPattern{
        kParticleEmitterUpdateGraphicsBytes, "xxxxxxxxxx????xxxxxxxxxx",
        L"iParticleEmitter::UpdateGraphicsForViewport"};
    const Pattern imageTrailRenderEffectPattern{
        kImageTrailRenderEffectBytes, "xxxxxxxxxxxxxxxxxxxxx????xxxxxxx",
        L"cPostEffect_ImageTrail::RenderEffect"};
    const Pattern subMeshGetModelMatrixPattern{
        kSubMeshGetModelMatrixBytes, "xxxxxxxxxxxxxxxxxxxxxxxxx????xxx",
        L"cSubMeshEntity::GetModelMatrix"};
    const Pattern mouseSdlUpdatePattern{
        kMouseSdlUpdateBytes, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
        L"cMouseSDL::Update"};
    const Pattern mouseSdlGetRelPositionPattern{
        kMouseSdlGetRelPositionBytes, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
        L"cMouseSDL::GetRelPosition"};
    const Pattern guiSendMousePosPattern{
        kGuiSendMousePosBytes, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
        L"cGui::SendMousePos"};
    const Pattern luxUpdateGamePlayerInputPattern{
        kLuxUpdateGamePlayerInputBytes, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
        L"cLuxInputHandler::UpdateGamePlayerInput"};

    return ValidatePattern(imageBase, *text, enginePattern,
                           mapping.engineRun, error) &&
           ValidatePattern(imageBase, *text, updaterPattern,
                           mapping.updaterRunMessage, error) &&
           ValidatePattern(imageBase, *text, scenePattern,
                           mapping.sceneRender, error) &&
           ValidatePattern(imageBase, *text, cameraSetPositionPattern,
                           mapping.cameraSetPosition, error) &&
           ValidatePattern(imageBase, *text, cameraSetPitchPattern,
                           mapping.cameraSetPitch, error) &&
           ValidatePattern(imageBase, *text, cameraSetYawPattern,
                           mapping.cameraSetYaw, error) &&
           ValidatePattern(imageBase, *text, cameraSetRollPattern,
                           mapping.cameraSetRoll, error) &&
           ValidatePattern(imageBase, *text, cameraAddPitchPattern,
                           mapping.cameraAddPitch, error) &&
           ValidatePattern(imageBase, *text, cameraAddYawPattern,
                           mapping.cameraAddYaw, error) &&
           ValidatePattern(imageBase, *text, luxPlayerAddYawPattern,
                           mapping.luxPlayerAddYaw, error) &&
           ValidatePattern(imageBase, *text, luxPlayerAddPitchPattern,
                           mapping.luxPlayerAddPitch, error) &&
           ValidatePattern(imageBase, *text, playerHandsPostUpdatePattern,
                           mapping.playerHandsPostUpdate, error) &&
           ValidatePattern(imageBase, *text, luxPropOnUpdatePattern,
                           mapping.luxPropOnUpdate, error) &&
           ValidatePattern(imageBase, *text, luxEnemyOnUpdatePattern,
                           mapping.luxEnemyOnUpdate, error) &&
           ValidatePattern(imageBase, *text, entityGetWorldMatrixPattern,
                           mapping.entityGetWorldMatrix, error) &&
           ValidatePattern(imageBase, *text, entityGetEntityParentPattern,
                           mapping.entityGetEntityParent, error) &&
           ValidatePattern(imageBase, *text, physicsBodyGetEnabledPattern,
                           mapping.physicsBodyGetEnabled, error) &&
           ValidatePattern(imageBase, *text, newtonTransformCallbackPattern,
                           mapping.newtonTransformCallback, error) &&
           ResolveImportedFunction(imageBase, *nt,
                                   "NewtonBodyGetUserData",
                                   L"NewtonBodyGetUserData",
                                   mapping.newtonBodyGetUserData, error) &&
           ValidatePattern(imageBase, *text, meshEntityUpdateGraphicsPattern,
                           mapping.meshEntityUpdateGraphics, error) &&
           ValidatePattern(imageBase, *text, particleEmitterUpdateGraphicsPattern,
                           mapping.particleEmitterUpdateGraphics, error) &&
           ValidatePattern(imageBase, *text, imageTrailRenderEffectPattern,
                           mapping.imageTrailRenderEffect, error) &&
           ValidatePattern(imageBase, *text, subMeshGetModelMatrixPattern,
                           mapping.subMeshGetModelMatrix, error) &&
           ValidatePattern(imageBase, *text, mouseSdlUpdatePattern,
                           mapping.mouseSdlUpdate, error) &&
           ValidatePattern(imageBase, *text, mouseSdlGetRelPositionPattern,
                           mapping.mouseSdlGetRelPosition, error) &&
           ValidatePattern(imageBase, *text, guiSendMousePosPattern,
                           mapping.guiSendMousePos, error) &&
           ResolveImportedFunction(imageBase, *nt,
                                   "SDL_GetRelativeMouseState",
                                   L"SDL_GetRelativeMouseState",
                                   mapping.sdlGetRelativeMouseState, error) &&
           ResolveImportedFunction(imageBase, *nt,
                                   "SDL_PumpEvents",
                                   L"SDL_PumpEvents",
                                   mapping.sdlPumpEvents, error) &&
           ResolveImportedFunction(imageBase, *nt,
                                   "SDL_GetTicks",
                                   L"SDL_GetTicks",
                                   mapping.sdlGetTicks, error) &&
           ValidatePattern(imageBase, *text, luxUpdateGamePlayerInputPattern,
                           mapping.luxUpdateGamePlayerInput, error);
}

} // namespace atdd::target
