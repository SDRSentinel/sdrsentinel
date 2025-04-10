//
// Created by Süleyman Poyraz on 2.10.2024.
//

#include <gui/layout.h>
#include <gui/layout_builder.h>
#include <json.hpp>
#include <utils/flog.h>
#include <core.h>
#include <map>
#include <filesystem>

using json = nlohmann::json;

std::map<ImGuiID, ImGuiID> dictionary;

// Layouts are kept in the root dir, not in the app bundle
static std::string getLayoutPath(const std::string& name) {
    std::string dir = (std::string)core::args["root"] + "/layouts";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir + "/" + name + ".json";
}

json serializeDockNode(ImGuiDockNode* node)
{
    if (!node) return nullptr;

    json j;
    j["NodeID"] = node->ID;
    j["SplitAxis"] = node->SplitAxis;
    j["Size"] = {node->Size.x, node->Size.y};
    j["SharedFlags"] = node->SharedFlags;
    j["LocalFlags"] = node->LocalFlags;
    j["LocalFlagsInWindows"] = node->LocalFlagsInWindows;
    j["MergedFlags"] = node->MergedFlags;
    j["State"] = node->State;

    // Serialize docked windows
    if (!node->Windows.empty()) {
        j["Windows"] = json::array();
        for (ImGuiWindow *window: node->Windows) {
            json window_info;
            window_info["WindowID"] = window->ID;
            window_info["WindowName"] = window->Name;
            j["Windows"].push_back(window_info);
        }
    }

    // Serialize child nodes (if any)
    if (node->ChildNodes[0]) {
        j["ChildNode1"] = serializeDockNode(node->ChildNodes[0]);
    }
    if (node->ChildNodes[1]) {
        j["ChildNode2"] = serializeDockNode(node->ChildNodes[1]);
    }

    return j;
}

DockNodeInfo deserializeDockNode(const json& j)
{
    DockNodeInfo nodeInfo;
    nodeInfo.nodeID = j["NodeID"].get<ImGuiID>();
    nodeInfo.splitAxis = j["SplitAxis"].get<int>();
    nodeInfo.size = ImVec2(j["Size"][0].get<float>(), j["Size"][1].get<float>());
    nodeInfo.sharedFlags = j["SharedFlags"].get<ImGuiDockNodeFlags>();
    nodeInfo.localFlags = j["LocalFlags"].get<ImGuiDockNodeFlags>();
    nodeInfo.mergedFlags = j["MergedFlags"].get<ImGuiDockNodeFlags>();
    nodeInfo.state = j["State"].get<int>();

    // Deserialize the windows associated with the node
    if (j.contains("Windows")) {
        for (const auto &window: j["Windows"]) {
            nodeInfo.windows.push_back(window["WindowName"].get<std::string>());
        }
    }

    // Deserialize child nodes recursively
    if (j.contains("ChildNode1")) {
        nodeInfo.child[0] = new DockNodeInfo(deserializeDockNode(j["ChildNode1"]));
    }
    if (j.contains("ChildNode2")) {
        nodeInfo.child[1] = new DockNodeInfo(deserializeDockNode(j["ChildNode2"]));
    }

    return nodeInfo;
}

void createDockFromInfo(ImGuiID dockspaceID, const DockNodeInfo& info)
{
    // Set up the dock node (starting with the dockspace root node)
    if (info.child[0] != nullptr || info.child[1] != nullptr) {
        // Split the dock node if it has children
        ImGuiID nodeID = info.nodeID;
        ImGuiID leftID = info.child[0]->nodeID;
        ImGuiID rightID = info.child[1]->nodeID;
        if (dictionary.size() != 0) {
            nodeID = dictionary[nodeID];
        }

        ImGuiDir splitDir;
        float splitRatio = 0.5f;

        if (info.splitAxis == -1) {
            splitDir = ImGuiDir_None;
        } else if (info.splitAxis == 0) {
            splitDir = (ImGuiDir) (info.splitAxis);
        } else {
            splitDir = (ImGuiDir) (2 * info.splitAxis);
        }
        if (info.splitAxis == 0) {
            splitRatio = info.child[0]->size.x / (info.child[0]->size.x + info.child[1]->size.x);
        } else {
            splitRatio = info.child[0]->size.y / (info.child[0]->size.y + info.child[1]->size.y);
        }
        // Split the main dockspace node into left and right nodes

        ImGui::DockBuilderSplitNode(nodeID, splitDir, splitRatio, &leftID, &rightID);

        dictionary[info.child[0]->nodeID] = leftID;
        dictionary[info.child[1]->nodeID] = rightID;

        //ImGui::DockBuilderSplitNode(info.nodeID, splitDir, splitRatio,
        //                            &nodeID, &parentID);

        // Recursively handle child nodes
        if (info.child[0]) {
            createDockFromInfo(dockspaceID, *info.child[0]);
        }
        if (info.child[1]) {
            createDockFromInfo(dockspaceID, *info.child[1]);
        }
    } else {
        // If the node has no children, dock the windows
        for (const auto& window_name : info.windows) {
            ImGui::DockBuilderDockWindow(window_name.c_str(), dictionary[info.nodeID]);
        }
    }
}

void layout::createDockLayoutFromJson(ImGuiID& dockspaceID, ImVec2 availableSpaceForDocking, std::string filename) {
    // Parse the JSON
    // Open the JSON file
    dictionary.clear();

    float XSplit = availableSpaceForDocking.x;
    float XSplitLeft = XSplit/ 10.0f * 3.0f;
    float XSplitRight = XSplit - XSplitLeft;

    float YSplit = availableSpaceForDocking.y;
    float YSplitTop = YSplit / 10.0f * 6.0f;
    float YSplitBottom = YSplit - YSplitTop;

    LayoutBuilder builder;

    json menuNode = LayoutBuilder()
            .setMergedFlags(ImGuiDockNodeFlags_PassthruCentralNode)
            .setNodeID("MenuNode")
            .setSharedFlags(ImGuiDockNodeFlags_PassthruCentralNode)
            .setSize(XSplitLeft, YSplitTop)
            .setSplitAxis(ImGuiAxis_None)
            .setState(ImGuiDockNodeState_HostWindowVisible)
            .addWindow("Menu")
            .build();

    json debugNode = LayoutBuilder()
            .setMergedFlags(ImGuiDockNodeFlags_PassthruCentralNode)
            .setNodeID("DebugNode")
            .setSharedFlags(ImGuiDockNodeFlags_PassthruCentralNode)
            .setSize(XSplitLeft, YSplitBottom)
            .setSplitAxis(ImGuiAxis_None)
            .setState(ImGuiDockNodeState_HostWindowVisible)
            .addWindow("Debug")
            .addWindow("Module Manager")
            .build();

    json waterfallNode = LayoutBuilder()
            .setMergedFlags(ImGuiDockNodeFlags_PassthruCentralNode)
            .setNodeID("WaterfallNode")
            .setSharedFlags(ImGuiDockNodeFlags_PassthruCentralNode)
            .setSize(XSplitRight, YSplit)
            .setSplitAxis(ImGuiAxis_None)
            .setState(ImGuiDockNodeState_HostWindowVisible)
            .addWindow("Waterfall")
            .build();

    json leftSideDock = LayoutBuilder()
            .setMergedFlags(ImGuiDockNodeFlags_PassthruCentralNode)
            .setNodeID("DockSpaceLeft")
            .setSharedFlags(ImGuiDockNodeFlags_PassthruCentralNode)
            .setSize(XSplitLeft, YSplit)
            .setSplitAxis(ImGuiAxis_Y)
            .setState(ImGuiDockNodeState_Unknown)
            .setChildNode1(menuNode)
            .setChildNode2(debugNode)
            .build();


    json dockWidgetConfig = LayoutBuilder()
            .setLocalFlags(ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode)
            .setMergedFlags(ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode)
            .setNodeID("DockSpace")
            .setSharedFlags(ImGuiDockNodeFlags_PassthruCentralNode)
            .setSize(XSplit, YSplit)
            .setSplitAxis(ImGuiAxis_X)
            .setState(ImGuiDockNodeState_Unknown)
            .setChildNode1(leftSideDock)
            .setChildNode2(waterfallNode)
            .build();

    flog::info("Loading config");

    std::string filepath = getLayoutPath(filename);

    layoutConfig.setPath(filepath);
    layoutConfig.load(dockWidgetConfig);
    layoutConfig.enableAutoSave();

    // std::ifstream input_file(filename);
    // if (!input_file.is_open()) {
    //     flog::error("Could not open file: {0}\n", filename.c_str());
    //    return;
    // }


    // Deserialize root node
    layoutConfig.acquire();
    DockNodeInfo root_node_info = deserializeDockNode(layoutConfig.conf);
    layoutConfig.release();

    // Remove any existing dockspace and prepare for a new layout
    ImGui::DockBuilderRemoveNode(dockspaceID); // Clear out existing dockspace

    // Remove any existing dockspace and prepare for a new layout
    ImGuiID dockspace_id = root_node_info.nodeID;
    ImGui::DockSpace(dockspace_id, availableSpaceForDocking, ImGuiDockNodeFlags_None);

    ImGui::DockBuilderRemoveNode(dockspace_id); // Clear out existing dockspace
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_None); // Add the main dockspace
    dockspaceID = dockspace_id;

    // Rebuild the layout from the deserialized tree
    createDockFromInfo(dockspace_id, root_node_info);
    ImGui::DockBuilderFinish(dockspace_id);
}

void layout::printAllDockNodesAsJson(ImGuiDockNode* node, std::string filename) {
    if (node) {
        // Serialize the dock node tree into JSON
        std::string filepath = getLayoutPath(filename);

        layoutConfig.acquire();
        layoutConfig.setPath(filepath);
        layoutConfig.conf = serializeDockNode(node);
        layoutConfig.release();
        layoutConfig.save();
    } else {
        flog::error("DockSpace node not found.");
    }
}
