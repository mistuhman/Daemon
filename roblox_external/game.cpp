#include "game.h"
#include "memory.h"
#include "offsets.h"
#include "cache.h"
#ifdef __linux__
#include "offsets_dynamic.h"
#include "platform/sober_scanner.h"
#include "platform/platform.h"
#endif

namespace game {
    static instance ReadVisualEngine(uintptr_t base) {
        instance ve = read<instance>(base + Offsets::VisualEngine::Pointer);
        return ve;
    }

    instance ReadDatamodel(uintptr_t base) {
#ifdef __linux__
        // Linux (Sober): try PID-local scanner first — it resolves DataModel
        // directly via TaskScheduler → RenderJob chain relative to libBase.
        // The base is a verified ELF load bias, never a Windows PE base.
        if (offsets_dynamic::Ensure()) {
            uintptr_t dmAddr = offsets_dynamic::GetDataModel();
            if (is_valid_address(dmAddr)) {
                return instance{dmAddr};
            }
        }
        // A verified Linux module does not validate Windows field offsets.
        return instance{};
#else
        instance ve = ReadVisualEngine(base);
        if (!ve.is_valid())
            return instance{};

        uintptr_t fakedm = read<uintptr_t>(ve.address + Offsets::VisualEngine::FakeDataModel);
        if (!is_valid_address(fakedm))
            return instance{};

        instance dm = read<instance>(fakedm + Offsets::FakeDataModel::RealDataModel);
        return dm;
#endif
    }

    std::string GetGameName(uintptr_t base) {
        instance dm = ReadDatamodel(base);
        if (!dm.is_valid()) return "";
        return dm.get_name();
    }

    uint64_t GetPlaceId(uintptr_t base) {
        instance dm = ReadDatamodel(base);
        if (!dm.is_valid()) return 0;
        return read<uint64_t>(dm.address + Offsets::DataModel::PlaceId);
    }
}
