#include "multilistbox.h"
#include "dependencies/pugixml/src/pugixml.hpp"
#include "forms/scrollbar.h"
#include "framework/event.h"
#include "framework/framework.h"
#include "framework/renderer.h"

namespace OpenApoc
{

MultilistBox::MultilistBox() : MultilistBox(nullptr) {}

MultilistBox::MultilistBox(sp<ScrollBar> ExternalScrollBar)
	: Control(), scroller(ExternalScrollBar),
	ItemSize(0), ItemSpacing(0), ListOrientation(Orientation::Vertical),
	ScrollOrientation(ListOrientation), HoverColour(0, 0, 0, 0), SelectedColour(0, 0, 0, 0),
	AlwaysEmitSelectionEvents(true)
{
}

MultilistBox::~MultilistBox() = default;

void MultilistBox::preRender()
{
	Control::preRender();

	if (isDirty() && !scroller)
	{
		// MultilistBox without scroller should be rendered fully
		int sizeX = 0, sizeY = 0;

		if (isVisible())
		{
			bool removeLastSpacing = false;
			for (auto c : Controls)
			{
				if (c->isVisible())
				{
					sizeX = std::max(sizeX, c->Size.x);
					sizeY += c->Size.y + ItemSpacing;
					removeLastSpacing = true;
				}
			}
			sizeY -= removeLastSpacing ? ItemSpacing : 0;
		}
		
		Size.x = sizeX;
		Size.y = sizeY;

		// Adjusting the parent size
		if (auto parent = owningControl.lock())
		{
			if (parent->SelectionSize.x == 0) parent->SelectionSize.x = parent->Size.x;
			if (parent->SelectionSize.y == 0) parent->SelectionSize.y = parent->Size.y;

			parent->Size.x = std::max(parent->SelectionSize.x, Location.x + sizeX);
			parent->Size.y = std::max(parent->SelectionSize.x, Location.y + sizeY);
		}
	}
}

void MultilistBox::onRender()
{
	Control::onRender();

	if (isDirty())
	{
		resolveLocation();
	}

	Vec2<int> controlOffset;

	for (auto c = Controls.begin(); c != Controls.end(); c++)
	{
		auto ctrl = *c;
		if (ctrl != scroller && ctrl->isVisible())
		{
			ctrl->Location = controlOffset - this->scrollOffset;

			if (ListOrientation == ScrollOrientation && ItemSize != 0)
			{
				switch (ScrollOrientation)
				{
					case Orientation::Vertical:
						ctrl->Size.x = this->Size.x;
						ctrl->Size.y = ItemSize;
						break;
					case Orientation::Horizontal:
						ctrl->Size.x = ItemSize;
						ctrl->Size.y = this->Size.y;
						break;
				}
			}

			switch (ListOrientation)
			{
				case Orientation::Vertical:
					controlOffset.y += ctrl->Size.y + ItemSpacing;
					if (ListOrientation != ScrollOrientation && controlOffset.y >= Size.y)
					{
						controlOffset.y = 0;
						controlOffset.x += ctrl->Size.x + ItemSpacing;
					}
					break;
				case Orientation::Horizontal:
					controlOffset.x += ctrl->Size.x + ItemSpacing;
					if (ListOrientation != ScrollOrientation && controlOffset.x >= Size.x)
					{
						controlOffset.x = 0;
						controlOffset.y += ctrl->Size.y + ItemSpacing;
					}
					break;
			}
		}
	}

	//resolveLocation();

	if (scroller)
	{
		switch (ScrollOrientation)
		{
			case Orientation::Vertical:
				scroller->setMaximum(std::max(controlOffset.y - this->Size.y, scroller->getMinimum()));
				break;
			case Orientation::Horizontal:
				scroller->setMaximum(std::max(controlOffset.x - this->Size.x, scroller->getMinimum()));
				break;
		}
		scroller->updateLargeChangeValue();
	}
}

void MultilistBox::postRender()
{
	Control::postRender();

	for (auto c = Controls.begin(); c != Controls.end(); c++)
	{
		auto ctrl = *c;
		if (ctrl != scroller && ctrl->isVisible())
		{
			if (ctrl == hovered)
			{
				if (HoverImage)
				{
					fw().renderer->draw(HoverImage, ctrl->Location);
				}
				else
				{
					fw().renderer->drawRect(ctrl->Location, ctrl->SelectionSize, HoverColour);
				}
			}
			//if (ctrl == selected)
			//{
			//	if (SelectedImage)
			//	{
			//		fw().renderer->draw(SelectedImage, ctrl->Location);
			//	}
			//	else
			//	{
			//		fw().renderer->drawRect(ctrl->Location, ctrl->Size, SelectedColour);
			//	}
			//}
		}
	}
}

void MultilistBox::eventOccured(Event *e)
{
	// MultilistBox does not pass mousedown and mouseup events when out of bounds
	if ((e->type() != EVENT_MOUSE_DOWN && e->type() != EVENT_MOUSE_UP) || eventIsWithin(e))
	{
		Control::eventOccured(e);
	}
	if (e->type() == EVENT_FORM_INTERACTION)
	{
		sp<Control> ctrl = e->forms().RaisedBy;
		sp<Control> child = ctrl->getAncestor(shared_from_this());

		if (e->forms().EventFlag == FormEventType::MouseMove)
		{
			// FIXME: Scrolling amount should match wheel amount
			// Should wheel orientation match as well? Who has horizontal scrolls??
			if ((ctrl == shared_from_this() || child != nullptr) && scroller)
			{
				int wheelDelta =
					e->forms().MouseInfo.WheelVertical + e->forms().MouseInfo.WheelHorizontal;
				if (wheelDelta > 0)
				{
					scroller->scrollPrev();
				}
				else if (wheelDelta < 0)
				{
					scroller->scrollNext();
				}
			}
			const Vec2<int>& Size = (child->SelectionSize.x == 0 || child->SelectionSize.y == 0) ? child->Size : child->SelectionSize;
			if (ctrl == shared_from_this() || ctrl == scroller || !isPointInsideControlBounds(e, child))
			{
				child = nullptr;
			}
			if (hovered != child)
			{
				hovered = child;
				this->pushFormEvent(FormEventType::ListBoxChangeHover, e);
			}
		}
		else if (e->forms().EventFlag == FormEventType::MouseDown)
		{
			if (ctrl == shared_from_this() || ctrl == scroller)
			{
				child = nullptr;
			}
			//if ((AlwaysEmitSelectionEvents || selected != child) && child != nullptr)
			//{
			//	selected = child;
			//	this->pushFormEvent(FormEventType::ListBoxChangeSelected, e);
			//}
		}
	}
}

void MultilistBox::update()
{
	Control::update();

	if (scroller)
	{
		scroller->update();
		Vec2<int> newScrollOffset = this->scrollOffset;
		switch (ScrollOrientation)
		{
			case Orientation::Vertical:
				newScrollOffset.y = scroller->getValue();
				break;
			case Orientation::Horizontal:
				newScrollOffset.x = scroller->getValue();
				break;
		}
		if (newScrollOffset != this->scrollOffset)
		{
			this->scrollOffset = newScrollOffset;
			this->setDirty();
		}
	}
}

void MultilistBox::unloadResources() {}

void MultilistBox::clear()
{
	for (auto &c : Controls)
	{
		c->setParent(nullptr);
	}
	Controls.clear();
	this->selected.clear();
	this->hovered = nullptr;

	//resolveLocation();
	this->setDirty();
}

void MultilistBox::addItem(sp<Control> Item)
{
	Item->setParent(shared_from_this());
	//resolveLocation();
	this->setDirty();
}

void MultilistBox::replaceItem(sp<Control> Item)
{
	auto newData = Item->getData<void>();
	this->setDirty();
	bool found = false;
	for (size_t i = 0; i < Controls.size(); i++)
	{
		auto oldItem = Controls[i];
		if (oldItem->getData<void>() == newData)
		{
			Controls.erase(Controls.begin() + i);
			Item->setParent(shared_from_this(), i);
//			resolveLocation();
			//if (oldItem == this->selected)
			//{
			//	this->selected = Item;
			//}
			if (oldItem == this->hovered)
			{
				this->hovered = Item;
			}
			found = true;
			break;
		}
	}
	if (!found)
	{
		addItem(Item);
	}
}

sp<Control> MultilistBox::removeItem(sp<Control> Item)
{
	this->setDirty();
	//if (Item == this->selected)
	//{
	//	this->selected = nullptr;
	//}
	if (Item == this->hovered)
	{
		this->hovered = nullptr;
	}
	for (auto i = Controls.begin(); i != Controls.end(); i++)
	{
		if (*i == Item)
		{
			Controls.erase(i);
			resolveLocation();
			Item->setParent(nullptr);
			return Item;
		}
	}
	return nullptr;
}

sp<Control> MultilistBox::removeItem(int Index)
{
	this->setDirty();
	auto c = Controls.at(Index);
	Controls.erase(Controls.begin() + Index);
//	resolveLocation();
	//if (c == this->selected)
	//{
	//	this->selected = nullptr;
	//}
	//if (c == this->hovered)
	//{
	//	this->selected = nullptr;
	//}
	c->setParent(nullptr);
	return c;
}

sp<Control> MultilistBox::operator[](int Index) { return Controls.at(Index); }

sp<Control> MultilistBox::copyTo(sp<Control> CopyParent)
{
	sp<MultilistBox> copy;
	sp<ScrollBar> scrollCopy = std::dynamic_pointer_cast<ScrollBar>(scroller->lastCopiedTo.lock());

	if (CopyParent)
	{
		copy = CopyParent->createChild<MultilistBox>(scrollCopy);
	}
	else
	{
		copy = mksp<MultilistBox>(scrollCopy);
	}

	copy->ItemSize = this->ItemSize;
	copy->ItemSpacing = this->ItemSpacing;
	copy->ListOrientation = this->ListOrientation;
	copy->ScrollOrientation = this->ScrollOrientation;
	copy->HoverColour = this->HoverColour;
	copy->SelectedColour = this->SelectedColour;
	copyControlData(copy);
	return copy;
}

void MultilistBox::configureSelfFromXml(pugi::xml_node *node)
{
	Control::configureSelfFromXml(node);

	auto itemNode = node->child("item");
	if (itemNode)
	{
		if (itemNode.attribute("size"))
		{
			ItemSize = itemNode.attribute("size").as_int();
		}
		if (itemNode.attribute("spacing"))
		{
			ItemSpacing = itemNode.attribute("spacing").as_int();
		}
	}
	auto orientationNode = node->child("orientation");
	if (orientationNode)
	{
		UString value = orientationNode.text().get();
		if (value == "horizontal")
		{
			ListOrientation = Orientation::Horizontal;
			ScrollOrientation = Orientation::Horizontal;
		}
		else if (value == "vertical")
		{
			ListOrientation = Orientation::Vertical;
			ScrollOrientation = Orientation::Vertical;
		}
		if (orientationNode.attribute("list"))
		{
			value = orientationNode.attribute("list").as_string();
			if (value == "horizontal")
			{
				ListOrientation = Orientation::Horizontal;
			}
			else if (value == "vertical")
			{
				ListOrientation = Orientation::Vertical;
			}
		}
		if (orientationNode.attribute("scroll"))
		{
			value = orientationNode.attribute("scroll").as_string();
			if (value == "horizontal")
			{
				ScrollOrientation = Orientation::Horizontal;
			}
			else if (value == "vertical")
			{
				ScrollOrientation = Orientation::Vertical;
			}
		}
	}
	auto hoverColourNode = node->child("hovercolour");
	if (hoverColourNode)
	{
		uint8_t r = hoverColourNode.attribute("r").as_uint(0);
		uint8_t g = hoverColourNode.attribute("g").as_uint(0);
		uint8_t b = hoverColourNode.attribute("b").as_uint(0);
		uint8_t a = hoverColourNode.attribute("a").as_uint(255);
		HoverColour = { r, g, b, a };
	}
	auto selColourNode = node->child("selcolour");
	if (selColourNode)
	{
		uint8_t r = selColourNode.attribute("r").as_uint(0);
		uint8_t g = selColourNode.attribute("g").as_uint(0);
		uint8_t b = selColourNode.attribute("b").as_uint(0);
		uint8_t a = selColourNode.attribute("a").as_uint(255);
		SelectedColour = { r, g, b, a };
	}
}

void MultilistBox::setSelected(sp<Control> c)
{
	// A sanity check to make sure the selected control actually belongs to this list
	bool found = false;
	for (auto child : this->Controls)
	{
		if (child == c)
		{
			found = true;
			break;
		}
	}
	if (!found)
	{
		LogError(
			"Trying set MultilistBox selected control to something that isn't a member of the list");
	}
	this->selected.push_back(c);
	this->setDirty();
}

std::vector<sp<Control>> MultilistBox::getSelectedItems() { return selected; }

sp<Control> MultilistBox::getHoveredItem() { return hovered; }


}