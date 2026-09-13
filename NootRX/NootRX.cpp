// Copyright © 2023-2024 ChefKiss. Licensed under the Thou Shalt Not Profit License version 1.5.
// See LICENSE for details.

#include "NootRX.hpp"
#include "Firmware.hpp"
#include "Model.hpp"
#include "PatcherPlus.hpp"
#include <Headers/kern_api.hpp>
#include <Headers/kern_devinfo.hpp>
#include <Headers/kern_file.hpp>
#include <IOKit/IOCatalogue.h>

static const char *pathAGDP = "/System/Library/Extensions/AppleGraphicsControl.kext/Contents/PlugIns/"
                              "AppleGraphicsDevicePolicy.kext/Contents/MacOS/AppleGraphicsDevicePolicy";

static KernelPatcher::KextInfo kextAGDP {
    "com.apple.driver.AppleGraphicsDevicePolicy",
    &pathAGDP,
    1,
    {},
    {},
    KernelPatcher::KextInfo::Unloaded,
};

NootRXMain *NootRXMain::callback = nullptr;

void NootRXMain::init() {
    SYSLOG("NootRX", "Copyright 2023-2024 ChefKiss. If you've paid for this, you've been scammed.");

    switch (getKernelVersion()) {
        case KernelVersion::BigSur:
            this->attributes.setBigSur();
            break;
        case KernelVersion::Monterey:
            break;
        case KernelVersion::Ventura:
            this->attributes.setVenturaAndLater();
            break;
        case KernelVersion::Sonoma:
            this->attributes.setVenturaAndLater();
            if (getKernelMinorVersion() >= 4) { this->attributes.setSonoma1404AndLater(); }
            break;
        case KernelVersion::Sequoia:
        case KernelVersion::Tahoe:
            this->attributes.setVenturaAndLater();
            this->attributes.setSonoma1404AndLater();
            break;
        default:
            PANIC("NootRX", "Unsupported kernel version %d", getKernelVersion());
    }

    DBGLOG("NootRX", "isBigSur: %s", this->attributes.isBigSur() ? "yes" : "no");
    DBGLOG("NootRX", "isVenturaAndLater: %s", this->attributes.isVenturaAndLater() ? "yes" : "no");
    DBGLOG("NootRX", "isSonoma1404AndLater: %s", this->attributes.isSonoma1404AndLater() ? "yes" : "no");

    SYSLOG("NootRX_fix", "PowerColor RX 6900 XT Red Devil Fix initialized");

    callback = this;

    this->logLock = IOSimpleLockAlloc();
    this->logBuffer = static_cast<char *>(IOMalloc(kMaxLogBufferSize));
    if (this->logBuffer) {
        bzero(this->logBuffer, kMaxLogBufferSize);
        this->logBufferLen = 0;
    }

    this->debugDumpCall = thread_call_allocate(saveLogOnDisk, this);
    if (this->debugDumpCall) {
        thread_call_enter(this->debugDumpCall);
    }

    lilu.onKextLoadForce(&kextAGDP);

    this->dyldpatches.init();
    this->x6000fb.init();
    this->hwlibs.init();
    this->x6000.init();

    lilu.onPatcherLoadForce(
        [](void *user, KernelPatcher &patcher) { static_cast<NootRXMain *>(user)->processPatcher(patcher); }, this);
    lilu.onKextLoadForce(
        nullptr, 0,
        [](void *user, KernelPatcher &patcher, size_t id, mach_vm_address_t slide, size_t size) {
            static_cast<NootRXMain *>(user)->processKext(patcher, id, slide, size);
        },
        this);
}

void NootRXMain::appendLog(const char *fmt, ...) {
    char tmp[1024];
    va_list va;
    va_start(va, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, va);
    va_end(va);

    IOLog("%s", tmp);
    kprintf("%s", tmp);

    if (logLock && logBuffer) {
        size_t len = strnlen(tmp, sizeof(tmp));
        if (len > 0) {
            IOSimpleLockLock(logLock);
            size_t left = kMaxLogBufferSize - logBufferLen - 1;
            if (left > 0) {
                if (len > left) len = left;
                memcpy(logBuffer + logBufferLen, tmp, len);
                logBufferLen += len;
                logBuffer[logBufferLen] = '\0';
            }
            IOSimpleLockUnlock(logLock);
        }
    }
}

void NootRXMain::saveLogOnDisk(thread_call_param_t param0, thread_call_param_t) {
    auto *main = static_cast<NootRXMain *>(param0);
    if (!main) return;

    // Sleep 20s to allow root filesystem to mount RW
    IOSleep(20000);

    if (main->logLock && main->logBuffer && main->logBufferLen > 0) {
        auto *buf = static_cast<char *>(IOMalloc(main->logBufferLen + 1));
        size_t len = 0;
        if (buf) {
            IOSimpleLockLock(main->logLock);
            len = main->logBufferLen;
            memcpy(buf, main->logBuffer, len);
            buf[len] = '\0';
            IOSimpleLockUnlock(main->logLock);

            int ret = FileIO::writeBufferToFile("/var/log/NootRX_fix.log", buf, len);
            if (ret == 0) {
                IOLog("NootRX_fix: Log file written to /var/log/NootRX_fix.log (%zu bytes)\n", len);
            } else {
                IOLog("NootRX_fix: FileIO::writeBufferToFile to /var/log/NootRX_fix.log returned %d\n", ret);
            }
            IOFree(buf, len + 1);
        }
    }

    if (main->debugDumpCall) {
        thread_call_free(main->debugDumpCall);
        main->debugDumpCall = nullptr;
    }
}

void NootRXMain::processPatcher(KernelPatcher &patcher) {
    auto *devInfo = DeviceInfo::create();
    PANIC_COND(devInfo == nullptr, "NootRX", "DeviceInfo::create failed");

    devInfo->processSwitchOff();

    char slotName[256];
    bzero(slotName, sizeof(slotName));
    for (size_t i = 0, ii = 0; i < devInfo->videoExternal.size(); i++) {
        auto *device = OSDynamicCast(IOPCIDevice, devInfo->videoExternal[i].video);
        if (device == nullptr) { continue; }
        if (WIOKit::readPCIConfigValue(device, WIOKit::kIOPCIConfigVendorID) == WIOKit::VendorID::ATIAMD &&
            (WIOKit::readPCIConfigValue(device, WIOKit::kIOPCIConfigDeviceID) & 0xFF00) == 0x7300) {
            this->dGPU = device;
            snprintf(slotName, arrsize(slotName), "GFX%zu", ii++);
            WIOKit::renameDevice(device, slotName);
            WIOKit::awaitPublishing(device);
            if (device->getProperty("AAPL,slot-name") == nullptr) {
                snprintf(slotName, sizeof(slotName), "Slot-%zu", ii++);
                device->setProperty("AAPL,slot-name", slotName,
                    static_cast<UInt32>(strnlen(slotName, sizeof(slotName)) + 1));
            }
            break;
        }
    }

    PANIC_COND(this->dGPU == nullptr, "NootRX", "Failed to find a compatible GPU");

    UInt8 builtIn[] = {0x00};
    this->dGPU->setProperty("built-in", builtIn, arrsize(builtIn));

    this->deviceId = WIOKit::readPCIConfigValue(this->dGPU, WIOKit::kIOPCIConfigDeviceID);
    this->pciRevision = WIOKit::readPCIConfigValue(this->dGPU, WIOKit::kIOPCIConfigRevisionID);

    SYSLOG_COND(this->dGPU->getProperty("model") != nullptr, "NootRX",
        "WARNING!!! Attempted to manually override the model, this is no longer supported!!");
    auto *model = getBranding(this->deviceId, this->pciRevision);
    auto modelLen = static_cast<UInt32>(strlen(model) + 1);
    this->dGPU->setProperty("model", const_cast<char *>(model), modelLen);
    if (model[11] == 'P' && model[12] == 'r' && model[13] == 'o' && model[14] == ' ') {
        this->dGPU->setProperty("ATY,FamilyName", const_cast<char *>("Radeon Pro"), 11);
        // Without AMD Radeon Pro prefix
        this->dGPU->setProperty("ATY,DeviceName", const_cast<char *>(model) + 15, modelLen - 15);
    } else {
        this->dGPU->setProperty("ATY,FamilyName", const_cast<char *>("Radeon RX"), 10);
        // Without AMD Radeon RX prefix
        this->dGPU->setProperty("ATY,DeviceName", const_cast<char *>(model) + 14, modelLen - 14);
    }

    switch (this->deviceId) {
        case 0x73A2:
        case 0x73A3:
        case 0x73A5:
        case 0x73AB:
        case 0x73AF:
        case 0x73BF:
            this->attributes.setNavi21();
            this->enumRevision = 0x28;
            break;
        case 0x73DF:
            PANIC_COND(this->attributes.isBigSur(), "NootRX", "Your GPU requires macOS 12 and newer");
            this->attributes.setNavi22();
            this->enumRevision = 0x32;
            break;
        case 0x73E0:
        case 0x73E1:
        case 0x73E3:
        case 0x73EF:
        case 0x73FF:
            PANIC_COND(this->attributes.isBigSur(), "NootRX", "Your GPU requires macOS 12 and newer");
            if (this->pciRevision == 0xDF) {
                this->attributes.setNavi22();
                this->enumRevision = 0x32;
            } else {
                this->attributes.setNavi23();
                this->enumRevision = 0x3C;
            }
            break;
        default:
            PANIC("NootRX", "Unknown device ID: 0x%04X", this->deviceId);
    }

    DBGLOG("NootRX", "deviceId: 0x%04X", this->deviceId);
    DBGLOG("NootRX", "pciRevision: 0x%X", this->pciRevision);
    DBGLOG("NootRX", "enumRevision: 0x%X", this->enumRevision);
    DBGLOG("NootRX", "isNavi21: %s", this->attributes.isNavi21() ? "yes" : "no");
    DBGLOG("NootRX", "isNavi22: %s", this->attributes.isNavi22() ? "yes" : "no");
    DBGLOG("NootRX", "isNavi23: %s", this->attributes.isNavi23() ? "yes" : "no");

    /*
     * Decision record (v1.0.7): use Apple's native Navi21 implementation on
     * the exact Red Devil device tested under Ventura.
     *
     * Ventura 13.7.8 already includes 0x73BF1002 in both its Navi21
     * framebuffer and accelerator personalities.  Nevertheless, normal
     * NootRX operation replaces those personalities and patches the X6000
     * framebuffer/HWLibs/accelerator binaries: ASIC capability tables,
     * golden registers, PSP/SMU/GC/SDMA firmware paths and version checks are
     * among the state that stops being Apple's native 0x73BF path.
     *
     * The v1.0.5 diagnostic proved that restoring GPUTaskSingleChannel=1 was
     * applied, yet the same mpc2_assert_idle_mpcc timeout remained and a real
     * GFX command from IINA stalled (CompletedTS 0x1E3 versus SubmittedTS
     * 0x1E8), followed by IOAccelDisplayPipe timeout and channel 51 restart.
     * Isolated policy tweaks are therefore no longer a useful discriminator.
     *
     * Restrict this experiment to the exact known pair and exact macOS major
     * version.  Unsupported Navi variants and newer systems retain upstream
     * NootRX behaviour.  Passthrough still keeps the proven IOCatalogue
     * property overrides and the existing AGDP patch, because WhateverGreen
     * is disabled in this EFI; it skips only NootRX's low-level replacement
     * machinery.
     *
     * The validation contract for this test release is Ventura 13.7.8 build
     * 22H730 (Darwin 22.6).  v1.0.6 tried to enforce 22H730 by reading the
     * registry root here.  The 2026-09-13 diagnostic proved that this callback
     * runs before "OS Build Version" is published: the completed IORegistry
     * later contained 22H730, but [MODE] was 0 and the old [XML] path ran.
     * Consequently v1.0.6 never tested native passthrough at all.
     *
     * Use Lilu's already-initialised Darwin major/minor values for the early
     * decision instead.  Darwin 22.6 plus the complete PCI identity keeps the
     * mode narrow enough for this controlled machine without depending on a
     * property whose publication order is too late.  The exact macOS build is
     * deliberately a post-boot release check (sw_vers/os_version.txt); a build
     * other than 22H730 must not be accepted as a valid result for this test.
     */
    auto subsystemVendorId = WIOKit::readPCIConfigValue(this->dGPU, WIOKit::kIOPCIConfigSubSystemVendorID);
    auto subsystemId = WIOKit::readPCIConfigValue(this->dGPU, WIOKit::kIOPCIConfigSubSystemID);
    this->nativeNavi21Passthrough = getKernelVersion() == KernelVersion::Ventura && getKernelMinorVersion() == 6 &&
        this->deviceId == 0x73BF && this->pciRevision == 0xC0 && subsystemVendorId == 0x148C &&
        subsystemId == 0x2408;

    DeviceInfo::deleter(devInfo);

    auto checkFlag = [this](const char *name) -> bool {
        auto prop = this->dGPU->getProperty(name);
        if (!prop) {
            char bootarg[32];
            snprintf(bootarg, sizeof(bootarg), "-%s", name);
            return checkKernelArgument(bootarg);
        }
        if (auto data = OSDynamicCast(OSData, prop)) {
            if (data->getLength() > 0) {
                return static_cast<const UInt8 *>(data->getBytesNoCopy())[0] != 0;
            }
            return false;
        }
        if (auto b = OSDynamicCast(OSBoolean, prop)) return b->isTrue();
        if (auto num = OSDynamicCast(OSNumber, prop)) return num->unsigned32BitValue() != 0;
        return false;
    };

    this->rdFlags.noGfxOff = checkFlag("rd-nogfxoff");
    this->rdFlags.noUlv = checkFlag("rd-noulv");
    this->rdFlags.noVActiveDram = checkFlag("rd-novactivedram");
    /*
     * Decision record (post-1.0.3): keep rd-novactivedram authoritative.
     *
     * The -rd-vactivedram A/B experiment bypassed this protection and caused
     * four IOAccelDisplayPipe timeouts followed by GFX channel resets in less
     * than one minute.  The driver also selected a 96 MHz MCLK state during an
     * active display commit.  The override was therefore removed rather than
     * left as a dangerous hidden switch.
     *
     * Do not replace this with PP_MclkDpmDisabled or CFG_FORCEMAXDPM.  That
     * earlier experiment prevented required cold-boot GDDR6 training and
     * panicked with "GDDR6 Long Training Failed".
     */
    this->rdFlags.noMpo = checkFlag("rd-nompo");
    this->rdFlags.noStutter = checkFlag("rd-nostutter");
    this->rdFlags.floorDpm = checkFlag("rd-floordpm");
    this->rdFlags.noDcc = checkFlag("rd-nodcc");
    this->rdFlags.diag = checkFlag("rd-diag") || ADDPR(debugEnabled) || checkKernelArgument("-NRXDebug");

    this->appendLog("NootRX_fix: [INIT] Detected GPU 0x%04X:0x%02X\n", this->deviceId, this->pciRevision);
    this->appendLog("NootRX_fix: [FLAGS] nogfxoff=%d noulv=%d novactivedram=%d nompo=%d nostutter=%d floordpm=%d nodcc=%d diag=%d\n",
                    this->rdFlags.noGfxOff, this->rdFlags.noUlv, this->rdFlags.noVActiveDram,
                    this->rdFlags.noMpo, this->rdFlags.noStutter, this->rdFlags.floorDpm,
                    this->rdFlags.noDcc, this->rdFlags.diag);

    this->appendLog("NootRX_fix: [MODE] native-navi21-passthrough=%d (Darwin %d.%d + 1002:73BF/C0 + 148C:2408; validate 22H730 post-boot)\n",
                    this->nativeNavi21Passthrough, getKernelVersion(), getKernelMinorVersion());

    if (!this->nativeNavi21Passthrough) {
        this->dyldpatches.processPatcher(patcher);
    } else {
        // Keep a durable IORegistry marker so the post-boot diagnostic can
        // prove this path was selected even if the file log is truncated.
        this->dGPU->setProperty("NootRXNativeNavi21Passthrough", 1, 32);
        this->dGPU->setProperty("NootRXNativeDarwinMinor", getKernelMinorVersion(), 32);
        this->appendLog("NootRX_fix: [NATIVE] Preserving Apple's dyld/video code path; NootRX shared-cache patches skipped\n");
    }

    KernelPatcher::RouteRequest request {"__ZN11IOCatalogue10addDriversEP7OSArrayb", wrapAddDrivers,
        this->orgAddDrivers};
    PANIC_COND(!patcher.routeMultipleLong(KernelPatcher::KernelID, &request, 1), "NootRX",
        "Failed to route addDrivers");

    if (this->rdFlags.diag || ADDPR(debugEnabled)) {
        this->dGPU->setProperty("PP_LogLevel", 0xFFFFFFFF, 32);
        this->dGPU->setProperty("PP_LogSource", 0xFFFFFFFF, 32);
        this->dGPU->setProperty("PP_LogDestination", 0xFFFFFFFF, 32);
        this->dGPU->setProperty("PP_LogField", 0xFFFFFFFF, 32);
        this->dGPU->setProperty("PP_DumpRegister", TRUE, 32);
        this->dGPU->setProperty("PP_DumpSMCTable", TRUE, 32);
        this->dGPU->setProperty("PP_LogDumpTableBuffers", TRUE, 32);
    }
}

static const char *getDriverXMLForBundle(const char *bundleIdentifier, size_t *len) {
    const auto identifierLen = strlen(bundleIdentifier);
    const auto totalLen = identifierLen + 5;
    auto *filename = new char[totalLen];
    memcpy(filename, bundleIdentifier, identifierLen);
    strlcat(filename, ".xml", totalLen);

    const auto &driversXML = getFWByName(filename);
    delete[] filename;

    *len = driversXML.length;
    return reinterpret_cast<const char *>(driversXML.data);
}

static const char *DriverBundleIdentifiers[] = {
    "com.apple.kext.AMDRadeonX6000",
    "com.apple.kext.AMDRadeonX6000HWServices",
    "com.apple.kext.AMDRadeonX6000Framebuffer",
};
static const char *DriverBundleXMLsBigSur[] = {
    nullptr,
    nullptr,
    "com.apple.kext.AMDRadeonX6000Framebuffer_BigSur",
};
static_assert(arrsize(DriverBundleIdentifiers) == arrsize(DriverBundleXMLsBigSur));

static UInt8 matchedDrivers = 0;

static bool isNativeNavi21Personality(OSDictionary *dict) {
    auto *ioClass = OSDynamicCast(OSString, dict->getObject("IOClass"));
    if (ioClass == nullptr || ioClass->getCStringNoCopy() == nullptr) { return false; }

    auto *name = ioClass->getCStringNoCopy();
    return strcmp(name, "AMDRadeonX6000_AMDNavi21GraphicsAccelerator") == 0 ||
        strcmp(name, "AMDRadeonX6000_AmdRadeonControllerNavi21") == 0;
}

void NootRXMain::patchDriverPersonality(OSDictionary *drvDict, bool nativePersonality) {
    auto *ioClass = OSDynamicCast(OSString, drvDict->getObject("IOClass"));
    if (ioClass == nullptr || ioClass->getCStringNoCopy() == nullptr) { return; }

    auto *ioClassName = ioClass->getCStringNoCopy();
    if (strcmp(ioClassName, "AMDRadeonX6000_AMDNavi21GraphicsAccelerator") == 0 ||
        (!nativePersonality && strcmp(ioClassName, "AMDRadeonX6000_AMDNavi23GraphicsAccelerator") == 0)) {
        if (strcmp(ioClassName, "AMDRadeonX6000_AMDNavi21GraphicsAccelerator") == 0) {
            /*
             * Decision record (v1.0.5 rejected by v1.0.6): Ventura's native
             * Navi21 personality already has GPUTaskSingleChannel=1.  Version
             * 1.0.5 restored the same key to NootRX's replacement XML and the
             * IORegistry confirmed it, but artifacts and the GFX/display-pipe
             * reset were unchanged.  In native passthrough we deliberately do
             * not rewrite the scheduler key; Apple's complete personality is
             * kept.  The assignment remains only for unsupported devices that
             * still require NootRX's embedded personality.
             *
             * Do not add an 8-bpc override: AMD pBPC=0x2 means
             * COLOR_DEPTH_888, and the LG EDID declares an 8-bpc input.
             * ARGB2101010 is the compositor surface format, not proof of a
             * 10-bpc DisplayPort wire format.
             */
            if (nativePersonality) {
                callback->appendLog("NootRX_fix: [NATIVE] AMDRadeonX6000: preserving Apple's GPUTaskSingleChannel policy\n");
            } else {
                auto *v1 = OSNumber::withNumber(static_cast<UInt32>(1), 32);
                drvDict->setObject("GPUTaskSingleChannel", v1);
                v1->release();
                callback->appendLog("NootRX_fix: [XML] AMDRadeonX6000: GPUTaskSingleChannel=1 (restore Ventura Navi21 default)\n");
            }
        }
        if (callback->rdFlags.noDcc) {
            drvDict->setObject("GPUDCCDisplayable", kOSBooleanFalse);
            callback->appendLog("NootRX_fix: [%s] AMDRadeonX6000: GPUDCCDisplayable=false (DCC scanout disabled)\n",
                nativePersonality ? "NATIVE" : "XML");
        }
        if (nativePersonality) {
            // Post-boot proof that the native accelerator personality was
            // actually observed and overlaid before catalogue matching.
            callback->dGPU->setProperty("NootRXNativeAcceleratorOverlay", 1, 32);
        }
    }

    if (strcmp(ioClassName, "AMDRadeonX6000_AmdRadeonControllerNavi21") != 0) { return; }

    auto *atyProps = OSDynamicCast(OSDictionary, drvDict->getObject("aty_properties"));
    auto *atyConfig = OSDynamicCast(OSDictionary, drvDict->getObject("aty_config"));
    auto *source = nativePersonality ? "NATIVE" : "XML";

    if (atyProps) {
        if (callback->rdFlags.noGfxOff) {
            auto *v0 = OSNumber::withNumber(static_cast<UInt32>(0), 32);
            atyProps->setObject("PP_GfxOffControl", v0);
            v0->release();
            callback->appendLog("NootRX_fix: [%s] aty_properties: PP_GfxOffControl=0\n", source);
        }
        if (callback->rdFlags.noUlv) {
            auto *v1 = OSNumber::withNumber(static_cast<UInt32>(1), 32);
            atyProps->setObject("PP_DisableULV", v1);
            v1->release();
            callback->appendLog("NootRX_fix: [%s] aty_properties: PP_DisableULV=1\n", source);
        }
        if (callback->rdFlags.noVActiveDram) {
            auto *v1 = OSNumber::withNumber(static_cast<UInt32>(1), 32);
            atyProps->setObject("DalDisableVActiveDramChange", v1);
            v1->release();
            callback->appendLog("NootRX_fix: [%s] aty_properties: DalDisableVActiveDramChange=1\n", source);
        }
        if (callback->rdFlags.noMpo) {
            /*
             * Decision record (v1.0.4 rejected by v1.0.5): AMD Display Core
             * defines PipeSplitPolicy=1 as MPC_SPLIT_AVOID, so v1.0.4 tried
             * DalForceSingleDispPipeSplit=0 plus that policy.  The exact board
             * still logged both mpc2_assert_idle_mpcc warnings and reset after
             * an IOAccelDisplayPipe timeout.  Keep the best observed v1.0.3
             * value (1) and do not reintroduce DalPipeSplitPolicy.
             */
            auto *v1 = OSNumber::withNumber(static_cast<UInt32>(1), 32);
            atyProps->setObject("DalForceSingleDispPipeSplit", v1);
            v1->release();
            callback->appendLog("NootRX_fix: [%s] aty_properties: DalForceSingleDispPipeSplit=1 (1.0.3 baseline)\n",
                source);
        }
        if (callback->rdFlags.noStutter) {
            auto *v1 = OSNumber::withNumber(static_cast<UInt32>(1), 32);
            auto *v0 = OSNumber::withNumber(static_cast<UInt32>(0), 32);
            atyProps->setObject("PP_DisableClockStretcher", v1);
            atyProps->setObject("PP_Falcon_QuickTransition_Enable", v0);
            atyProps->setObject("PP_DisableGFXCLKStutter", v1);
            atyProps->setObject("PP_DisableFCLKStutter", v1);
            atyProps->setObject("PP_DisableMCLKStutter", v1);
            atyProps->setObject("PP_DisableDSCLKStutter", v1);
            v1->release();
            v0->release();
            callback->appendLog("NootRX_fix: [%s] aty_properties: Stutter clocks disabled (1.0.3 baseline)\n", source);
        }
        if (callback->rdFlags.floorDpm) {
            auto *v3 = OSNumber::withNumber(static_cast<UInt32>(3), 32);
            atyProps->setObject("DalForceMinDpmLevel", v3);
            v3->release();
            callback->appendLog("NootRX_fix: [%s] aty_properties: DalForceMinDpmLevel=3 (DPM High floor)\n", source);
        }
    }
    if (atyConfig && (callback->rdFlags.noMpo || callback->rdFlags.noStutter)) {
        atyConfig->setObject("CFG_USE_STUTTER", kOSBooleanFalse);
        atyConfig->setObject("CFG_USE_FBC", kOSBooleanFalse);
        atyConfig->setObject("CFG_USE_CPT", kOSBooleanFalse);
        callback->appendLog("NootRX_fix: [%s] aty_config: CFG_USE_STUTTER=false, CFG_USE_FBC=false, CFG_USE_CPT=false\n",
            source);
    }
    if (nativePersonality) {
        // Paired with NootRXNativeAcceleratorOverlay.  A v1.0.7 run is valid
        // only when both markers and all effective values appear in IORegistry.
        callback->dGPU->setProperty("NootRXNativeControllerOverlay", 1, 32);
    }
}

bool NootRXMain::wrapAddDrivers(void *that, OSArray *array, bool doNubMatching) {
    /*
     * The native path also shallow-clones the caller-owned array.  Each target
     * personality is then deep-cloned below, so neither OSKext's source graph
     * nor IOCatalogue's argument container is mutated.  The original function
     * retains what it needs during registration; we release this private array
     * only after addDrivers returns.
     */
    OSArray *privateArray = nullptr;
    OSArray *workingArray = array;
    if (callback->nativeNavi21Passthrough) {
        privateArray = OSArray::withArray(array);
        if (privateArray == nullptr) {
            callback->appendLog("NootRX_fix: [NATIVE][ERROR] Could not clone addDrivers array; using Apple's unmodified catalogue\n");
            return FunctionCast(wrapAddDrivers, callback->orgAddDrivers)(that, array, doNubMatching);
        }
        workingArray = privateArray;
    }

    UInt32 driverCount = workingArray->getCount();
    for (UInt32 driverIndex = 0; driverIndex < driverCount; driverIndex += 1) {
        OSObject *object = workingArray->getObject(driverIndex);
        PANIC_COND(object == nullptr, "NootRX", "Critical error in addDrivers: Index is out of bounds.");
        auto *dict = OSDynamicCast(OSDictionary, object);
        if (dict == nullptr) { continue; }

        /*
         * Native passthrough overlays Apple's personalities before the
         * original addDrivers starts matching.  Use OSCollection's deep copy
         * and replace exactly the two target array entries: mutating an
         * Apple-owned dictionary in place can unexpectedly affect another
         * owner of that object, while a shallow OSDictionary copy would still
         * share aty_properties/aty_config and defeat copy-on-write.
         *
         * copyCollection retains scalar OSNumber/OSBoolean values and clones
         * nested collections recursively.  replaceObject retains the clone;
         * releasing our creation reference afterward gives the array clear
         * ownership.  Non-target entries and the array shape remain native;
         * embedded XML is never parsed in this mode.
         */
        if (callback->nativeNavi21Passthrough) {
            if (isNativeNavi21Personality(dict)) {
                auto *collectionCopy = dict->copyCollection();
                auto *copy = OSDynamicCast(OSDictionary, collectionCopy);
                if (copy != nullptr) {
                    patchDriverPersonality(copy, true);
                    workingArray->replaceObject(driverIndex, copy);
                    copy->release();
                } else {
                    if (collectionCopy != nullptr) { collectionCopy->release(); }
                    // Leave Apple's unmodified entry intact instead of risking
                    // a partial overlay or a boot panic.  IORegistry will then
                    // expose the native values and make this test invalid.
                    callback->appendLog("NootRX_fix: [NATIVE][ERROR] Could not clone target personality; leaving it unmodified\n");
                }
            }
            continue;
        }

        auto *bundleIdentifier = OSDynamicCast(OSString, dict->getObject("CFBundleIdentifier"));
        if (bundleIdentifier == nullptr || bundleIdentifier->getLength() == 0) { continue; }
        auto *bundleIdentifierCStr = bundleIdentifier->getCStringNoCopy();
        if (bundleIdentifierCStr == nullptr) { continue; }

        for (size_t identifierIndex = 0; identifierIndex < arrsize(DriverBundleIdentifiers); identifierIndex += 1) {
            if ((matchedDrivers & (1U << identifierIndex)) != 0) { continue; }

            if (strcmp(bundleIdentifierCStr, DriverBundleIdentifiers[identifierIndex]) == 0) {
                matchedDrivers |= (1U << identifierIndex);

                DBGLOG("NootRX", "Matched %s, injecting.", bundleIdentifierCStr);

                size_t len;
                auto *driverBundle =
                    callback->attributes.isBigSur() ? DriverBundleXMLsBigSur[identifierIndex] : bundleIdentifierCStr;
                if (driverBundle == nullptr) { driverBundle = bundleIdentifierCStr; }
                auto *driverXML = getDriverXMLForBundle(driverBundle, &len);

                OSString *errStr = nullptr;
                auto *dataUnserialized = OSUnserializeXML(driverXML, len, &errStr);

                PANIC_COND(dataUnserialized == nullptr, "NootRX", "Failed to unserialize driver XML for %s: %s",
                    bundleIdentifierCStr, errStr ? errStr->getCStringNoCopy() : "(nil)");

                auto *drivers = OSDynamicCast(OSArray, dataUnserialized);
                PANIC_COND(drivers == nullptr, "NootRX", "Failed to cast %s driver data", bundleIdentifierCStr);
                UInt32 injectedDriverCount = drivers->getCount();

                workingArray->ensureCapacity(driverCount + injectedDriverCount);

                for (UInt32 injectedDriverIndex = 0; injectedDriverIndex < injectedDriverCount;
                    injectedDriverIndex += 1) {
                    auto *driverObj = drivers->getObject(injectedDriverIndex);
                    if (auto *drvDict = OSDynamicCast(OSDictionary, driverObj)) {
                        patchDriverPersonality(drvDict, false);
                    }
                    workingArray->setObject(driverIndex, drivers->getObject(injectedDriverIndex));
                    driverIndex += 1;
                    driverCount += 1;
                }

                dataUnserialized->release();
                break;
            }
        }
    }

    auto result = FunctionCast(wrapAddDrivers, callback->orgAddDrivers)(that, workingArray, doNubMatching);
    if (privateArray != nullptr) { privateArray->release(); }
    return result;
}

void NootRXMain::ensureRMMIO() {
    if (this->rmmio != nullptr) { return; }

    this->dGPU->setMemoryEnable(true);
    this->dGPU->setBusMasterEnable(true);
    this->rmmio =
        this->dGPU->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress5, kIOMapInhibitCache | kIOMapAnywhere);
    PANIC_COND(this->rmmio == nullptr || this->rmmio->getLength() == 0, "NootRX", "Failed to map RMMIO");
    this->rmmioPtr = reinterpret_cast<UInt32 *>(this->rmmio->getVirtualAddress());
    this->devRevision = (this->readReg32(0xD31) & 0xF000000) >> 0x18;
}

void NootRXMain::processKext(KernelPatcher &patcher, size_t id, mach_vm_address_t slide, size_t size) {
    if (kextAGDP.loadIndex == id) {
        // Apply AGDP patch on all boards including MacPro7,1 (required for commercial PC Navi GPUs)
        const LookupPatchPlus patch {&kextAGDP, kAGDPBoardIDKeyOriginal, kAGDPBoardIDKeyPatched, 1};
        PANIC_COND(!patch.apply(patcher, slide, size), "NootRX", "Failed to apply AGDP patch");

        DBGLOG("NootRX", "Processed Apple Graphics Device Policy");
        if (callback) {
            callback->appendLog("NootRX_fix: [AGDP] Applied board-id -> applehax patch for board %s\n",
                BaseDeviceInfo::get().boardIdentifier);
        }
    } else if (this->nativeNavi21Passthrough) {
        /*
         * v1.0.7 native passthrough intentionally leaves Apple's exact
         * 0x73BF/C0 framebuffer, HWServices/HWLibs and accelerator binaries
         * untouched.  Do not call ensureRMMIO here: doing so would only be a
         * precursor to NootRX's capability/firmware substitution and would
         * make this experiment unable to distinguish Apple's implementation
         * from the prior releases.
         *
         * AGDP is handled above, not skipped.  The test EFI has WhateverGreen
         * present but disabled, so its agdpmod=pikera boot argument cannot be
         * relied upon to apply the policy patch.
         */
        return;
    } else if (this->x6000fb.processKext(patcher, id, slide, size)) {
        DBGLOG("NootRX", "Processed Framebuffer");
    } else if (this->hwlibs.processKext(patcher, id, slide, size)) {
        DBGLOG("NootRX", "Processed HW Library");
    } else if (this->x6000.processKext(patcher, id, slide, size)) {
        DBGLOG("NootRX", "Processed Accelerator");
    }
}

UInt32 NootRXMain::readReg32(UInt32 reg) {
    if ((reg * sizeof(UInt32)) < this->rmmio->getLength()) {
        return this->rmmioPtr[reg];
    } else {
        this->rmmioPtr[mmPCIE_INDEX2] = reg;
        return this->rmmioPtr[mmPCIE_DATA2];
    }
}

void NootRXMain::writeReg32(UInt32 reg, UInt32 val) {
    if ((reg * sizeof(UInt32)) < this->rmmio->getLength()) {
        this->rmmioPtr[reg] = val;
    } else {
        this->rmmioPtr[mmPCIE_INDEX2] = reg;
        this->rmmioPtr[mmPCIE_DATA2] = val;
    }
}

const char *NootRXMain::getGCPrefix() {
    if (this->attributes.isNavi21()) {
        return "gc_10_3_";
    } else if (this->attributes.isNavi22()) {
        return "gc_10_3_2_";
    } else if (this->attributes.isNavi23()) {
        return "gc_10_3_4_";
    } else {
        UNREACHABLE();
    }
}
