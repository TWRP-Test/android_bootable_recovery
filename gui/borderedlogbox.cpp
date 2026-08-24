/*
	Copyright 2024 TeamWin
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

// borderedlogbox.cpp - GUIBorderedLogBox object

#include <string>
#include <vector>
#include <algorithm>

extern "C" {
#include "../twcommon.h"
}
#include "twrpminui/minui.h"
#include "twrpminui/truetype.hpp"

#include "rapidxml.hpp"
#include "objects.hpp"
#include "../data.hpp"

extern int scale_theme_y(int y);

GUIBorderedLogBox::GUIBorderedLogBox(xml_node<>* node) : GUIScrollList(node)
{
	mLastRenderedCount = 0;
	mBorderWidth = 2;
	mLeftMargin = -1;
	mRightMargin = -1;
	mXMLNode = node;
	scrollToEnd = true;
	allowSelection = false;
	mTopRowValue = 0;
	mBottomRowValue = 0;
	mHeaderH = 0;
	mSeparatorH = 0;

	// Default colors
	ConvertStrToColor("white", &mBorderColor);

	if (!node)
		return;

	xml_node<>* child = FindNode(node, "color");
	if (child)
		mBorderColor = LoadAttrColor(child, "border", mBorderColor);

	mBorderWidth = LoadAttrIntScaleX(node, "borderwidth");
	if (mBorderWidth <= 0)
		mBorderWidth = 2;
	
	mLeftMargin = LoadAttrIntScaleX(node, "leftmargin");
	mRightMargin = LoadAttrIntScaleX(node, "rightmargin");

	xml_attribute<>* attr = node->first_attribute("toprow");
	if (attr) {
		mTopRowVar = attr->value();
		mTopRowValue = LoadAttrIntScaleY(node, "toprow");
	}
	
	attr = node->first_attribute("bottomrow");
	if (attr) {
		mBottomRowVar = attr->value();
		mBottomRowValue = LoadAttrIntScaleY(node, "bottomrow");
	}

	xml_node<>* placement = FindNode(node, "placement");
	int tempX = 0, tempW = 0;
	LoadPlacement(placement, &tempX, NULL, &tempW, NULL);
	
	if (mLeftMargin >= 0) {
		mRenderX = mLeftMargin;
	} else {
		mRenderX = tempX;
	}
	
	if (mRightMargin >= 0) {
		int screenWidth = gr_fb_width();
		mRenderW = screenWidth - mRenderX - mRightMargin;
	} else {
		mRenderW = tempW;
	}

	CalculateRenderPosition();
}

void GUIBorderedLogBox::CalculateRenderPosition()
{
	int topRow = mTopRowValue;
	int bottomRow = mBottomRowValue;

	if (topRow > 0 && bottomRow > 0 && bottomRow > topRow)
	{
		mRenderY = topRow;
		mRenderH = bottomRow - topRow;
	}
	else
	{
		if (mXMLNode) {
			xml_node<>* placement = FindNode(mXMLNode, "placement");
			if (placement)
			{
				int tempY = 0, tempH = 0;
				LoadPlacement(placement, NULL, &tempY, NULL, &tempH);
				if (tempY > 0)
					mRenderY = tempY;
				if (tempH > 0)
					mRenderH = tempH;
			}
		}
	}

	SetRenderPos(mRenderX, mRenderY, mRenderW, mRenderH);
	SetActionPos(mRenderX, mRenderY, mRenderW, mRenderH);
}

int GUIBorderedLogBox::Render(void)
{
	if (!isConditionTrue())
		return 0;

	int result = GUIScrollList::Render();
	
	if (mBorderWidth > 0) {
		gr_color(mBorderColor.red, mBorderColor.green, mBorderColor.blue, mBorderColor.alpha);
		gr_fill(mRenderX, mRenderY, mRenderW, mBorderWidth);
		gr_fill(mRenderX, mRenderY + mRenderH - mBorderWidth, mRenderW, mBorderWidth);
		gr_fill(mRenderX, mRenderY, mBorderWidth, mRenderH);
		gr_fill(mRenderX + mRenderW - mBorderWidth, mRenderY, mBorderWidth, mRenderH);
	}
	
	if (GetItemCount() > 0) {
		int bottom_offset = GetDisplayRemainder() - actualItemHeight;
		bool isAtBottom = firstDisplayedItem == (int)GetItemCount() - GetDisplayItemCount() - (bottom_offset != 0) && y_offset == bottom_offset;
		if (isAtBottom)
			scrollToEnd = true;
	}

	mLastRenderedCount = mLogLines.size();
	return result;
}

int GUIBorderedLogBox::Update(void)
{
	if (!isConditionTrue())
		return 0;

	if (mLogLines.size() != mLastRenderedCount) {
		mUpdate = 1;
	}

	if (scrollToEnd) {
		SetVisibleListLocation(mLogLines.size() - 1);
	}

	GUIScrollList::Update();
	
	if (mUpdate) {
		mUpdate = 0;
		return 2;
	}
	
	return 0;
}

int GUIBorderedLogBox::NotifyVarChange(const std::string& varName, const std::string& value)
{
	GUIScrollList::NotifyVarChange(varName, value);
	return 0;
}

int GUIBorderedLogBox::NotifyTouch(TOUCH_STATE state, int x, int y)
{
	if (!isConditionTrue())
		return -1;

	scrollToEnd = false;
	return GUIScrollList::NotifyTouch(state, x, y);
}

size_t GUIBorderedLogBox::GetItemCount()
{
	return mLogLines.size();
}

void GUIBorderedLogBox::RenderItem(size_t itemindex, int yPos, bool selected __unused)
{
	if (!mFont || !mFont->GetResource())
		return;

	std::string color = (itemindex < mLogColors.size()) ? mLogColors[itemindex] : "normal";
	if (color == "normal") {
		gr_color(mFontColor.red, mFontColor.green, mFontColor.blue, mFontColor.alpha);
	} else {
		COLOR lineColor;
		ConvertStrToColor(color, &lineColor);
		lineColor.alpha = 255;
		gr_color(lineColor.red, lineColor.green, lineColor.blue, lineColor.alpha);
	}

	const char* text = mLogLines[itemindex].c_str();
	gr_textEx_scaleW(mRenderX + 5, yPos, text, mFont->GetResource(), mRenderW - 10, TOP_LEFT, 0);
}

void GUIBorderedLogBox::AddLogLine(const std::string& line, const std::string& color)
{
	mLogLines.push_back(line);
	mLogColors.push_back(color);
	
	if (scrollToEnd) {
		SetVisibleListLocation(mLogLines.size() - 1);
	}
}

void GUIBorderedLogBox::ClearLogs()
{
	mLogLines.clear();
	mLogColors.clear();
	mLastRenderedCount = 0;
	firstDisplayedItem = 0;
	y_offset = 0;
	scrollingSpeed = 0;
	scrollToEnd = true;
}
