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
#include <pthread.h>

extern "C" {
#include "../twcommon.h"
}
#include "minuitwrp/minui.h"

#include "rapidxml.hpp"
#include "objects.hpp"
#include "../data.hpp"

// Global WLAN list
static std::vector<GUIWlanList::WlanItem> gWlanList;
static pthread_mutex_t gWlanListMutex = PTHREAD_MUTEX_INITIALIZER;

void SetWlanList(const std::vector<GUIWlanList::WlanItem>& list) {
	pthread_mutex_lock(&gWlanListMutex);
	gWlanList = list;
	pthread_mutex_unlock(&gWlanListMutex);
}

std::vector<GUIWlanList::WlanItem> GetWlanList() {
	pthread_mutex_lock(&gWlanListMutex);
	std::vector<GUIWlanList::WlanItem> list = gWlanList;
	pthread_mutex_unlock(&gWlanListMutex);
	return list;
}

GUIWlanList::GUIWlanList(xml_node<>* node) : GUIScrollList(node)
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
		DataManager::GetValue(mVariable, currentValue);
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

	updateList = true;
}

GUIWlanList::~GUIWlanList()
{
}

int GUIWlanList::Update(void)
{
	if (!isConditionTrue())
		return 0;

	if (updateList) {
		mList = GetWlanList();
		SetVisibleListLocation(0);
		updateList = false;
		mUpdate = 1;
	}

	int ret = GUIScrollList::Update();

	if (mUpdate) {
		mUpdate = 0;
		Render();
		return 2;
	}

	return ret;
}

int GUIWlanList::NotifyVarChange(const std::string& varName, const std::string& value)
{
	GUIScrollList::NotifyVarChange(varName, value);

	if (!isConditionTrue())
		return 0;

	if (varName == "tw_wlan_list_update") {
		// Force immediate update when variable changes
		mList = GetWlanList();
		SetVisibleListLocation(0);
		updateList = false;
		mUpdate = 1;
		return 0;
	}

	return 0;
}

void GUIWlanList::SetPageFocus(int inFocus)
{
	GUIScrollList::SetPageFocus(inFocus);
	if (inFocus) {
		// Force immediate update when page gains focus
		mList = GetWlanList();
		SetVisibleListLocation(0);
		updateList = false;
		// Select first item by default if list is not empty
		if (!mList.empty() && !mList.at(0).selected) {
			mList.at(0).selected = 1;
			if (!mVariable.empty()) {
				DataManager::SetValue(mVariable, mList.at(0).ssid);
				DataManager::SetValue("tw_selected_wlan_encryption", mList.at(0).encryption);
			}
		}
		mUpdate = 1;
	}
}

size_t GUIWlanList::GetItemCount()
{
	return mList.size();
}

void GUIWlanList::RenderItem(size_t itemindex, int yPos, bool selected)
{
	if (itemindex >= mList.size())
		return;

	ImageResource* icon = mList.at(itemindex).selected ? mIconSelected : mIconUnselected;
	std::string text = mList.at(itemindex).ssid;
	if (!mList.at(itemindex).signal.empty()) {
		text += " (" + mList.at(itemindex).signal + ")";
	}

	RenderStdItem(yPos, selected, icon, text.c_str());
}

void GUIWlanList::NotifySelect(size_t item_selected)
{
	if (item_selected >= mList.size())
		return;

	int listSize = mList.size();
	for (int i = 0; i < listSize; i++)
		mList.at(i).selected = 0;

	mList.at(item_selected).selected = 1;
	mUpdate = 1;

	if (!mVariable.empty()) {
		DataManager::SetValue(mVariable, mList.at(item_selected).ssid);
		// Also save encryption type for password page
		DataManager::SetValue("tw_selected_wlan_encryption", mList.at(item_selected).encryption);
	}
}

