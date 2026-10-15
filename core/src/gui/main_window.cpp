#include <gui/main_window.h>
#include <gui/gui.h>
#include "imgui.h"
#include <stdio.h>
#include <thread>
#include <complex>
#include <gui/widgets/waterfall.h>
#include <gui/widgets/frequency_select.h>
#include <signal_path/iq_frontend.h>
#include <gui/icons.h>
#include <gui/widgets/bandplan.h>
#include <gui/style.h>
#include <config.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/menus/source.h>
#include <gui/menus/display.h>
#include <gui/menus/bandplan.h>
#include <gui/menus/sink.h>
#include <gui/menus/vfo_color.h>
#include <gui/menus/module_manager.h>
#include <gui/menus/theme.h>
#include <gui/dialogs/credits.h>
#include <filesystem>
#include <signal_path/source.h>
#include <gui/dialogs/loading_screen.h>
#include <gui/colormaps.h>
#include <gui/layout.h>
#include <gui/widgets/snr_meter.h>
#include <gui/tuner.h>

void MainWindow::init() {
    LoadingScreen::show("Initializing UI");
    gui::waterfall.init();
    gui::waterfall.setRawFFTSize(fftSize);

    credits::init();

    core::configManager.acquire();
    json menuElements = core::configManager.conf["menuElements"];
    std::string modulesDir = core::configManager.conf["modulesDirectory"];
    std::string resourcesDir = core::configManager.conf["resourcesDirectory"];
    core::configManager.release();

    // Assert that directories are absolute
    modulesDir = std::filesystem::absolute(modulesDir).string();
    resourcesDir = std::filesystem::absolute(resourcesDir).string();

    if (!std::filesystem::is_directory(modulesDir)) {
        if (std::filesystem::is_directory("../Plugins")) {
            modulesDir = std::filesystem::absolute("../Plugins").string();
            flog::warn("Configured modules directory not found; falling back to bundle plugins: {0}", modulesDir);
            core::configManager.acquire();
            core::configManager.conf["modulesDirectory"] = "../Plugins";
            core::configManager.release(true);
        }
        else if (std::filesystem::is_directory("./modules")) {
            modulesDir = std::filesystem::absolute("./modules").string();
            flog::warn("Configured modules directory not found; falling back to ./modules: {0}", modulesDir);
            core::configManager.acquire();
            core::configManager.conf["modulesDirectory"] = modulesDir;
            core::configManager.release(true);
        }
    }

    if (!std::filesystem::is_directory(resourcesDir)) {
        if (std::filesystem::is_directory("../Resources")) {
            resourcesDir = std::filesystem::absolute("../Resources").string();
            flog::warn("Configured resources directory not found; falling back to bundle resources: {0}", resourcesDir);
            core::configManager.acquire();
            core::configManager.conf["resourcesDirectory"] = "../Resources";
            core::configManager.release(true);
        }
        else if (std::filesystem::is_directory("./root/res")) {
            resourcesDir = std::filesystem::absolute("./root/res").string();
            flog::warn("Configured resources directory not found; falling back to ./root/res: {0}", resourcesDir);
            core::configManager.acquire();
            core::configManager.conf["resourcesDirectory"] = resourcesDir;
            core::configManager.release(true);
        }
        else if (std::filesystem::is_directory("../root/res")) {
            resourcesDir = std::filesystem::absolute("../root/res").string();
            flog::warn("Configured resources directory not found; falling back to ../root/res: {0}", resourcesDir);
            core::configManager.acquire();
            core::configManager.conf["resourcesDirectory"] = resourcesDir;
            core::configManager.release(true);
        }
    }

    // Load menu elements
    gui::menu.order.clear();
    for (auto& elem : menuElements) {
        if (!elem.contains("name")) {
            flog::error("Menu element is missing name key");
            continue;
        }
        if (!elem["name"].is_string()) {
            flog::error("Menu element 'name' isn't a string");
            continue;
        }
        if (!elem.contains("open")) {
            flog::error("Menu element is missing open key");
            continue;
        }
        if (!elem["open"].is_boolean()) {
            flog::error("Menu element 'open' isn't a boolean");
            continue;
        }
        Menu::MenuOption_t opt;
        opt.name = elem["name"];
        opt.open = elem["open"];
        opt.detached = elem.contains("detached") && elem["detached"].is_boolean() && elem["detached"].get<bool>();
        gui::menu.order.push_back(opt);
    }

    gui::menu.registerEntry("Source", sourcemenu::draw, NULL);
    gui::menu.registerEntry("Sinks", sinkmenu::draw, NULL);
    gui::menu.registerEntry("Band Plan", bandplanmenu::draw, NULL);
    gui::menu.registerEntry("Display", displaymenu::draw, NULL);
    gui::menu.registerEntry("Theme", thememenu::draw, NULL);
    gui::menu.registerEntry("VFO Color", vfo_color_menu::draw, NULL);

    gui::dock.registerEntry("Module Manager", module_manager_menu::draw, NULL);

    gui::freqSelect.init();

    // Set default values for waterfall in case no source init's it
    gui::waterfall.setBandwidth(8000000);
    gui::waterfall.setViewBandwidth(8000000);

    fft_in = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fftSize);
    fft_out = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * fftSize);
    fftwPlan = fftwf_plan_dft_1d(fftSize, fft_in, fft_out, FFTW_FORWARD, FFTW_ESTIMATE);

    sigpath::iqFrontEnd.init(&dummyStream, 8000000, true, 1, false, 1024, 20.0, IQFrontEnd::FFTWindow::NUTTALL,
                             acquireFFTBuffer, releaseFFTBuffer, this);
    sigpath::iqFrontEnd.start();

    vfoCreatedHandler.handler = vfoAddedHandler;
    vfoCreatedHandler.ctx = this;
    sigpath::vfoManager.onVfoCreated.bindHandler(&vfoCreatedHandler);

    flog::info("Loading modules");

    // Load modules from /module directory
    if (std::filesystem::is_directory(modulesDir)) {
        for (const auto& file : std::filesystem::directory_iterator(modulesDir)) {
            std::string path = file.path().generic_string();
            if (file.path().extension().generic_string() != SDRPP_MOD_EXTENTSION) {
                continue;
            }
            if (!file.is_regular_file()) { continue; }
            flog::info("Loading {0}", path);
            LoadingScreen::show("Loading " + file.path().filename().string());
            core::moduleManager.loadModule(path);
        }
    } else {
        flog::warn("Module directory {0} does not exist, not loading modules from directory", modulesDir);
    }

    // Read module config
    core::configManager.acquire();
    std::vector<std::string> modules = core::configManager.conf["modules"];
    auto modList = core::configManager.conf["moduleInstances"].items();
    core::configManager.release();

    // Load additional modules specified through config
    for (auto const& path : modules) {
#ifndef __ANDROID__
        std::string apath = std::filesystem::absolute(path).string();
        flog::info("Loading {0}", apath);
        LoadingScreen::show("Loading " + std::filesystem::path(path).filename().string());
        core::moduleManager.loadModule(apath);
#else
        core::moduleManager.loadModule(path);
#endif
    }

    // Create module instances
    for (auto const& [name, _module] : modList) {
        std::string mod = _module["module"];
        bool enabled = _module["enabled"];
        flog::info("Initializing {0} ({1})", name, mod);
        LoadingScreen::show("Initializing " + name + " (" + mod + ")");
        core::moduleManager.createInstance(name, mod);
        if (!enabled) { core::moduleManager.disableInstance(name); }
    }

    // Load color maps
    LoadingScreen::show("Loading color maps");
    flog::info("Loading color maps");
    if (std::filesystem::is_directory(resourcesDir + "/colormaps")) {
        for (const auto& file : std::filesystem::directory_iterator(resourcesDir + "/colormaps")) {
            std::string path = file.path().generic_string();
            LoadingScreen::show("Loading " + file.path().filename().string());
            flog::info("Loading {0}", path);
            if (file.path().extension().generic_string() != ".json") {
                continue;
            }
            if (!file.is_regular_file()) { continue; }
            colormaps::loadMap(path);
        }
    } else {
        flog::warn("Color map directory {0} does not exist, not loading modules from directory", modulesDir);
    }

    gui::waterfall.updatePalletteFromArray(colormaps::maps["Turbo"].map, colormaps::maps["Turbo"].entryCount);

    sourcemenu::init();
    sinkmenu::init();
    bandplanmenu::init();
    displaymenu::init();
    vfo_color_menu::init();
    module_manager_menu::init();

    // TODO for 0.2.5
    // Fix gain not updated on startup, soapysdr

    // Update UI settings
    LoadingScreen::show("Loading configuration");
    core::configManager.acquire();
    fftMin = core::configManager.conf["min"];
    fftMax = core::configManager.conf["max"];
    fftContrast = core::configManager.conf["contrast"];
    gui::waterfall.setFFTMin(fftMin);
    gui::waterfall.setWaterfallMin(fftMin);
    gui::waterfall.setFFTMax(fftMax);
    gui::waterfall.setWaterfallMax(fftMax);
    gui::waterfall.setContrast((int) std::floor(fftContrast));
    double frequency = core::configManager.conf["frequency"];

    showMenu = core::configManager.conf["showMenu"];
    startedWithMenuClosed = !showMenu;

    gui::freqSelect.setFrequency(frequency);
    gui::freqSelect.frequencyChanged = false;
    sigpath::sourceManager.tune(frequency);
    gui::waterfall.setCenterFrequency(frequency);
    bw = 1.0;
    fftContrast = 80;
    gui::waterfall.vfoFreqChanged = false;
    gui::waterfall.centerFreqMoved = false;
    gui::waterfall.selectFirstVFO();

    menuWidth = core::configManager.conf["menuWidth"];
    newWidth = menuWidth;

    fftHeight = core::configManager.conf["fftHeight"];
    gui::waterfall.setFFTHeight(fftHeight);

    tuningMode = core::configManager.conf["centerTuning"] ? tuner::TUNER_MODE_CENTER : tuner::TUNER_MODE_NORMAL;
    gui::waterfall.VFOMoveSingleClick = (tuningMode == tuner::TUNER_MODE_CENTER);

    core::configManager.release();

    // Correct the offset of all VFOs so that they fit on the screen
    float finalBwHalf = gui::waterfall.getBandwidth() / 2.0;
    for (auto& [_name, _vfo] : gui::waterfall.vfos) {
        if (_vfo->lowerOffset < -finalBwHalf) {
            sigpath::vfoManager.setCenterOffset(_name, (_vfo->bandwidth / 2) - finalBwHalf);
            continue;
        }
        if (_vfo->upperOffset > finalBwHalf) {
            sigpath::vfoManager.setCenterOffset(_name, finalBwHalf - (_vfo->bandwidth / 2));
            continue;
        }
    }

    autostart = core::args["autostart"].b();
    initComplete = true;

    core::moduleManager.doPostInitAll();
}

float* MainWindow::acquireFFTBuffer(void* ctx) {
    return gui::waterfall.getFFTBuffer();
}

void MainWindow::releaseFFTBuffer(void* ctx) {
    gui::waterfall.pushFFT();
}

void MainWindow::vfoAddedHandler(VFOManager::VFO* vfo, void* ctx) {
    MainWindow* _this = (MainWindow*)ctx;
    std::string name = vfo->getName();
    core::configManager.acquire();
    if (!core::configManager.conf["vfoOffsets"].contains(name)) {
        core::configManager.release();
        return;
    }
    double offset = core::configManager.conf["vfoOffsets"][name];
    core::configManager.release();

    double viewBW = gui::waterfall.getViewBandwidth();
    double viewOffset = gui::waterfall.getViewOffset();

    double viewLower = viewOffset - (viewBW / 2.0);
    double viewUpper = viewOffset + (viewBW / 2.0);

    double newOffset = std::clamp<double>(offset, viewLower, viewUpper);

    sigpath::vfoManager.setCenterOffset(name, _this->initComplete ? newOffset : offset);
}

void MainWindow::draw() {
    ImGui::Begin("Main", NULL, WINDOW_FLAGS);
    ImVec4 textCol = ImGui::GetStyleColorVec4(ImGuiCol_Text);

    ImGui::WaterfallVFO* vfo = NULL;
    if (gui::waterfall.selectedVFO != "") {
        vfo = gui::waterfall.vfos[gui::waterfall.selectedVFO];
    }

    // Handle VFO movement
    if (vfo != NULL) {
        if (vfo->centerOffsetChanged) {
            if (tuningMode == tuner::TUNER_MODE_CENTER) {
                tuner::tune(tuner::TUNER_MODE_CENTER, gui::waterfall.selectedVFO,
                            gui::waterfall.getCenterFrequency() + vfo->generalOffset);
            }
            gui::freqSelect.setFrequency(gui::waterfall.getCenterFrequency() + vfo->generalOffset);
            gui::freqSelect.frequencyChanged = false;
            core::configManager.acquire();
            core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = vfo->generalOffset;
            core::configManager.release(true);
        }
    }

    sigpath::vfoManager.updateFromWaterfall(&gui::waterfall);

    // Handle selection of another VFO
    if (gui::waterfall.selectedVFOChanged) {
        gui::freqSelect.setFrequency((vfo != NULL)
                                         ? (vfo->generalOffset + gui::waterfall.getCenterFrequency())
                                         : gui::waterfall.getCenterFrequency());
        gui::waterfall.selectedVFOChanged = false;
        gui::freqSelect.frequencyChanged = false;
    }

    // Handle change in selected frequency
    if (gui::freqSelect.frequencyChanged) {
        gui::freqSelect.frequencyChanged = false;
        tuner::tune(tuningMode, gui::waterfall.selectedVFO, gui::freqSelect.frequency);
        if (vfo != NULL) {
            vfo->centerOffsetChanged = false;
            vfo->lowerOffsetChanged = false;
            vfo->upperOffsetChanged = false;
        }
        core::configManager.acquire();
        core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
        if (vfo != NULL) {
            core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = vfo->generalOffset;
        }
        core::configManager.release(true);
    }

    // Handle dragging the frequency scale
    if (gui::waterfall.centerFreqMoved) {
        gui::waterfall.centerFreqMoved = false;
        sigpath::sourceManager.tune(gui::waterfall.getCenterFrequency());
        if (vfo != NULL) {
            gui::freqSelect.setFrequency(gui::waterfall.getCenterFrequency() + vfo->generalOffset);
        } else {
            gui::freqSelect.setFrequency(gui::waterfall.getCenterFrequency());
        }
        core::configManager.acquire();
        core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
        core::configManager.release(true);
    }

    int _fftHeight = gui::waterfall.getFFTHeight();
    if (fftHeight != _fftHeight) {
        fftHeight = _fftHeight;
        core::configManager.acquire();
        core::configManager.conf["fftHeight"] = fftHeight;
        core::configManager.release(true);
    }

    // To Bar
    // ImGui::BeginChild("TopBarChild", ImVec2(0, 49.0f * style::uiScale), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImVec2 btnSize(30 * style::uiScale, 30 * style::uiScale);

    if (ImGui::ImageButton("sdrpp_menu_btn", icons::MENU, btnSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0),
                           textCol) || ImGui::IsKeyPressed(ImGuiKey_Menu, false)) {
        showMenu = !showMenu;
        core::configManager.acquire();
        core::configManager.conf["showMenu"] = showMenu;
        core::configManager.release(true);
    }

    ImGui::SameLine();

    bool tmpPlaySate = playing;
    if (playButtonLocked && !tmpPlaySate) { style::beginDisabled(); }
    if (playing) {
        if (ImGui::ImageButton("sdrpp_stop_btn", icons::STOP, btnSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0),
                               textCol) || ImGui::IsKeyPressed(ImGuiKey_End, false)) {
            setPlayState(false);
        }
    } else {
        // TODO: Might need to check if there even is a device
        if (ImGui::ImageButton("sdrpp_play_btn", icons::PLAY, btnSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0),
                               textCol) || ImGui::IsKeyPressed(ImGuiKey_End, false)) {
            setPlayState(true);
        }
    }
    if (playButtonLocked && !tmpPlaySate) { style::endDisabled(); }

    // Handle auto-start
    if (autostart) {
        autostart = false;
        setPlayState(true);
    }

    ImGui::SameLine();
    float origY = ImGui::GetCursorPosY();

    sigpath::sinkManager.showVolumeSlider(gui::waterfall.selectedVFO, "##_sdrpp_main_volume_", 248 * style::uiScale,
                                          btnSize.x, 5, true);

    ImGui::SameLine();

    ImGui::SetCursorPosY(origY);
    gui::freqSelect.draw();

    ImGui::SameLine();

    ImGui::SetCursorPosY(origY);
    if (tuningMode == tuner::TUNER_MODE_CENTER) {
        if (ImGui::ImageButton("sdrpp_ena_st_btn", icons::CENTER_TUNING, btnSize, ImVec2(0, 0), ImVec2(1, 1),
                               ImVec4(0, 0, 0, 0), textCol)) {
            tuningMode = tuner::TUNER_MODE_NORMAL;
            gui::waterfall.VFOMoveSingleClick = false;
            core::configManager.acquire();
            core::configManager.conf["centerTuning"] = false;
            core::configManager.release(true);
        }
    } else {
        // TODO: Might need to check if there even is a device
        if (ImGui::ImageButton("sdrpp_dis_st_btn", icons::NORMAL_TUNING, btnSize, ImVec2(0, 0), ImVec2(1, 1),
                               ImVec4(0, 0, 0, 0), textCol)) {
            tuningMode = tuner::TUNER_MODE_CENTER;
            gui::waterfall.VFOMoveSingleClick = true;
            tuner::tune(tuner::TUNER_MODE_CENTER, gui::waterfall.selectedVFO, gui::freqSelect.frequency);
            core::configManager.acquire();
            core::configManager.conf["centerTuning"] = true;
            core::configManager.release(true);
        }
    }

    ImGui::SameLine();

    // Selected VFO
    ImGui::SetCursorPosY(origY + (3.0f * style::uiScale));
    ImGui::SetNextItemWidth(150.0f * style::uiScale);
    if (ImGui::BeginCombo("##sdrpp_selected_vfo", gui::waterfall.selectedVFO.empty() ? "No VFO" : gui::waterfall.selectedVFO.c_str())) {
        for (auto const& [vfoName, vfoItem] : gui::waterfall.vfos) {
            bool isSelected = (vfoName == gui::waterfall.selectedVFO);
            if (ImGui::Selectable(vfoName.c_str(), isSelected)) {
                gui::waterfall.selectedVFO = vfoName;
                gui::waterfall.selectedVFOChanged = true;
            }
            if (isSelected) { ImGui::SetItemDefaultFocus(); }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Selected VFO\nPage Up / Page Down cycles through the VFOs"); }

    ImGui::SameLine();

    int snrOffset = 87.0f * style::uiScale;
    int snrWidth = std::clamp<int>(ImGui::GetWindowSize().x - ImGui::GetCursorPosX() - snrOffset,
                                   100.0f * style::uiScale, 300.0f * style::uiScale);
    int snrPos = std::max<int>(ImGui::GetWindowSize().x - (snrWidth + snrOffset), ImGui::GetCursorPosX());

    ImGui::SetCursorPosX(snrPos);
    ImGui::SetCursorPosY(origY + (5.0f * style::uiScale));
    ImGui::SetNextItemWidth(snrWidth);
    ImGui::SNRMeter((vfo != NULL) ? gui::waterfall.selectedVFOSNR : 0);

    // Note: this is what makes the vertical size correct, needs to be fixed
    ImGui::SameLine();

    // ImGui::EndChild();

    // Logo button
    ImGui::SetCursorPosX(ImGui::GetWindowSize().x - (48 * style::uiScale));
    ImGui::SetCursorPosY(10.0f * style::uiScale);
    if (ImGui::ImageButton("sdrpp_logo_st_btn", icons::LOGO, ImVec2(32 * style::uiScale, 32 * style::uiScale),
                           ImVec2(0, 0), ImVec2(1, 1))) {
        showCredits = true;
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        showCredits = false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        showCredits = false;
    }

    // Reset waterfall lock
    lockWaterfallControls = showCredits;

    // Handle menu resize
    ImVec2 winSize = ImGui::GetWindowSize();
    ImVec2 mousePos = ImGui::GetMousePos();
    if (!lockWaterfallControls && showMenu) {
        float curY = ImGui::GetCursorPosY();
        bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (grabbingMenu) {
            newWidth = mousePos.x;
            newWidth = std::clamp<float>(newWidth, 250, winSize.x - 250);
            ImGui::GetForegroundDrawList()->AddLine(ImVec2(newWidth, curY), ImVec2(newWidth, winSize.y - 10),
                                                    ImGui::GetColorU32(ImGuiCol_SeparatorActive));
        }
        if (mousePos.x >= newWidth - (2.0f * style::uiScale) && mousePos.x <= newWidth + (2.0f * style::uiScale) &&
            mousePos.y > curY) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (click) {
                grabbingMenu = true;
            }
        } else {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
        }
        if (!down && grabbingMenu) {
            grabbingMenu = false;
            menuWidth = newWidth;
            core::configManager.acquire();
            core::configManager.conf["menuWidth"] = menuWidth;
            core::configManager.release(true);
        }
    }

    // Process menu keybinds
    displaymenu::checkKeybinds();

    // Create a separator between buttons and the dockspace
    ImGui::Separator();

    // Get the available space under the buttons
    ImVec2 availableSpaceForDocking = ImGui::GetContentRegionAvail();

    // Create a child region with a dockspace in the remaining space
    ImGui::BeginChild("DockingRegion", availableSpaceForDocking, false,
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);


    if (firstMenuRender) {
        // Rebuild the layout from the deserialized tree
        core::configManager.acquire();
        std::string layout = core::configManager.conf["layout"];
        core::configManager.release();

        layout::createDockLayoutFromJson(dockspace_id, availableSpaceForDocking, layout);

        /*
        TODO: Add this as a backup in case the layout file is missing

        ImGuiID dockspaceId = ImGui::GetID("DockSpace");
        dockspace_id = dockspaceId;
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, DOCKSPACE_FLAGS);
        ImGui::DockBuilderSetNodeSize(dockspaceId, availableSpaceForDocking);

        ImGuiID dockspace_left = ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.2f, nullptr, &dockspaceId);
        ImGui::DockBuilderDockWindow("Waterfall", dockspaceId);
        ImGui::DockBuilderFinish(dockspaceId);

        ImGuiID dockspace_left_down = ImGui::DockBuilderSplitNode(dockspace_left, ImGuiDir_Down, 0.2f, nullptr, &dockspace_left);

        ImGui::DockBuilderDockWindow("Menu", dockspace_left);
        ImGui::DockBuilderDockWindow("Debug", dockspace_left_down);
        ImGui::DockBuilderFinish(dockspace_left);
        */
    } else {
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), DOCKSPACE_FLAGS);

        // Save layout on F12 or when the dock settings change
        ImGuiIO& dockIo = ImGui::GetIO();
        bool autoSaveLayout = dockIo.WantSaveIniSettings;
        dockIo.WantSaveIniSettings = false;
        if (ImGui::IsKeyPressed(ImGuiKey_F12) || autoSaveLayout) {
            saveDockLayout = false;
            ImGuiContext& g = *GImGui;
            ImGuiID id = ImGui::GetID("DockSpace");
            ImGuiDockNode* node = ImGui::DockContextFindNodeByID(&g, id);

            core::configManager.acquire();
            std::string layout = core::configManager.conf["layout"];
            core::configManager.release();
            layout::printAllDockNodesAsJson(node, layout);
        }
    }

    ImGui::EndChild();

    ImGui::End(); // Main

    // Left Column
    ImGui::SetNextWindowSizeConstraints(ImVec2(300 * style::uiScale, 300 * style::uiScale), ImVec2(INFINITY, INFINITY));
    ImGui::Begin("Menu", NULL);
    ImGui::BeginChild("Menu Child");
    // Save menu state and enabled modules
    auto saveMenuConfig = []() {
        core::configManager.acquire();
        json arr = json::array();
        for (int i = 0; i < gui::menu.order.size(); i++) {
            arr[i]["name"] = gui::menu.order[i].name;
            arr[i]["open"] = gui::menu.order[i].open;
            arr[i]["detached"] = gui::menu.order[i].detached;
        }
        core::configManager.conf["menuElements"] = arr;

        // Update enabled and disabled modules
        for (auto [_name, inst] : core::moduleManager.instances) {
            if (!core::configManager.conf["moduleInstances"].contains(_name)) { continue; }
            core::configManager.conf["moduleInstances"][_name]["enabled"] = inst.instance->isEnabled();
        }

        core::configManager.release(true);
    };

    if (gui::menu.draw(firstMenuRender)) {
        saveMenuConfig();
    }
    if (startedWithMenuClosed) {
        startedWithMenuClosed = false;
    } else {
        firstMenuRender = false;
    }

    ImGui::EndChild();

    if (gui::menu.drawDetached()) {
        saveMenuConfig();
    }

    if (gui::dock.draw()) {
        core::configManager.acquire();
        std::string layout = core::configManager.conf["layout"];
        core::configManager.release();
        //layout::printAllDockNodesAsJson(ImGui::DockBuilderGetNode(dockspace_id), layout);
    }

    // Display the Debug Window

    if (debugWindow) {
        ImGui::Begin("Debug", &debugWindow);
        ImGui::Text("Frame time: %.3f ms/frame", ImGui::GetIO().DeltaTime * 1000.0f);
        ImGui::Text("Framerate: %.1f FPS", ImGui::GetIO().Framerate);
        ImGui::Text("Center Frequency: %.0f Hz", gui::waterfall.getCenterFrequency());
        ImGui::Text("Source name: %s", sourceName.c_str());
        ImGui::Checkbox("Show demo window", &demoWindow);
        ImGui::Text("ImGui version: %s", ImGui::GetVersion());

        // ImGui::Checkbox("Bypass buffering", &sigpath::iqFrontEnd.inputBuffer.bypass);

        // ImGui::Text("Buffering: %d", (sigpath::iqFrontEnd.inputBuffer.writeCur - sigpath::iqFrontEnd.inputBuffer.readCur + 32) % 32);

        if (ImGui::Button("Test Bug")) {
            flog::error("Will this make the software crash?");
        }

        if (ImGui::Button("Testing something")) {
            gui::menu.order[0].open = true;
            firstMenuRender = true;
        }

        ImGui::Checkbox("WF Single Click", &gui::waterfall.VFOMoveSingleClick);
        ImGui::Checkbox("Lock Menu Order", &gui::menu.locked);

        ImGui::Spacing();

        ImGui::End();
    }

    // Right Column
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    //ImGui::NextColumn();
    ImGui::PopStyleVar();

    ImGui::End(); // Menu

    ImGui::Begin("Waterfall", NULL);
    ImVec2 availableSpaceInWaterfall = ImGui::GetContentRegionAvail();
    // Fit the widest label
    float wfControlsWidth = std::max<float>(40.0f * style::uiScale, ImGui::CalcTextSize("Contrast").x + (28.0f * style::uiScale));
    ImGui::Columns(2, "WaterfallColumns", false);
    ImGui::SetColumnWidth(0, std::max<int>(availableSpaceInWaterfall.x - wfControlsWidth,
                                           100.0f * style::uiScale));
    ImGui::SetColumnWidth(1, wfControlsWidth);

    ImGui::BeginChild("FFT Child");

    gui::waterfall.draw();

    ImGui::EndChild();

    if (!lockWaterfallControls) {
        // Handle arrow keys
        if (vfo != NULL && (gui::waterfall.mouseInFFT || gui::waterfall.mouseInWaterfall)) {
            bool freqChanged = false;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && !gui::freqSelect.digitHovered) {
                double nfreq = gui::waterfall.getCenterFrequency() + vfo->generalOffset - vfo->snapInterval;
                nfreq = roundl(nfreq / vfo->snapInterval) * vfo->snapInterval;
                tuner::tune(tuningMode, gui::waterfall.selectedVFO, nfreq);
                freqChanged = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && !gui::freqSelect.digitHovered) {
                double nfreq = gui::waterfall.getCenterFrequency() + vfo->generalOffset + vfo->snapInterval;
                nfreq = roundl(nfreq / vfo->snapInterval) * vfo->snapInterval;
                tuner::tune(tuningMode, gui::waterfall.selectedVFO, nfreq);
                freqChanged = true;
            }
            if (freqChanged) {
                core::configManager.acquire();
                core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
                if (vfo != NULL) {
                    core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = vfo->generalOffset;
                }
                core::configManager.release(true);
            }
        }

        // Handle scrollwheel
        int wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0 && (gui::waterfall.mouseInFFT || gui::waterfall.mouseInWaterfall)) {
            double nfreq;
            if (vfo != NULL) {
                // Select factor depending on modifier keys
                double interval;
                if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) {
                    interval = vfo->snapInterval * 10.0;
                } else if (ImGui::IsKeyDown(ImGuiKey_LeftAlt)) {
                    interval = vfo->snapInterval * 0.1;
                } else {
                    interval = vfo->snapInterval;
                }

                nfreq = gui::waterfall.getCenterFrequency() + vfo->generalOffset + (interval * wheel);
                nfreq = roundl(nfreq / interval) * interval;
            } else {
                nfreq = gui::waterfall.getCenterFrequency() - (gui::waterfall.getViewBandwidth() * wheel / 20.0);
            }
            tuner::tune(tuningMode, gui::waterfall.selectedVFO, nfreq);
            gui::freqSelect.setFrequency(nfreq);
            core::configManager.acquire();
            core::configManager.conf["frequency"] = gui::waterfall.getCenterFrequency();
            if (vfo != NULL) {
                core::configManager.conf["vfoOffsets"][gui::waterfall.selectedVFO] = vfo->generalOffset;
            }
            core::configManager.release(true);
        }
    }

    ImGui::NextColumn();
    ImGui::BeginChild("WaterfallControls");

    // Waterfall control's size calculations
    float wfSliderHor = 20.0 * style::uiScale;
    float wfSliderVert = ImGui::GetWindowSize().y / 5.0 - 10.0 * style::uiScale;
    wfSliderVert = wfSliderVert < 100.0 * style::uiScale ? 100.0 * style::uiScale : wfSliderVert;
    ImVec2 wfSliderSize(wfSliderHor, wfSliderVert);

    // Zoom slider
    ImGui::SetCursorPosX(std::max<float>(0.0f, (ImGui::GetWindowSize().x / 2.0) - (ImGui::CalcTextSize("Zoom").x / 2.0)));
    ImGui::TextUnformatted("Zoom");
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - 10 * style::uiScale);

    if (ImGui::VSliderFloat("##_7_", wfSliderSize, &bw, 1.0, 0.0, "")) {
        double factor = (double)bw * (double)bw;

        // Map 0.0 -> 1.0 to 1000.0 -> bandwidth
        double wfBw = gui::waterfall.getBandwidth();
        double delta = wfBw - 1000.0;
        double finalBw = std::min<double>(1000.0 + (factor * delta), wfBw);

        gui::waterfall.setViewBandwidth(finalBw);
        if (vfo != NULL) {
            gui::waterfall.setViewOffset(vfo->centerOffset); // center vfo on screen
        }
    }

    ImGui::NewLine();

    // Contrast slider
    ImGui::SetCursorPosX(std::max<float>(0.0f, (ImGui::GetWindowSize().x / 2.0) - (ImGui::CalcTextSize("Contrast").x / 2.0)));
    ImGui::TextUnformatted("Contrast");
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - 10 * style::uiScale);
    if (ImGui::VSliderFloat("##_8_", wfSliderSize, &fftContrast, 0.0, 160.0, "")) {
        gui::waterfall.setContrast((int)std::floor(fftContrast));
    }

    ImGui::NewLine();
    // Calculate the offset and range dynamically based on fftMax and fftMin
    float offset = (fftMax + fftMin) / 2.0f;
    float range = fftMax - fftMin;

    // Offset slider
    ImGui::SetCursorPosX(std::max<float>(0.0f, (ImGui::GetWindowSize().x / 2.0) - (ImGui::CalcTextSize("Offset").x / 2.0)));
    ImGui::TextUnformatted("Offset");
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - 10 * style::uiScale);
    if (ImGui::VSliderFloat("##_9_", wfSliderSize, &offset, -160.0f, 0.0f, "")) {
        // Recalculate fftMax and fftMin based on the new offset and range
        fftMax = offset + range / 2.0f;
        fftMin = offset - range / 2.0f;

        // Clamp fftMax to ensure it does not exceed 0
        if (fftMax > 0.0f) {
            fftMax = 0.0f;
            fftMin = fftMax - range; // Adjust fftMin to maintain the range
        }

        // Clamp fftMin to ensure it does not exceed -160
        if (fftMin < -160.0f) {
            fftMin = -160.0f;
            fftMax = fftMin + range; // Adjust fftMax to maintain the range
        }

        flog::verbose("Offset Changed:\n\toffset: {0},\n\trange: {1},\n\tfftMax: {2},\n\tfftMin: {3}", offset, range,
                      fftMax, fftMin);

        core::configManager.acquire();
        core::configManager.conf["max"] = fftMax;
        core::configManager.conf["min"] = fftMin;
        core::configManager.release(true);
    }

    ImGui::NewLine();

    // Range slider
    ImGui::SetCursorPosX(std::max<float>(0.0f, (ImGui::GetWindowSize().x / 2.0) - (ImGui::CalcTextSize("Range").x / 2.0)));
    ImGui::TextUnformatted("Range");
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x / 2.0) - 10 * style::uiScale);
    ImGui::SetItemUsingMouseWheel();
    if (ImGui::VSliderFloat("##_10_", wfSliderSize, &range, 10.0f, 160.0f, "")) {
        // Recalculate fftMax and fftMin based on the new range and current offset
        fftMax = offset + range / 2.0f;
        fftMin = offset - range / 2.0f;

        // Clamp fftMax to ensure it does not exceed 0
        if (fftMax > 0.0f) {
            fftMax = 0.0f;
            fftMin = fftMax - range; // Adjust fftMin to maintain the range
        }
        // Clamp fftMin to ensure it does not exceed -160
        if (fftMin < -160.0f) {
            fftMin = -160.0f;
            fftMax = fftMin + range; // Adjust fftMax to maintain the range
        }

        flog::verbose("Range Changed:\n\toffset: {0},\n\trange: {1},\n\tfftMax: {2},\n\tfftMin: {3}", offset, range,
                      fftMax, fftMin);

        core::configManager.acquire();
        core::configManager.conf["max"] = fftMax;
        core::configManager.conf["min"] = fftMin;
        core::configManager.release(true);
    }

    ImGui::EndChild();

    gui::waterfall.setFFTMin(fftMin);
    gui::waterfall.setFFTMax(fftMax);
    gui::waterfall.setWaterfallMin(fftMin);
    gui::waterfall.setWaterfallMax(fftMax);

    ImGui::End(); // Waterfall

    if (showCredits) {
        credits::show();
    }

    if (demoWindow) {
        ImGui::ShowDemoWindow();
    }
}

void MainWindow::setPlayState(bool _playing) {
    if (_playing == playing) { return; }
    if (_playing) {
        sigpath::iqFrontEnd.flushInputBuffer();
        sigpath::sourceManager.start();
        sigpath::sourceManager.tune(gui::waterfall.getCenterFrequency());
        playing = true;
        onPlayStateChange.emit(true);
    } else {
        playing = false;
        onPlayStateChange.emit(false);
        sigpath::sourceManager.stop();
        sigpath::iqFrontEnd.flushInputBuffer();
    }
}

void MainWindow::setViewBandwidthSlider(float bandwidth) {
    bw = bandwidth;
}

bool MainWindow::sdrIsRunning() {
    return playing;
}

bool MainWindow::isPlaying() {
    return playing;
}

void MainWindow::setFirstMenuRender() {
    firstMenuRender = true;
}
