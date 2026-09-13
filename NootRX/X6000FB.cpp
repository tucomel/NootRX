// Copyright © 2023-2024 ChefKiss. Licensed under the Thou Shalt Not Profit License version 1.5.
// See LICENSE for details.

#include "X6000FB.hpp"
#include "NootRX.hpp"
#include "PatcherPlus.hpp"
#include <Headers/kern_api.hpp>

static const char *pathRadeonX6000Framebuffer =
    "/System/Library/Extensions/AMDRadeonX6000Framebuffer.kext/Contents/MacOS/AMDRadeonX6000Framebuffer";

// Native 22H730 getPixelInformation table indices, not nominal bits-per-pixel values.
static constexpr IOIndex kNative8BpcDepth = 1;
static constexpr IOIndex kNative10BpcDepth = 2;

static KernelPatcher::KextInfo kextRadeonX6000Framebuffer {
    "com.apple.kext.AMDRadeonX6000Framebuffer",
    &pathRadeonX6000Framebuffer,
    1,
    {},
    {},
    KernelPatcher::KextInfo::Unloaded,
};

X6000FB *X6000FB::callback = nullptr;

void X6000FB::init() {
    SYSLOG("X6000FB", "Module initialised");

    callback = this;

    lilu.onKextLoadForce(&kextRadeonX6000Framebuffer);
}

bool X6000FB::processKext(KernelPatcher &patcher, size_t id, mach_vm_address_t slide, size_t size) {
    if (kextRadeonX6000Framebuffer.loadIndex == id) {
        NootRXMain::callback->ensureRMMIO();

        CAILAsicCapsEntry *orgAsicCapsTable = nullptr;

        SolveRequestPlus solveRequest {"__ZL20CAIL_ASIC_CAPS_TABLE", orgAsicCapsTable, kCailAsicCapsTablePattern};
        PANIC_COND(!solveRequest.solve(patcher, id, slide, size), "X6000FB", "Failed to resolve CAIL_ASIC_CAPS_TABLE");

        if (!NootRXMain::callback->attributes.isNavi21()) {
            RouteRequestPlus request {"__ZNK32AMDRadeonX6000_AmdAsicInfoNavi2327getEnumeratedRevisionNumberEv",
                wrapGetEnumeratedRevision};
            PANIC_COND(!request.route(patcher, id, slide, size), "X6000FB",
                "Failed to route getEnumeratedRevisionNumber");
        }

        if (NootRXMain::callback->rdFlags.force8Bpc) {
            /*
             * 22H730 keeps the depth index in setDisplayMode and obtains the
             * complete pixel record through getPixelInformation.  Route both
             * or neither: changing only one can make IOFramebuffer allocate an
             * ARGB2101010 surface while DAL programs ARGB8888 (or vice versa),
             * which would create the exact class of corruption under test.
             * KernelPatcher resolves every symbol in routeMultiple before it
             * writes either prologue, so an unsupported driver fails closed.
             */
            KernelPatcher::RouteRequest requests[] = {
                {"__ZN35AMDRadeonX6000_AmdRadeonFramebuffer14setDisplayModeEii", wrapSetDisplayMode,
                    this->orgSetDisplayMode},
                {"__ZN35AMDRadeonX6000_AmdRadeonFramebuffer19getPixelInformationEiiiP18IOPixelInformation",
                    wrapGetPixelInformation, this->orgGetPixelInformation},
            };
            PANIC_COND(!patcher.routeMultiple(id, requests, slide, size), "X6000FB",
                "Failed to route the coherent 8-bpc mode pair");
            NootRXMain::callback->appendLog(
                "NootRX_fix: [8BPC] Routed setDisplayMode + getPixelInformation (depth 2 -> 1)\n");
        }

        if (ADDPR(debugEnabled)) {
            RouteRequestPlus requests[] = {
                {"__ZN24AMDRadeonX6000_AmdLogger15initWithPciInfoEP11IOPCIDevice", wrapInitWithPciInfo,
                    this->orgInitWithPciInfo},
                {"__ZN34AMDRadeonX6000_AmdRadeonController10doGPUPanicEPKcz", wrapDoGPUPanic},
                {"_dm_logger_write", wrapDmLoggerWrite, kDmLoggerWritePattern},
            };
            PANIC_COND(!RouteRequestPlus::routeAll(patcher, id, requests, slide, size), "X6000FB",
                "Failed to route debug symbols");
        }

        PANIC_COND(MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) != KERN_SUCCESS, "X6000FB",
            "Failed to enable kernel writing");
        orgAsicCapsTable[0].familyId = AMDGPU_FAMILY_NAVI;
        orgAsicCapsTable[0].deviceId = NootRXMain::callback->deviceId;
        orgAsicCapsTable[0].revNo = NootRXMain::callback->devRevision;
        orgAsicCapsTable[0].emulatedRevNo =
            static_cast<UInt32>(NootRXMain::callback->enumRevision) + NootRXMain::callback->devRevision;
        orgAsicCapsTable[0].revId = NootRXMain::callback->pciRevision;
        orgAsicCapsTable[0].caps = ddiCapsNavi2Universal;
        MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);
        DBGLOG("X6000FB", "Applied DDI Caps patches");

        if (ADDPR(debugEnabled)) {
            auto *logEnableMaskMinors =
                patcher.solveSymbol<void *>(id, "__ZN14AmdDalDmLogger19LogEnableMaskMinorsE", slide, size);
            patcher.clearError();

            if (logEnableMaskMinors == nullptr) {
                size_t offset = 0;
                PANIC_COND(!KernelPatcher::findPattern(kDalDmLoggerShouldLogPartialPattern,
                               kDalDmLoggerShouldLogPartialPatternMask, arrsize(kDalDmLoggerShouldLogPartialPattern),
                               reinterpret_cast<const void *>(slide), size, &offset),
                    "X6000FB", "Failed to solve LogEnableMaskMinors");
                auto *instAddr = reinterpret_cast<UInt8 *>(slide + offset);
                // inst + instSize + imm32 = addr
                logEnableMaskMinors = instAddr + 7 + *reinterpret_cast<SInt32 *>(instAddr + 3);
            }

            PANIC_COND(MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) != KERN_SUCCESS, "X6000FB",
                "Failed to enable kernel writing");
            memset(logEnableMaskMinors, 0xFF, 0x80);    // Enable all DalDmLogger logs
            MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);

            // Enable all Display Core and BiosParserHelper logs
            const LookupPatchPlus patches[] = {
                {&kextRadeonX6000Framebuffer, kInitPopulateDcInitDataOriginal, kInitPopulateDcInitDataPatched, 1},
                {&kextRadeonX6000Framebuffer, kBiosParserHelperInitWithDataOriginal,
                    kBiosParserHelperInitWithDataPatched, 1},
            };
            PANIC_COND(!LookupPatchPlus::applyAll(patcher, patches, slide, size), "X6000FB",
                "Failed to apply debug enablement patches");
        }

        return true;
    }

    return false;
}

UInt32 X6000FB::wrapGetEnumeratedRevision(void *) { return NootRXMain::callback->enumRevision; }

bool X6000FB::wrapInitWithPciInfo(void *that, void *pciDevice) {
    auto ret = FunctionCast(wrapInitWithPciInfo, callback->orgInitWithPciInfo)(that, pciDevice);
    getMember<UInt64>(that, 0x28) = 0xFFFFFFFFFFFFFFFF;    // Enable all log types
    getMember<UInt32>(that, 0x30) = 0xFF;                  // Enable all log severities
    return ret;
}

IOReturn X6000FB::wrapSetDisplayMode(void *that, IODisplayModeID displayMode, IOIndex depth) {
    const auto nativeDepth = depth == kNative10BpcDepth ? kNative8BpcDepth : depth;
    if (nativeDepth != depth) {
        NootRXMain::callback->appendLog(
            "NootRX_fix: [8BPC] setDisplayMode mode=0x%X remapped depth %d -> %d\n",
            static_cast<UInt32>(displayMode), depth, nativeDepth);
    }
    return FunctionCast(wrapSetDisplayMode, callback->orgSetDisplayMode)(that, displayMode, nativeDepth);
}

IOReturn X6000FB::wrapGetPixelInformation(void *that, IODisplayModeID displayMode, IOIndex depth, IOIndex aperture,
    IOPixelInformation *pixelInfo) {
    const auto nativeDepth = depth == kNative10BpcDepth ? kNative8BpcDepth : depth;
    return FunctionCast(wrapGetPixelInformation, callback->orgGetPixelInformation)(
        that, displayMode, nativeDepth, aperture, pixelInfo);
}

void X6000FB::wrapDoGPUPanic(void *, char const *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    auto *buf = static_cast<char *>(IOMalloc(1000));
    bzero(buf, 1000);
    vsnprintf(buf, 1000, fmt, va);
    va_end(va);

    DBGLOG("X6000FB", "doGPUPanic: %s", buf);
    IOSleep(10000);
    panic("%s", buf);
}

constexpr static const char *LogTypes[] = {
    "Error",
    "Warning",
    "Debug",
    "DC_Interface",
    "DTN",
    "Surface",
    "HW_Hotplug",
    "HW_LKTN",
    "HW_Mode",
    "HW_Resume",
    "HW_Audio",
    "HW_HPDIRQ",
    "MST",
    "Scaler",
    "BIOS",
    "BWCalcs",
    "BWValidation",
    "I2C_AUX",
    "Sync",
    "Backlight",
    "Override",
    "Edid",
    "DP_Caps",
    "Resource",
    "DML",
    "Mode",
    "Detect",
    "LKTN",
    "LinkLoss",
    "Underflow",
    "InterfaceTrace",
    "PerfTrace",
    "DisplayStats",
};

// Needed to prevent stack overflow
void X6000FB::wrapDmLoggerWrite(void *, const UInt32 logType, const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    auto *message = static_cast<char *>(IOMalloc(0x1000));
    vsnprintf(message, 0x1000, fmt, va);
    va_end(va);
    auto *epilogue = message[strnlen(message, 0x1000) - 1] == '\n' ? "" : "\n";
    if (logType < arrsize(LogTypes)) {
        kprintf("[%s]\t%s%s", LogTypes[logType], message, epilogue);
    } else {
        kprintf("%s%s", message, epilogue);
    }
    IOFree(message, 0x1000);
}
