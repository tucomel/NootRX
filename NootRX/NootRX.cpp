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

    this->dyldpatches.processPatcher(patcher);

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

bool NootRXMain::wrapAddDrivers(void *that, OSArray *array, bool doNubMatching) {
    UInt32 driverCount = array->getCount();
    for (UInt32 driverIndex = 0; driverIndex < driverCount; driverIndex += 1) {
        OSObject *object = array->getObject(driverIndex);
        PANIC_COND(object == nullptr, "NootRX", "Critical error in addDrivers: Index is out of bounds.");
        auto *dict = OSDynamicCast(OSDictionary, object);
        if (dict == nullptr) { continue; }
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

                array->ensureCapacity(driverCount + injectedDriverCount);

                for (UInt32 injectedDriverIndex = 0; injectedDriverIndex < injectedDriverCount;
                     injectedDriverIndex += 1) {
                    auto *driverObj = drivers->getObject(injectedDriverIndex);
                    if (auto *drvDict = OSDynamicCast(OSDictionary, driverObj)) {
                        auto *ioClass = OSDynamicCast(OSString, drvDict->getObject("IOClass"));
                        if (ioClass && (strcmp(ioClass->getCStringNoCopy(), "AMDRadeonX6000_AMDNavi21GraphicsAccelerator") == 0 ||
                                        strcmp(ioClass->getCStringNoCopy(), "AMDRadeonX6000_AMDNavi23GraphicsAccelerator") == 0)) {
                            if (strcmp(ioClass->getCStringNoCopy(), "AMDRadeonX6000_AMDNavi21GraphicsAccelerator") == 0) {
                                /*
                                 * Decision record (v1.0.5): restore an Apple Navi 21
                                 * scheduler invariant lost by NootRX's replacement XML.
                                 *
                                 * Ventura 13.7.8 sets GPUTaskSingleChannel=1 on its
                                 * native Navi 21 accelerator personality.  NootRX
                                 * replaces that personality, but its embedded copy
                                 * omitted the key.  The v1.0.4 diagnostic then captured
                                 * an IOAccelDisplayPipe transaction timeout while every
                                 * reported render/compute channel had CompletedTS equal
                                 * to SubmittedTS.  Restore Apple's value so display flips
                                 * use the channel model expected by the native 0x73BF/C0
                                 * driver.  This does not disable Metal or OpenGL.
                                 *
                                 * Do not add an 8-bpc override here: AMD's pBPC=0x2 is
                                 * COLOR_DEPTH_888 (8 bpc), and the LG EDID also declares
                                 * an 8-bpc digital input.  macOS's ARGB2101010 label is
                                 * the compositor framebuffer format, not proof of a
                                 * 10-bpc DisplayPort link.
                                 */
                                auto *v1 = OSNumber::withNumber(static_cast<UInt32>(1), 32);
                                drvDict->setObject("GPUTaskSingleChannel", v1);
                                v1->release();
                                callback->appendLog("NootRX_fix: [XML] AMDRadeonX6000: GPUTaskSingleChannel=1 (restore Ventura Navi21 default)\n");
                            }
                            if (callback->rdFlags.noDcc) {
                                drvDict->setObject("GPUDCCDisplayable", kOSBooleanFalse);
                                callback->appendLog("NootRX_fix: [XML] AMDRadeonX6000: GPUDCCDisplayable=false (DCC scanout disabled)\n");
                            }
                        }
                        if (ioClass && strcmp(ioClass->getCStringNoCopy(), "AMDRadeonX6000_AmdRadeonControllerNavi21") == 0) {
                            auto *atyProps = OSDynamicCast(OSDictionary, drvDict->getObject("aty_properties"));
                            auto *atyConfig = OSDynamicCast(OSDictionary, drvDict->getObject("aty_config"));
                            if (atyProps) {
                                if (callback->rdFlags.noGfxOff) {
                                    auto *v0 = OSNumber::withNumber(static_cast<UInt32>(0), 32);
                                    atyProps->setObject("PP_GfxOffControl", v0);
                                    v0->release();
                                    callback->appendLog("NootRX_fix: [XML] aty_properties: PP_GfxOffControl=0\n");
                                }
                                if (callback->rdFlags.noUlv) {
                                    auto *v1 = OSNumber::withNumber(static_cast<UInt32>(1), 32);
                                    atyProps->setObject("PP_DisableULV", v1);
                                    v1->release();
                                    callback->appendLog("NootRX_fix: [XML] aty_properties: PP_DisableULV=1\n");
                                }
                                if (callback->rdFlags.noVActiveDram) {
                                    auto *v1 = OSNumber::withNumber(static_cast<UInt32>(1), 32);
                                    atyProps->setObject("DalDisableVActiveDramChange", v1);
                                    v1->release();
                                    callback->appendLog("NootRX_fix: [XML] aty_properties: DalDisableVActiveDramChange=1\n");
                                }
                                if (callback->rdFlags.noMpo) {
                                    /*
                                     * Decision record (v1.0.4 rejected by v1.0.5):
                                     * rd-nompo keeps its compatibility name, but the
                                     * tested value must follow the 1.0.3 hardware
                                     * baseline rather than an inferred Linux policy.
                                     *
                                     * AMD Display Core defines PipeSplitPolicy=1 as
                                     * MPC_SPLIT_AVOID.  Its DCN 2.x validation code
                                     * treats ForceSingleDispPipeSplit=true as a forced
                                     * split.  Version 1.0.4 therefore tried 0 plus the
                                     * avoid policy.  On this exact 0x73BF/C0 board that
                                     * did not clear either mpc2_assert_idle_mpcc warning
                                     * and produced an IOAccelDisplayPipe timeout at
                                     * uptime 40.529987.  Since 1.0.3 with value 1 remains
                                     * the best observed hardware baseline, restore it and
                                     * remove DalPipeSplitPolicy so v1.0.5 changes only the
                                     * missing GPUTaskSingleChannel invariant versus 1.0.3.
                                     */
                                    auto *v1 = OSNumber::withNumber(static_cast<UInt32>(1), 32);
                                    atyProps->setObject("DalForceSingleDispPipeSplit", v1);
                                    v1->release();
                                    callback->appendLog("NootRX_fix: [XML] aty_properties: DalForceSingleDispPipeSplit=1 (restored 1.0.3 hardware baseline)\n");
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
                                    callback->appendLog("NootRX_fix: [XML] aty_properties: Stutter clocks disabled (safe baseline)\n");
                                }
                                if (callback->rdFlags.floorDpm) {
                                    auto *v3 = OSNumber::withNumber(static_cast<UInt32>(3), 32);
                                    atyProps->setObject("DalForceMinDpmLevel", v3);
                                    v3->release();
                                    callback->appendLog("NootRX_fix: [XML] aty_properties: DalForceMinDpmLevel=3 (DPM High floor)\n");
                                }
                            }
                            if (atyConfig && (callback->rdFlags.noMpo || callback->rdFlags.noStutter)) {
                                atyConfig->setObject("CFG_USE_STUTTER", kOSBooleanFalse);
                                atyConfig->setObject("CFG_USE_FBC", kOSBooleanFalse);
                                atyConfig->setObject("CFG_USE_CPT", kOSBooleanFalse);
                                callback->appendLog("NootRX_fix: [XML] aty_config: CFG_USE_STUTTER=false, CFG_USE_FBC=false, CFG_USE_CPT=false\n");
                            }
                        }
                    }
                    array->setObject(driverIndex, drivers->getObject(injectedDriverIndex));
                    driverIndex += 1;
                    driverCount += 1;
                }

                dataUnserialized->release();
                break;
            }
        }
    }

    return FunctionCast(wrapAddDrivers, callback->orgAddDrivers)(that, array, doNubMatching);
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
