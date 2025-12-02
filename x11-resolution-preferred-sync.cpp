#include <iostream>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <optional>

#include <sys/wait.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#ifndef DISABLE_SPICE_VDAGENTD_RESTART
    static const char* spice_vdagentd_restart_define = "spice-vdagentd restart is enabled";
    static void restart_spice_vdagentd();
#else
    static const char* spice_vdagentd_restart_define = "spice-vdagentd restart is disabled";
#endif

struct Profile {
    unsigned int width;
    unsigned int height;
};

#ifndef DISABLE_XRANDR_AUTO_COMMAND
    #ifndef XRANDR_OUTPUT
        #define XRANDR_OUTPUT "Virtual-1"
    #endif
    static const char* resolution_setter = "resolution setter is xrandr auto command (" XRANDR_OUTPUT ")";
    static void systemXRandrAutoCommand();
#else
    static const char* resolution_setter = "resolution setter is XRRSetCrtcConfig";
    static bool setResolution(Display* display, Window root, Profile& profile);
#endif

static std::optional<Profile> getResolution(Display* display, Window root);
static std::optional<Profile> getPreferredResolution(Display* display, Window root);

int main() {
    std::cout << "[INFO] X11 Resolution Preferred Sync" << std::endl;
    std::cout << "[INFO] " << spice_vdagentd_restart_define << std::endl;
    std::cout << "[INFO] " << resolution_setter << std::endl;

    Display *display = XOpenDisplay(nullptr);
    if (!display) {
        std::cerr << "[ERROR] Failed to open X display." << std::endl;
        return 1;
    }

    int eventBase, errorBase;
    if (!XRRQueryExtension(display, &eventBase, &errorBase)) {
        std::cerr << "[ERROR] XRandR extension not available." << std::endl;
        XCloseDisplay(display);
        return 1;
    }

    Window root = DefaultRootWindow(display);

    XRRSelectInput(display, root, RROutputChangeNotifyMask);

    constexpr auto CHANGE_DELAY = std::chrono::milliseconds(500);

    auto lastChange = std::chrono::steady_clock::now() - CHANGE_DELAY;

    auto enforcePreferredResolution = [&]() {
        if (std::chrono::steady_clock::now() - lastChange < CHANGE_DELAY) {
            std::cout << "[INFO] Recent change detected, skipping enforcement." << std::endl;
            return;
        }

        auto preferredRes = getPreferredResolution(display, root);
        auto currentRes = getResolution(display, root);

        if (!preferredRes) {
            std::cerr << "[ERROR] Could not retrieve preferred resolution." << std::endl;
            return;
        }

        if (!currentRes) {
            std::cerr << "[ERROR] Could not retrieve current resolution." << std::endl;
            return;
        }

        if (currentRes->width == preferredRes->width && currentRes->height == preferredRes->height) {
            std::cout << "[INFO] Current resolution matches preferred. No action needed." << std::endl;
            return;
        }

        std::cout << "[INFO] Changing resolution from "
                  << currentRes->width << "x" << currentRes->height
                  << " to " << preferredRes->width << "x" << preferredRes->height << std::endl;

        #ifndef DISABLE_XRANDR_AUTO_COMMAND
        systemXRandrAutoCommand();
        #else
        if (setResolution(display, root, *preferredRes)) {
            std::cout << "[INFO] Resolution changed successfully." << std::endl;
        } else {
            std::cerr << "[ERROR] Failed to set preferred resolution." << std::endl;
            return;
        }
        #endif

        #ifndef DISABLE_SPICE_VDAGENTD_RESTART
        restart_spice_vdagentd();
        #endif

        lastChange = std::chrono::steady_clock::now();
    };

    enforcePreferredResolution();

    std::cout << "[INFO] Listening for resolution change events..." << std::endl;

    while (true) {
        XEvent event;
        XNextEvent(display, &event);

        if (event.type == eventBase + RRNotify) {
            XRRNotifyEvent* rr = reinterpret_cast<XRRNotifyEvent*>(&event);

            if (rr->subtype == RRNotify_OutputChange) {
                std::cout << "[INFO] Detected output change event." << std::endl;
                enforcePreferredResolution();
            }
        }
    }

    return 0;
}

static std::optional<Profile> getResolution(Display* display, Window root) {
    XRRScreenResources* res = XRRGetScreenResourcesCurrent(display, root);
    if (!res) return std::nullopt;

    XRROutputInfo* out = nullptr;
    XRRCrtcInfo* crtc = nullptr;

    Profile profile{};
    bool found = false;

    for (int i = 0; i < res->noutput; ++i) {
        out = XRRGetOutputInfo(display, res, res->outputs[i]);
        if (!out) continue;

        if (out->crtc) {
            crtc = XRRGetCrtcInfo(display, res, out->crtc);
            if (crtc) {
                if (found) {
                    std::cerr << "[ERROR] Multiple active displays detected. Only a single display is supported." << std::endl;
                    XRRFreeCrtcInfo(crtc);
                    XRRFreeOutputInfo(out);
                    XRRFreeScreenResources(res);
                    return std::nullopt;
                }

                profile.width = crtc->width;
                profile.height = crtc->height;
                found = true;
                XRRFreeCrtcInfo(crtc);
            }
        }

        XRRFreeOutputInfo(out);
    }
    XRRFreeScreenResources(res);

    return found ? std::optional<Profile>{profile} : std::nullopt;
}

static std::optional<Profile> getPreferredResolution(Display* display, Window root) {
    XRRScreenResources* res = XRRGetScreenResourcesCurrent(display, root);
    if (!res) return std::nullopt;

    std::optional<Profile> result;

    for (int i = 0; i < res->noutput; ++i) {
        XRROutputInfo* out = XRRGetOutputInfo(display, res, res->outputs[i]);
        if (!out) continue;

        if (out->npreferred > 0) {
            RRMode preferMode = out->modes[0];
            for (int j = 0; j < res->nmode; ++j) {
                if (res->modes[j].id == preferMode) {
                    Profile p;
                    p.width  = res->modes[j].width;
                    p.height = res->modes[j].height;
                    result = p;
                    break;
                }
            }
        }

        XRRFreeOutputInfo(out);

        if (result) break;
    }

    XRRFreeScreenResources(res);

    return result;
}

#ifdef DISABLE_XRANDR_AUTO_COMMAND
static bool setResolution(Display* display, Window root, Profile& profile) {
    XRRScreenResources* res = XRRGetScreenResourcesCurrent(display, root);
    if (!res) {
        std::cerr << "[ERROR] XRRGetScreenResourcesCurrent failed (setResolution)" << std::endl;
        return false;
    }

    bool success = false;

    for (int i = 0; i < res->noutput; ++i) {
        XRROutputInfo* out = XRRGetOutputInfo(display, res, res->outputs[i]);
        if (!out) continue;

        if (!out->crtc) {
            XRRFreeOutputInfo(out);
            continue;
        }

        XRRCrtcInfo* crtc = XRRGetCrtcInfo(display, res, out->crtc);
        if (!crtc) {
            XRRFreeOutputInfo(out);
            continue;
        }

        RRMode modeId = None;
        for (int m = 0; m < res->nmode; ++m) {
            XRRModeInfo& mi = res->modes[m];
            if (mi.width == (unsigned int)profile.width &&
                mi.height == (unsigned int)profile.height) {
                modeId = mi.id;
                break;
            }
        }

        if (modeId == None) {
            std::cerr << "[WARN] No mode found for "
                      << profile.width << "x" << profile.height << std::endl;
            XRRFreeCrtcInfo(crtc);
            XRRFreeOutputInfo(out);
            break;
        }

        bool supported = false;
        for (int m = 0; m < out->nmode; ++m) {
            if (out->modes[m] == modeId) {
                supported = true;
                break;
            }
        }

        if (!supported) {
            std::cerr << "[WARN] Output does not support mode "
                      << profile.width << "x" << profile.height << std::endl;
            XRRFreeCrtcInfo(crtc);
            XRRFreeOutputInfo(out);
            continue;
        }

        Status st = XRRSetCrtcConfig(
            display,
            res,
            out->crtc,
            CurrentTime,
            crtc->x,
            crtc->y,
            modeId,
            crtc->rotation,
            &res->outputs[i],
            1
        );

        if (st == Success) success = true;
        else std::cerr << "[ERROR] XRRSetCrtcConfig failed (status=" << st << ")" << std::endl;

        XRRFreeCrtcInfo(crtc);
        XRRFreeOutputInfo(out);
        break;
    }

    XRRFreeScreenResources(res);
    XFlush(display);

    return success;
}
#endif

#ifndef DISABLE_SPICE_VDAGENTD_RESTART
static void restart_spice_vdagentd() {
    std::this_thread::sleep_for(std::chrono::milliseconds{250});

    std::cout << "[INFO] Restarting spice-vdagentd.service" << std::endl;

    std::thread([] {
        int ret = std::system("systemctl restart spice-vdagentd.service");

        if (ret == -1) std::cerr << "[ERROR] Failed to execute systemctl" << std::endl;

        if (WIFEXITED(ret)) {
            int exit_code = WEXITSTATUS(ret);
            if (exit_code == 0) std::cout << "[INFO] spice-vdagentd.service restarted successfully" << std::endl;
            else std::cerr << "[ERROR] systemctl exit code: " << exit_code << std::endl;
        } else {
            std::cerr << "[ERROR] systemctl terminated abnormally" << std::endl;
        }
    }).detach();
}
#endif

#ifndef DISABLE_XRANDR_AUTO_COMMAND
static void systemXRandrAutoCommand() {
    std::this_thread::sleep_for(std::chrono::milliseconds{250});

    std::cout << "[INFO] Executing xrandr auto" << std::endl;

    std::thread([] {
        int ret = std::system("xrandr --output " XRANDR_OUTPUT " --auto");

        if (ret == -1) std::cerr << "[ERROR] Failed to execute xrandr auto" << std::endl;

        if (WIFEXITED(ret)) {
            int exit_code = WEXITSTATUS(ret);
            if (exit_code == 0) std::cout << "[INFO] xrandr auto executed successfully" << std::endl;
            else std::cerr << "[ERROR] xrandr exit code: " << exit_code << std::endl;
        } else {
            std::cerr << "[ERROR] xrandr terminated abnormally" << std::endl;
        }
    }).detach();
}
#endif
