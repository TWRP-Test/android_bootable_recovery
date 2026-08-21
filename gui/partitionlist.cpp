/*
	Copyright 2020 TeamWin
	This file is part of TWRP/TeamWin Recovery Project.

	TWRP is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	TWRP is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with TWRP.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <format>
#include <android-base/strings.h>

extern "C" {
#include "../twcommon.h"
}
#include "minuitwrp/minui.h"

#include "rapidxml.hpp"
#include "objects.hpp"
#include "../data.hpp"
#include "../partitions.hpp"
#include "../variables.h"

GUIPartitionList::GUIPartitionList(xml_node<>* node) : GUIScrollList(node)
{
	xml_attribute<>* attr;
	xml_node<>* child;

	mIconSelected = mIconUnselected = NULL;
	mUpdate = 0;
	updateList = false;

	child = FindNode(node, "icon");
	if (child)
	{
		mIconSelected = LoadAttrImage(child, "selected");
		mIconUnselected = LoadAttrImage(child, "unselected");
	}

	// Handle the result variable
	child = FindNode(node, "data");
	if (child)
	{
		attr = child->first_attribute("name");
		if (attr)
			mVariable = attr->value();
		attr = child->first_attribute("selectedlist");
		if (attr)
			selectedList = attr->value();
	}

	int iconWidth = 0, iconHeight = 0;
	if (mIconSelected && mIconSelected->GetResource() && mIconUnselected && mIconUnselected->GetResource()) {
		iconWidth = std::max(mIconSelected->GetWidth(), mIconUnselected->GetWidth());
		iconHeight = std::max(mIconSelected->GetHeight(), mIconUnselected->GetHeight());
	} else if (mIconSelected && mIconSelected->GetResource()) {
		iconWidth = mIconSelected->GetWidth();
		iconHeight = mIconSelected->GetHeight();
	} else if (mIconUnselected && mIconUnselected->GetResource()) {
		iconWidth = mIconUnselected->GetWidth();
		iconHeight = mIconUnselected->GetHeight();
	}
	SetMaxIconSize(iconWidth, iconHeight);

	child = FindNode(node, "listtype");
	if (child && (attr = child->first_attribute("name"))) {
		ListType = attr->value();
		updateList = true;
	} else {
		mList.clear();
		LOGERR("No partition listtype specified for partitionlist GUI element\n");
		return;
	}
}

GUIPartitionList::~GUIPartitionList()
{
}

int GUIPartitionList::Update(void)
{
	if (!isConditionTrue())
		return 0;

	// Check for changes in mount points if the list type is mount and update the list and render if needed
	if (ListType == "mount") {
		for (PartitionList& partition : mList) {
			if (PartitionManager.Is_Mounted_By_Path(partition.Mount_Point) && !partition.selected) {
				partition.selected = true;
				mUpdate = 1;
			} else if (!PartitionManager.Is_Mounted_By_Path(partition.Mount_Point) && partition.selected) {
				partition.selected = false;
				mUpdate = 1;
			}
		}
	}

	GUIScrollList::Update();

	if (updateList) {
		// Completely update the list if needed -- Used primarily for
		// restore as the list for restore will change depending on what
		// partitions were backed up
		mList.clear();
		PartitionManager.Get_Partition_List(ListType, &mList);
		SetVisibleListLocation(0);
		updateList = false;
		mUpdate = 1;
		if (ListType == "backup" || ListType == "flashimg")
			MatchList();
	}

	if (mUpdate) {
		mUpdate = 0;
		if (Render() == 0)
			return 2;
	}

	return 0;
}

int GUIPartitionList::NotifyVarChange(const std::string& varName, const std::string& value)
{
	GUIScrollList::NotifyVarChange(varName, value);

	if (!isConditionTrue())
		return 0;

	// A deferred data/media walk landed, so the sizes on screen are stale.
	if (ListType == "backup" && varName == TW_BACKUP_SIZES_READY) {
		updateList = true;
		mUpdate = 1;
		return 0;
	}

	if (varName == mVariable && !mUpdate)
	{
		if (ListType == "storage") {
			currentValue = value;
			SetPosition();
		} else if (ListType == "backup") {
			updateList = true;
			MatchList();
		} else if (ListType == "restore") {
			updateList = true;
			SetVisibleListLocation(0);
		}

		mUpdate = 1;
		return 0;
	}
	return 0;
}

void GUIPartitionList::SetPageFocus(int inFocus)
{
	GUIScrollList::SetPageFocus(inFocus);
	if (inFocus) {
		if (ListType == "storage" || ListType == "flashimg") {
			DataManager::GetValue(mVariable, currentValue);
			SetPosition();
		}
		updateList = true;
		mUpdate = 1;
	}
}

void GUIPartitionList::MatchList(void) {
	std::string variablelist;
	DataManager::GetValue(mVariable, variablelist);

	std::vector<std::string> selected_paths = android::base::Tokenize(variablelist, ";");
	for (PartitionList& partition : mList) {
		auto it = std::find(selected_paths.begin(), selected_paths.end(), partition.Mount_Point);
		partition.selected = it != selected_paths.end();
		if (partition.selected) {
			TWPartition* t_part = PartitionManager.Find_Partition_By_Path(partition.Mount_Point);
			DataManager::SetValue("tw_is_slot_part", t_part != nullptr ? (int) t_part->SlotSelect : 0);
		}
	}
}

void GUIPartitionList::SetPosition() {
	int listSize = mList.size();

	SetVisibleListLocation(0);
	for (int i = 0; i < listSize; i++) {
		if (mList[i].Mount_Point == currentValue) {
			mList[i].selected = true;
			SetVisibleListLocation(i);
		} else {
			mList[i].selected = false;
		}
	}
}

size_t GUIPartitionList::GetItemCount()
{
	return mList.size();
}

void GUIPartitionList::RenderItem(size_t itemindex, int yPos, bool selected)
{
	// note: the "selected" parameter above is for the currently touched item
	// don't confuse it with the more persistent "selected" flag per list item used below
	ImageResource* icon = mList[itemindex].selected ? mIconSelected : mIconUnselected;
	const std::string& text = mList[itemindex].Display_Name;

	RenderStdItem(yPos, selected, icon, text.c_str());
}

void GUIPartitionList::NotifySelect(size_t item_selected)
{
	if (item_selected < mList.size()) {
		PartitionList& selected_partition = mList[item_selected];
		if (ListType == "mount") {
			if (!selected_partition.selected) {
				if (PartitionManager.Mount_By_Path(selected_partition.Mount_Point, true)) {
					selected_partition.selected = true;
					PartitionManager.Add_MTP_Storage(selected_partition.Mount_Point);
					mUpdate = 1;
				}
			} else {
				if (PartitionManager.UnMount_By_Path(selected_partition.Mount_Point, true)) {
					selected_partition.selected = false;
					mUpdate = 1;
				}
			}
		} else if (!mVariable.empty()) {
			if (ListType == "storage") {
				TWPartition* Part = PartitionManager.Find_Partition_By_Path(selected_partition.Mount_Point);
				if (Part == nullptr) {
					LOGERR("Unable to locate partition for '%s'\n", selected_partition.Mount_Point.c_str());
					return;
				}
				bool update_size = !Part->Is_Mounted() && Part->Removable;
				if (!Part->Mount(true))
					return;
				if (update_size && !Part->Update_Size(true))
					return;

				for (PartitionList& partition : mList)
					partition.selected = false;

				if (update_size) {
					selected_partition.Display_Name = std::format("{} ({} MB)", Part->Storage_Name, Part->Free / (1024 * 1024));
				}
				selected_partition.selected = true;
				mUpdate = 1;
				DataManager::SetValue(mVariable, selected_partition.Mount_Point);
			} else {
				if (ListType == "flashimg") { // only one item can be selected for flashing images
					for (PartitionList& partition : mList)
						partition.selected = false;
				}
				selected_partition.selected = !selected_partition.selected;
				if (selected_partition.selected) {
					TWPartition* t_part = PartitionManager.Find_Partition_By_Path(selected_partition.Mount_Point);
					DataManager::SetValue("tw_is_slot_part", t_part != nullptr ? (int) t_part->SlotSelect : 0);
				}

				std::string variablelist;
				for (const PartitionList& partition : mList) {
					if (partition.selected)
						variablelist += partition.Mount_Point + ";";
				}

				mUpdate = 1;
				if (selectedList.empty())
					DataManager::SetValue(mVariable, variablelist);
				else
					DataManager::SetValue(selectedList, variablelist);
			}
		}
	}
}
