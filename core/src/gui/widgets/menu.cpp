#include <gui/widgets/menu.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <gui/style.h>

Menu::Menu() {
}

void Menu::registerEntry(std::string name, void (*drawHandler)(void* ctx), void* ctx, ModuleManager::Instance* inst) {
    MenuItem_t item;
    item.drawHandler = drawHandler;
    item.ctx = ctx;
    item.inst = inst;
    items[name] = item;
    if (!isInOrderList(name)) {
        MenuOption_t opt;
        opt.name = name;
        opt.open = true;
        order.push_back(opt);
    }
}

void Menu::removeEntry(std::string name) {
    items.erase(name);
}

bool Menu::draw(bool updateStates) {
    bool changed = false;
    float menuWidth = ImGui::GetContentRegionAvail().x;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImVec2 checkboxOffset = ImVec2(menuWidth - ImGui::GetTextLineHeight() - (6.0f * style::uiScale), - ImGui::GetTextLineHeight() - (10.0f * style::uiScale));

    int displayedCount = 0;
    int rawId = 0;

    ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);

    for (MenuOption_t& opt : order) {
        rawId++;
        if (items.find(opt.name) == items.end() || opt.detached) {
            continue;
        }
        if (opt.name == draggedMenuName) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", draggedMenuName.c_str());
            ImGui::EndTooltip();
            continue;
        }

        // Draw dragged menu item
        if (displayedCount == insertBefore && !draggedMenuName.empty()) {
            if (updateStates) { ImGui::SetNextItemOpen(false); }
            ImVec2 posMin = ImGui::GetCursorScreenPos();
            ImVec2 posMax = ImVec2(posMin.x + menuWidth, posMin.y + ImGui::GetFrameHeight());
            style::beginDisabled();
            ImRect orignalRect = window->WorkRect;
            ImGui::CollapsingHeader((draggedMenuName + "##sdrpp_main_menu_dragging").c_str());
            if (items[draggedOpt.name].inst != NULL) {
                window->WorkRect = orignalRect;
                ImVec2 pos = ImGui::GetCursorPos();
                ImGui::SetCursorPosX(pos.x + checkboxOffset.x);
                ImGui::SetCursorPosY(pos.y + checkboxOffset.y);
                bool enabled = items[draggedOpt.name].inst->isEnabled();
                ImGui::Checkbox(("##_menu_checkbox_" + draggedOpt.name).c_str(), &enabled);
                ImGui::SetCursorPos(pos);
            }
            style::endDisabled();
            window->DrawList->AddRect(posMin, posMax, textColor, 0.0f, 0, style::uiScale);
        }
        displayedCount++;

        MenuItem_t& item = items[opt.name];


        ImRect orginalRect = window->WorkRect;
        float buttonSize = ImGui::GetTextLineHeight() + (6.0f * style::uiScale);
        window->WorkRect = ImRect(orginalRect.Min, ImVec2(orginalRect.Max.x - buttonSize - (item.inst != NULL ? buttonSize : 0.0f), orginalRect.Max.y));

        ImVec2 posMin = ImGui::GetCursorScreenPos();
        ImVec2 posMax = ImVec2(posMin.x + menuWidth, posMin.y + ImGui::GetFrameHeight());

        headerTops[displayedCount - 1] = posMin.y;
        optionIDs[displayedCount - 1] = rawId - 1;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(posMin, posMax)) {
            menuClicked = true;
            clickedMenuName = opt.name;
        }

        bool menuDragged = (menuClicked && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && draggedMenuName.empty() && clickedMenuName == opt.name);
        if (menuDragged && !locked) {
            draggedMenuName = opt.name;
            draggedId = rawId - 1;
            draggedOpt = opt;
            continue;
        }
        else if (menuDragged) {
            ImGui::SetTooltip("Menu Order Locked!");
        }

        // Draw menu header and menu content
        if (updateStates || opt.applyOpen) {
            ImGui::SetNextItemOpen(opt.open);
            opt.applyOpen = false;
        }
        bool headerOpen = ImGui::CollapsingHeader((opt.name + "##sdrpp_main_menu").c_str());
        window->WorkRect = orginalRect;

        {
            ImVec2 pos = ImGui::GetCursorPos();
            float lineHeight = ImGui::GetTextLineHeight();

            if (item.inst != NULL) {
                ImGui::SetCursorPosX(pos.x + checkboxOffset.x);
                ImGui::SetCursorPosY(pos.y + checkboxOffset.y);
                bool enabled = item.inst->isEnabled();
                if (ImGui::Checkbox(("##_menu_checkbox_" + opt.name).c_str(), &enabled)) {
                    enabled ? item.inst->enable() : item.inst->disable();
                    changed = true;
                }
            }

            // Detach button
            float buttonX = pos.x + checkboxOffset.x - (item.inst != NULL ? lineHeight + (6.0f * style::uiScale) : 0.0f);
            ImGui::SetCursorPos(ImVec2(buttonX, pos.y + checkboxOffset.y + ImGui::GetStyle().FramePadding.y));
            if (ImGui::InvisibleButton(("##_menu_detach_" + opt.name).c_str(), ImVec2(lineHeight, lineHeight))) {
                opt.detached = true;
                spawnName = opt.name;
                spawnPos = ImGui::GetMousePos();
                changed = true;
            }
            bool hovered = ImGui::IsItemHovered();
            ImVec2 a = ImGui::GetItemRectMin();
            float sz = lineHeight;
            ImDrawList* dl = window->DrawList;
            ImU32 col = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
            float th = std::max(1.0f, style::uiScale);
            dl->AddRect(ImVec2(a.x + 0.10f * sz, a.y + 0.35f * sz), ImVec2(a.x + 0.65f * sz, a.y + 0.90f * sz), col, 0.0f, 0, th);
            dl->AddLine(ImVec2(a.x + 0.40f * sz, a.y + 0.60f * sz), ImVec2(a.x + 0.90f * sz, a.y + 0.10f * sz), col, th);
            dl->AddLine(ImVec2(a.x + 0.90f * sz, a.y + 0.10f * sz), ImVec2(a.x + 0.60f * sz, a.y + 0.10f * sz), col, th);
            dl->AddLine(ImVec2(a.x + 0.90f * sz, a.y + 0.10f * sz), ImVec2(a.x + 0.90f * sz, a.y + 0.40f * sz), col, th);
            if (hovered) { ImGui::SetTooltip("Detach into a dock panel"); }

            ImGui::SetCursorPos(pos);
        }

        if (headerOpen) {
            // Check if the state changed
            if (!opt.open && !updateStates) {
                opt.open = true;
                changed = true;
            }

            item.drawHandler(item.ctx);
            ImGui::Spacing();
        }
        else if (opt.open && !updateStates) {
            opt.open = false;
            changed = true;
        }
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && menuClicked) {

        if (!draggedMenuName.empty()) {
            // Move menu
            order.erase(order.begin() + draggedId);

            if (insertBefore == displayedCount) {
                order.push_back(draggedOpt);
            }
            else if (insertBeforeName != "") {
                int beforeId = 0;
                for (int i = 0; i < order.size(); i++) {
                    if (order[i].name == insertBeforeName) {
                        beforeId = i;
                        break;
                    }
                }
                order.insert(order.begin() + beforeId, draggedOpt);
            }
            changed = true;
        }

        menuClicked = false;
        draggedMenuName = "";
        insertBeforeName = "";
        insertBefore = -1;
    }

    // TODO: Figure out why the hell this is needed
    if (insertBefore == displayedCount && !draggedMenuName.empty()) {
        if (updateStates) { ImGui::SetNextItemOpen(false); }
        ImVec2 posMin = ImGui::GetCursorScreenPos();
        ImVec2 posMax = ImVec2(posMin.x + menuWidth, posMin.y + ImGui::GetFrameHeight());
        style::beginDisabled();
        ImRect orignalRect = window->WorkRect;
        ImGui::CollapsingHeader((draggedMenuName + "##sdrpp_main_menu_dragging").c_str());
        if (items[draggedOpt.name].inst != NULL) {
            window->WorkRect = orignalRect;
            ImVec2 pos = ImGui::GetCursorPos();
            ImGui::SetCursorPosX(pos.x + checkboxOffset.x);
            ImGui::SetCursorPosY(pos.y + checkboxOffset.y);
            bool enabled = items[draggedOpt.name].inst->isEnabled();
            ImGui::Checkbox(("##_menu_checkbox_" + draggedOpt.name).c_str(), &enabled);
            ImGui::SetCursorPos(pos);
        }
        style::endDisabled();
        window->DrawList->AddRect(posMin, posMax, textColor, 0.0f, 0, style::uiScale);
    }

    if (!draggedMenuName.empty()) {
        insertBefore = displayedCount;
        ImVec2 mPos = ImGui::GetMousePos();
        for (int i = 0; i < displayedCount; i++) {
            if (headerTops[i] > mPos.y) {
                insertBefore = i;
                insertBeforeName = order[optionIDs[i]].name;
                break;
            }
        }
    }

    return changed;
}

bool Menu::drawDetached() {
    bool changed = false;

    for (MenuOption_t& opt : order) {
        auto it = items.find(opt.name);
        if (!opt.detached || it == items.end()) {
            continue;
        }
        MenuItem_t& item = it->second;

        // Only position it when just detached
        if (spawnName == opt.name) {
            ImGui::SetNextWindowPos(spawnPos, ImGuiCond_Always);
            spawnName = "";
        }
        ImGui::SetNextWindowSize(ImVec2(320.0f * style::uiScale, 380.0f * style::uiScale), ImGuiCond_FirstUseEver);

        bool open = true;
        if (ImGui::Begin(opt.name.c_str(), &open)) {
            if (item.inst != NULL) {
                bool enabled = item.inst->isEnabled();
                if (ImGui::Checkbox(("Enabled##_menu_checkbox_" + opt.name).c_str(), &enabled)) {
                    enabled ? item.inst->enable() : item.inst->disable();
                    changed = true;
                }
            }
            item.drawHandler(item.ctx);
        }
        ImGui::End();

        // Closing the window puts the section back
        if (!open) {
            opt.detached = false;
            opt.applyOpen = true;
            changed = true;
        }
    }

    return changed;
}

bool Menu::isInOrderList(std::string name) {
    for (MenuOption_t opt : order) {
        if (opt.name == name) {
            return true;
        }
    }
    return false;
}
