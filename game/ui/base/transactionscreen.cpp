#include "game/ui/base/transactionscreen.h"
#include "forms/form.h"
#include "forms/graphic.h"
#include "forms/graphicbutton.h"
#include "forms/label.h"
#include "forms/listbox.h"
#include "forms/radiobutton.h"
#include "forms/scrollbar.h"
#include "forms/ui.h"
#include "framework/configfile.h"
#include "framework/data.h"
#include "framework/event.h"
#include "framework/font.h"
#include "framework/framework.h"
#include "framework/keycodes.h"
#include "framework/logger.h"
#include "framework/renderer.h"
#include "game/state/city/base.h"
#include "game/state/city/building.h"
#include "game/state/city/vehicle.h"
#include "game/state/city/vehiclemission.h"
#include "game/state/city/vequipment.h"
#include "game/state/gamestate.h"
#include "game/state/rules/aequipmenttype.h"
#include "game/state/rules/battle/damage.h"
#include "game/state/rules/city/vammotype.h"
#include "game/state/rules/city/vehicletype.h"
#include "game/state/rules/city/vequipmenttype.h"
#include "game/state/shared/organisation.h"
#include "game/ui/components/controlgenerator.h"
#include "game/ui/general/aequipmentsheet.h"
#include "game/ui/general/messagebox.h"
#include "game/ui/general/vehiclesheet.h"
#include "library/strings_format.h"
#include <array>

namespace OpenApoc
{

sp<Image> TransactionScreen::TransactionControl::bgLeft;
sp<Image> TransactionScreen::TransactionControl::bgRight;
sp<Image> TransactionScreen::TransactionControl::purchaseBoxIcon;
sp<Image> TransactionScreen::TransactionControl::purchaseXComIcon;
sp<Image> TransactionScreen::TransactionControl::purchaseArrow;
sp<Image> TransactionScreen::TransactionControl::alienContainedDetain;
sp<Image> TransactionScreen::TransactionControl::alienContainedKill;
sp<Image> TransactionScreen::TransactionControl::scrollLeft;
sp<Image> TransactionScreen::TransactionControl::scrollRight;
sp<Image> TransactionScreen::TransactionControl::transactionShade;
sp<BitmapFont> TransactionScreen::TransactionControl::labelFont;
bool TransactionScreen::TransactionControl::resourcesInitialised = false;

TransactionScreen::TransactionScreen(sp<GameState> state, bool forceLimits)
    : BaseStage(state), forceLimits(forceLimits)
{
	// Load resources
	form = ui().getForm("transactionscreen");
	formItemAgent = form->findControlTyped<Form>("AGENT_ITEM_VIEW");
	formItemAgent->setVisible(false);

	formItemVehicle = form->findControlTyped<Form>("VEHICLE_ITEM_VIEW");
	formItemVehicle->setVisible(false);

	// Assign event handlers
	onScrollChange = [this](FormsEvent *) { this->updateFormValues(); };
	onHover = [this](FormsEvent *e) {
		auto tctrl = std::dynamic_pointer_cast<TransactionControl>(e->forms().RaisedBy);
		if (!tctrl)
		{
			LogError("Non-Transaction Control called a callback? WTF?");
			return;
		}
		this->displayItem(tctrl);
	};

	// Assign main form contents
	textViewBaseStatic = form->findControlTyped<Label>("TEXT_BUTTON_BASE_STATIC");

	// Adding callbacks after checking the button because we don't need to
	// have the callback be called since changeBase() will update display anyways

	form->findControlTyped<RadioButton>("BUTTON_SOLDIERS")
	    ->addCallback(FormEventType::CheckBoxSelected,
	                  [this](Event *) { this->setDisplayType(Type::Soldier); });
	form->findControlTyped<RadioButton>("BUTTON_BIOSCIS")
	    ->addCallback(FormEventType::CheckBoxSelected,
	                  [this](Event *) { this->setDisplayType(Type::Bio); });
	form->findControlTyped<RadioButton>("BUTTON_PHYSCIS")
	    ->addCallback(FormEventType::CheckBoxSelected,
	                  [this](Event *) { this->setDisplayType(Type::Physist); });
	form->findControlTyped<RadioButton>("BUTTON_ENGINRS")
	    ->addCallback(FormEventType::CheckBoxSelected,
	                  [this](Event *) { this->setDisplayType(Type::Engineer); });
	form->findControlTyped<RadioButton>("BUTTON_ALIENS")
	    ->addCallback(FormEventType::CheckBoxSelected,
	                  [this](Event *) { this->setDisplayType(Type::Aliens); });

	form->findControlTyped<RadioButton>("BUTTON_VEHICLES")
	    ->addCallback(FormEventType::CheckBoxSelected,
	                  [this](Event *) { this->setDisplayType(Type::Vehicle); });
	form->findControlTyped<RadioButton>("BUTTON_AGENTS")
	    ->addCallback(FormEventType::CheckBoxSelected,
	                  [this](Event *) { this->setDisplayType(Type::AgentEquipment); });
	form->findControlTyped<RadioButton>("BUTTON_FLYING")
	    ->addCallback(FormEventType::CheckBoxSelected,
	                  [this](Event *) { this->setDisplayType(Type::FlyingEquipment); });
	form->findControlTyped<RadioButton>("BUTTON_GROUND")
	    ->addCallback(FormEventType::CheckBoxSelected,
	                  [this](Event *) { this->setDisplayType(Type::GroundEquipment); });
}

TransactionScreen::~TransactionScreen() = default;

void TransactionScreen::changeBase(sp<Base> newBase)
{
	BaseStage::changeBase(newBase);
	textViewBaseStatic->setText(state->current_base->name);

	// Set index for all controls
	int index = getLeftIndex();
	for (auto &l : transactionControls)
	{
		for (auto &c : l.second)
		{
			c->setIndexLeft(index);
		}
	}
	// Apply display type and base highlight
	setDisplayType(type);
}

void TransactionScreen::setDisplayType(Type type)
{
	this->type = type;

	form->findControlTyped<ScrollBar>("LIST_SCROLL")->setValue(0);
	auto list = form->findControlTyped<ListBox>("LIST");
	list->clear();

	// Controls already populated - just add them below, otherwise create them first
	if (transactionControls.find(type) == transactionControls.end())
	{
		// Controls not populated - create them
		switch (type)
		{
			case Type::Soldier:
			case Type::Bio:
			case Type::Physist:
			case Type::Engineer:
				LogWarning("Implement agent exchange controls");
				break;
			case Type::Vehicle:
				populateControlsVehicle();
				break;
			case Type::AgentEquipment:
				populateControlsAgentEquipment();
				break;
			case Type::FlyingEquipment:
			case Type::GroundEquipment:
				populateControlsVehicleEquipment();
				break;
			case Type::Aliens:
				populateControlsAlien();
				break;
		}
	}
	// Highlight
	switch (type)
	{
		case Type::Soldier:
		case Type::Bio:
		case Type::Physist:
		case Type::Engineer:
			viewHighlight = BaseGraphics::FacilityHighlight::Quarters;
			break;
		case Type::Vehicle:
			viewHighlight = BaseGraphics::FacilityHighlight::None;
			break;
		case Type::AgentEquipment:
			viewHighlight = BaseGraphics::FacilityHighlight::Stores;
			break;
		case Type::FlyingEquipment:
		case Type::GroundEquipment:
			viewHighlight = BaseGraphics::FacilityHighlight::Stores;
			break;
		case Type::Aliens:
			viewHighlight = BaseGraphics::FacilityHighlight::Aliens;
			break;
	}
	// Finally add all controls
	for (auto &c : transactionControls[type])
	{
		list->addItem(c);
	}
	// Update display for bases
	updateFormValues(false);
	updateBaseHighlight();
}

int TransactionScreen::getLeftIndex()
{
	int index = 0;
	for (auto &b : state->player_bases)
	{
		if (b.first == state->current_base.id)
		{
			return index;
		}
		index++;
	}
	return 8;
}

int TransactionScreen::getRightIndex() { return 8; }

void TransactionScreen::populateControlsVehicle()
{
	int leftIndex = getLeftIndex();
	int rightIndex = getRightIndex();
	for (auto &v : state->vehicle_types)
	{
		if (state->economy.find(v.first) != state->economy.end())
		{
			auto control = TransactionControl::createControl(
			    *state, StateRef<VehicleType>{state.get(), v.first}, leftIndex, rightIndex);
			if (control)
			{
				control->addCallback(FormEventType::ScrollBarChange, onScrollChange);
				control->addCallback(FormEventType::MouseMove, onHover);
				transactionControls[type].push_back(control);
			}
		}
	}
	for (auto &v : state->vehicles)
	{
		if (v.second->owner == state->getPlayer())
		{
			auto control = TransactionControl::createControl(
			    *state, StateRef<Vehicle>{state.get(), v.first}, leftIndex, rightIndex);
			if (control)
			{
				control->addCallback(FormEventType::ScrollBarChange, onScrollChange);
				control->addCallback(FormEventType::MouseMove, onHover);
				transactionControls[type].push_back(control);
			}
		}
	}
}

void TransactionScreen::populateControlsAgentEquipment()
{
	static const std::list<AEquipmentType::Type> agTypes = {
	    AEquipmentType::Type::Grenade, AEquipmentType::Type::Weapon,
	    // Ammo means everything else
	    AEquipmentType::Type::Ammo, AEquipmentType::Type::Armor, AEquipmentType::Type::Loot,
	};
	int leftIndex = getLeftIndex();
	int rightIndex = getRightIndex();
	for (auto &t : agTypes)
	{
		for (auto &ae : state->agent_equipment)
		{
			if (ae.second->bioStorage)
			{
				continue;
			}
			if (ae.second->type == AEquipmentType::Type::Ammo)
			{
				continue;
			}
			if (t == AEquipmentType::Type::Ammo)
			{
				if (std::find(agTypes.begin(), agTypes.end(), ae.second->type) != agTypes.end())
				{
					continue;
				}
			}
			else
			{
				if (ae.second->type != t)
				{
					continue;
				}
			}
			// Add equipment
			if (state->economy.find(ae.first) != state->economy.end())
			{
				auto control = TransactionControl::createControl(
				    *state, StateRef<AEquipmentType>{state.get(), ae.first}, leftIndex, rightIndex);
				if (control)
				{
					control->addCallback(FormEventType::ScrollBarChange, onScrollChange);
					control->addCallback(FormEventType::MouseMove, onHover);
					transactionControls[type].push_back(control);
				}
			}
			// Add ammo
			for (auto &ammo : ae.second->ammo_types)
			{
				if (state->economy.find(ammo.id) != state->economy.end())
				{
					auto controlAmmo =
					    TransactionControl::createControl(*state, ammo, leftIndex, rightIndex);
					if (controlAmmo)
					{
						controlAmmo->addCallback(FormEventType::ScrollBarChange, onScrollChange);
						controlAmmo->addCallback(FormEventType::MouseMove, onHover);
						transactionControls[type].push_back(controlAmmo);

						// Link to already existing
						for (auto &c : transactionControls[type])
						{
							if (c->itemId == ammo.id)
							{
								c->link(controlAmmo);
							}
						}
					}
				}
			}
		}
	}
}

void TransactionScreen::populateControlsVehicleEquipment()
{
	bool flying = type == Type::FlyingEquipment;
	auto otherType = flying ? Type::GroundEquipment : Type::FlyingEquipment;
	bool otherPopulated = transactionControls.find(otherType) != transactionControls.end();
	static const std::list<EquipmentSlotType> vehTypes = {EquipmentSlotType::VehicleWeapon,
	                                                      EquipmentSlotType::VehicleGeneral,
	                                                      EquipmentSlotType::VehicleEngine};
	int leftIndex = getLeftIndex();
	int rightIndex = getRightIndex();
	for (auto &t : vehTypes)
	{
		StateRef<VAmmoType> ammoType;
		for (auto &ve : state->vehicle_equipment)
		{
			if (ve.second->type != t)
			{
				continue;
			}
			if (flying &&
			    ve.second->users.find(VEquipmentType::User::Air) == ve.second->users.end())
			{
				continue;
			}
			if (!flying &&
			    ve.second->users.find(VEquipmentType::User::Ground) == ve.second->users.end())
			{
				continue;
			}
			if (state->economy.find(ve.first) != state->economy.end())
			{
				if (ammoType && ve.second->ammo_type != ammoType)
				{
					if (state->economy.find(ammoType.id) != state->economy.end())
					{
						auto controlAmmo = TransactionControl::createControl(*state, ammoType,
						                                                     leftIndex, rightIndex);
						if (controlAmmo)
						{
							controlAmmo->addCallback(FormEventType::ScrollBarChange,
							                         onScrollChange);
							controlAmmo->addCallback(FormEventType::MouseMove, onHover);
							transactionControls[type].push_back(controlAmmo);

							sp<TransactionControl> otherControlAmmo;
							if (otherPopulated)
							{
								for (auto &c : transactionControls[otherType])
								{
									if (c->itemId == ammoType.id)
									{
										otherControlAmmo = c;
										break;
									}
								}
							}
							if (!otherControlAmmo)
							{
								for (auto &c : transactionControls[type])
								{
									if (c->itemId == ammoType.id)
									{
										otherControlAmmo = c;
										break;
									}
								}
							}
							if (otherControlAmmo)
							{
								otherControlAmmo->link(controlAmmo);
							}
						}
					}
					ammoType = nullptr;
				}

				auto control = TransactionControl::createControl(
				    *state, StateRef<VEquipmentType>{state.get(), ve.first}, leftIndex, rightIndex);
				if (control)
				{
					control->addCallback(FormEventType::ScrollBarChange, onScrollChange);
					control->addCallback(FormEventType::MouseMove, onHover);
					transactionControls[type].push_back(control);

					sp<TransactionControl> otherControl;
					if (otherPopulated)
					{
						for (auto &c : transactionControls[otherType])
						{
							if (c->itemId == ve.first)
							{
								otherControl = c;
								break;
							}
						}
					}
					if (!otherControl)
					{
						for (auto &c : transactionControls[type])
						{
							if (c->itemId == ve.first)
							{
								otherControl = c;
								break;
							}
						}
					}
					if (otherControl)
					{
						otherControl->link(control);
					}
				}
				if (ve.second->ammo_type)
				{
					ammoType = ve.second->ammo_type;
				}
			}
		}
		if (ammoType)
		{
			if (state->economy.find(ammoType.id) != state->economy.end())
			{
				auto controlAmmo =
				    TransactionControl::createControl(*state, ammoType, leftIndex, rightIndex);
				if (controlAmmo)
				{
					controlAmmo->addCallback(FormEventType::ScrollBarChange, onScrollChange);
					controlAmmo->addCallback(FormEventType::MouseMove, onHover);
					transactionControls[type].push_back(controlAmmo);

					sp<TransactionControl> otherControlAmmo;
					if (otherPopulated)
					{
						for (auto &c : transactionControls[otherType])
						{
							if (c->itemId == ammoType.id)
							{
								otherControlAmmo = c;
								break;
							}
						}
					}
					if (!otherControlAmmo)
					{
						for (auto &c : transactionControls[type])
						{
							if (c->itemId == ammoType.id)
							{
								otherControlAmmo = c;
								break;
							}
						}
					}
					if (otherControlAmmo)
					{
						otherControlAmmo->link(controlAmmo);
					}
				}
			}
		}
	}
}

void TransactionScreen::populateControlsAlien()
{
	int leftIndex = getLeftIndex();
	int rightIndex = getRightIndex();
	for (auto &ae : state->agent_equipment)
	{
		if (!ae.second->bioStorage)
		{
			continue;
		}
		// Add alien
		if (state->economy.find(ae.first) != state->economy.end())
		{
			auto control = TransactionControl::createControl(
			    *state, StateRef<AEquipmentType>{state.get(), ae.first}, leftIndex, rightIndex);
			if (control)
			{
				control->addCallback(FormEventType::ScrollBarChange, onScrollChange);
				control->addCallback(FormEventType::MouseMove, onHover);
				transactionControls[type].push_back(control);
			}
		}
	}
}

void TransactionScreen::updateFormValues(bool queueHighlightUpdate)
{
	int leftIndex = getLeftIndex();
	int rightIndex = getRightIndex();

	// FIXME: UPDATE LQ DELTA
	lqDelta = 0;
	lq2Delta = 0;

	// Update storage
	cargoDelta = 0;
	cargo2Delta = 0;
	bioDelta = 0;
	bio2Delta = 0;
	moneyDelta = 0;

	std::set<sp<TransactionControl>> linkedControls;
	for (auto &l : transactionControls)
	{
		for (auto &c : l.second)
		{
			if (linkedControls.find(c) != linkedControls.end())
			{
				continue;
			}
			cargoDelta += c->getCargoDelta(leftIndex);
			bioDelta += c->getBioDelta(leftIndex);
			cargo2Delta += c->getCargoDelta(rightIndex);
			bio2Delta += c->getBioDelta(rightIndex);
			moneyDelta += c->getPriceDelta();
			for (auto &l : c->getLinked())
			{
				linkedControls.insert(l);
			}
		}
	}

	if (queueHighlightUpdate)
	{
		framesUntilHighlightUpdate = 30;
	}
}

void TransactionScreen::updateBaseHighlight()
{
	switch (viewHighlight)
	{
		case BaseGraphics::FacilityHighlight::Quarters:
		{
			auto facilityPic = form->findControlTyped<Graphic>("FACILITY_FIRST_PIC");
			facilityPic->setVisible(true);
			facilityPic->setImage(state->facility_types["FACILITYTYPE_LIVING_QUARTERS"]->sprite);
			form->findControlTyped<Graphic>("FACILITY_FIRST_BAR")->setVisible(true);
			int usage =
			    state->current_base->getUsage(*state, FacilityType::Capacity::Quarters, lqDelta);
			fillBaseBar(true, usage);
			auto facilityLabel = form->findControlTyped<Label>("FACILITY_FIRST_TEXT");
			facilityLabel->setVisible(true);
			facilityLabel->setText(format("%s%%", usage));
			break;
		}
		case BaseGraphics::FacilityHighlight::Stores:
		{
			auto facilityPic = form->findControlTyped<Graphic>("FACILITY_FIRST_PIC");
			facilityPic->setVisible(true);
			facilityPic->setImage(state->facility_types["FACILITYTYPE_STORES"]->sprite);
			form->findControlTyped<Graphic>("FACILITY_FIRST_BAR")->setVisible(true);
			int usage =
			    state->current_base->getUsage(*state, FacilityType::Capacity::Stores, cargoDelta);
			fillBaseBar(true, usage);
			auto facilityLabel = form->findControlTyped<Label>("FACILITY_FIRST_TEXT");
			facilityLabel->setVisible(true);
			facilityLabel->setText(format("%s%%", usage));
			break;
		}
		case BaseGraphics::FacilityHighlight::Aliens:
		{
			auto facilityPic = form->findControlTyped<Graphic>("FACILITY_FIRST_PIC");
			facilityPic->setVisible(true);
			facilityPic->setImage(state->facility_types["FACILITYTYPE_ALIEN_CONTAINMENT"]->sprite);
			form->findControlTyped<Graphic>("FACILITY_FIRST_BAR")->setVisible(true);
			int usage =
			    state->current_base->getUsage(*state, FacilityType::Capacity::Aliens, bioDelta);
			fillBaseBar(true, usage);
			auto facilityLabel = form->findControlTyped<Label>("FACILITY_FIRST_TEXT");
			facilityLabel->setVisible(true);
			facilityLabel->setText(format("%s%%", usage));
			break;
		}
		default:
		{
			form->findControlTyped<Graphic>("FACILITY_FIRST_PIC")->setVisible(false);
			form->findControlTyped<Graphic>("FACILITY_FIRST_BAR")->setVisible(false);
			form->findControlTyped<Graphic>("FACILITY_FIRST_FILL")->setVisible(false);
			form->findControlTyped<Label>("FACILITY_FIRST_TEXT")->setVisible(false);
			break;
		}
	}
}

void TransactionScreen::fillBaseBar(bool left, int percent)
{
	auto facilityBar = left ? form->findControlTyped<Graphic>("FACILITY_FIRST_FILL")
	                        : form->findControlTyped<Graphic>("FACILITY_SECOND_FILL");
	facilityBar->setVisible(true);

	auto progressImage = mksp<RGBImage>(facilityBar->Size);
	int redHeight = progressImage->size.y * std::min(100, percent) / 100;
	{
		RGBImageLock l(progressImage);
		for (int x = 0; x < 2; x++)
		{
			for (int y = 1; y <= progressImage->size.y; y++)
			{
				if (y <= redHeight)
				{
					l.set({x, progressImage->size.y - y}, {255, 0, 0, 255});
				}
			}
		}
	}
	facilityBar->setImage(progressImage);
}

void TransactionScreen::displayItem(sp<TransactionControl> control)
{
	if (control->itemType == TransactionControl::Type::AgentEquipmentBio ||
	    control->itemType == TransactionControl::Type::AgentEquipmentCargo)
	{
		formItemAgent->setVisible(true);
		formItemVehicle->setVisible(false);

		AEquipmentSheet(formItemAgent).display(state->agent_equipment[control->itemId]);
	}
	else
	{
		formItemVehicle->setVisible(true);
		formItemAgent->setVisible(false);

		if (control->itemType == TransactionControl::Type::VehicleType)
		{
			VehicleSheet(formItemVehicle).display(state->vehicle_types[control->itemId]);
		}
		else if (control->itemType == TransactionControl::Type::Vehicle)
		{
			VehicleSheet(formItemVehicle).display(state->vehicles[control->itemId]);
		}
		else if (control->itemType == TransactionControl::Type::VehicleEquipment)
		{
			VehicleSheet(formItemVehicle).display(state->vehicle_equipment[control->itemId]);
		}
		else // vehicle ammo & fuel don't display anything
		{
			formItemVehicle->setVisible(false);
		}
	}
}

bool TransactionScreen::isClosable() const
{
	// FIXME: Check for agent transfers
	std::set<sp<TransactionControl>> linkedControls;
	for (auto &l : transactionControls)
	{
		for (auto &c : l.second)
		{
			if (linkedControls.find(c) != linkedControls.end())
			{
				continue;
			}

			for (int i = 0; i < 8; i++)
			{
				if (c->initialStock[i] != c->currentStock[i])
				{
					return false;
				}
			}

			for (auto &l : c->getLinked())
			{
				linkedControls.insert(l);
			}
		}
	}

	return true;
}

void TransactionScreen::attemptCloseScreen()
{
	if (isClosable())
	{
		fw().stageQueueCommand({StageCmd::Command::POP});
	}
	else
	{
		fw().stageQueueCommand(
		    {StageCmd::Command::PUSH,
		     mksp<MessageBox>(confirmClosure, "", MessageBox::ButtonOptions::YesNoCancel,
		                      [this] { closeScreen(); },
		                      [] { // On clicking "No" Just exit without doing anything
			                      fw().stageQueueCommand({StageCmd::Command::POP});
			                  })});
	}
}

void TransactionScreen::initViewSecondBase()
{
	for (int i = 1; i <= 8; i++)
	{
		auto viewName = format("BUTTON_SECOND_BASE_%d", i);
		form->findControlTyped<GraphicButton>(viewName)->setVisible(false);
	}
	form->findControlTyped<Label>("TEXT_BUTTON_SECOND_BASE")->setVisible(false);
}

void TransactionScreen::begin()
{
	BaseStage::begin();

	initViewSecondBase();
}

void TransactionScreen::pause() {}

void TransactionScreen::resume()
{
	form->findControlTyped<Label>("TEXT_FUNDS")->setText(state->getPlayerBalance());
}

void TransactionScreen::finish() {}

void TransactionScreen::eventOccurred(Event *e)
{
	form->eventOccured(e);

	if (e->type() == EVENT_KEY_DOWN)
	{
		switch (e->keyboard().KeyCode)
		{
			case SDLK_ESCAPE:
			case SDLK_RETURN:
			case SDLK_SPACE:
				form->findControl("BUTTON_OK")->click();
				return;
		}
	}

	if (e->type() == EVENT_FORM_INTERACTION)
	{
		if (e->forms().EventFlag == FormEventType::ButtonClick)
		{
			if (e->forms().RaisedBy->Name == "BUTTON_OK")
			{
				attemptCloseScreen();
				return;
			}
		}
	}
}

void TransactionScreen::update()
{
	form->update();
	if (framesUntilHighlightUpdate > 0)
	{
		framesUntilHighlightUpdate--;
		if (framesUntilHighlightUpdate == 0)
		{
			updateBaseHighlight();
		}
	}
}

void TransactionScreen::render()
{
	fw().stageGetPrevious(this->shared_from_this())->render();

	textViewBaseStatic->setVisible(!textViewBase || !textViewBase->isVisible());

	form->render();
	BaseStage::render();
}

bool TransactionScreen::isTransition() { return false; }

void TransactionScreen::TransactionControl::initResources()
{
	bgLeft = fw().data->loadImage(format(
	    "PCK:xcom3/ufodata/newbut.pck:xcom3/ufodata/newbut.tab:%d:xcom3/ufodata/research.pcx", 45));
	bgRight = fw().data->loadImage(format(
	    "PCK:xcom3/ufodata/newbut.pck:xcom3/ufodata/newbut.tab:%d:xcom3/ufodata/research.pcx", 46));
	purchaseBoxIcon = fw().data->loadImage(format(
	    "PCK:xcom3/ufodata/newbut.pck:xcom3/ufodata/newbut.tab:%d:xcom3/ufodata/research.pcx", 47));
	purchaseXComIcon = fw().data->loadImage(format(
	    "PCK:xcom3/ufodata/newbut.pck:xcom3/ufodata/newbut.tab:%d:xcom3/ufodata/research.pcx", 48));
	purchaseArrow = fw().data->loadImage(format(
	    "PCK:xcom3/ufodata/newbut.pck:xcom3/ufodata/newbut.tab:%d:xcom3/ufodata/research.pcx", 52));
	alienContainedDetain = fw().data->loadImage(format(
	    "PCK:xcom3/ufodata/newbut.pck:xcom3/ufodata/newbut.tab:%d:xcom3/ufodata/research.pcx", 75));
	alienContainedKill = fw().data->loadImage(format(
	    "PCK:xcom3/ufodata/newbut.pck:xcom3/ufodata/newbut.tab:%d:xcom3/ufodata/research.pcx", 76));
	scrollLeft = fw().data->loadImage(format(
	    "PCK:xcom3/ufodata/newbut.pck:xcom3/ufodata/newbut.tab:%d:xcom3/ufodata/research.pcx", 53));
	scrollRight = fw().data->loadImage(format(
	    "PCK:xcom3/ufodata/newbut.pck:xcom3/ufodata/newbut.tab:%d:xcom3/ufodata/research.pcx", 54));
	transactionShade = fw().data->loadImage("city/transaction-shade.png");
	labelFont = ui().getFont("smalfont");

	resourcesInitialised = true;
}

void TransactionScreen::TransactionControl::setScrollbarValues()
{
	if (indexLeft == indexRight)
	{
		scrollBar->setMinimum(0);
		scrollBar->setMaximum(0);
		scrollBar->setValue(0);
	}
	else
	{
		scrollBar->setMinimum(0);
		scrollBar->setMaximum(currentStock[indexLeft] + currentStock[indexRight]);
		scrollBar->setValue(currentStock[indexRight]);
	}
	updateValues();
}

void TransactionScreen::TransactionControl::setIndexLeft(int index)
{
	indexLeft = index;
	setScrollbarValues();
}

void TransactionScreen::TransactionControl::setIndexRight(int index)
{
	indexRight = index;
	setScrollbarValues();
}

void TransactionScreen::TransactionControl::updateValues()
{
	if (scrollBar->getMaximum() != 0)
	{
		if (manufacturerHostile || manufacturerUnavailable)
		{
			if (indexLeft == 8)
			{
				if (scrollBar->getValue() > scrollBar->getMaximum() - initialStock[indexLeft])
				{
					scrollBar->setValue(initialStock[indexLeft]);
					auto message_box = mksp<MessageBox>(
					    manufacturer,
					    manufacturerHostile ? tr("Order canceled by the hostile manufacturer.")
					                        : tr("Manufacturer has no intact buildings in this "
					                             "city to deliver goods from."),
					    MessageBox::ButtonOptions::Ok);
					fw().stageQueueCommand({StageCmd::Command::PUSH, message_box});
					return;
				}
			}
			if (indexRight == 8)
			{
				if (scrollBar->getValue() < initialStock[indexRight])
				{
					scrollBar->setValue(initialStock[indexRight]);
					auto message_box = mksp<MessageBox>(
					    manufacturer,
					    manufacturerHostile ? tr("Order canceled by the hostile manufacturer.")
					                        : tr("Manufacturer has no intact buildings in this "
					                             "city to deliver goods from."),
					    MessageBox::ButtonOptions::Ok);
					fw().stageQueueCommand({StageCmd::Command::PUSH, message_box});
					return;
				}
			}
		}

		int newRight = scrollBar->getValue();
		int newLeft = scrollBar->getMaximum() - scrollBar->getValue();
		if (newRight != currentStock[indexRight] || newLeft != currentStock[indexLeft])
		{
			currentStock[indexRight] = newRight;
			currentStock[indexLeft] = newLeft;
			for (auto &c : linked)
			{
				c->suspendUpdates = true;
				c->currentStock[indexRight] = newRight;
				c->currentStock[indexLeft] = newLeft;
				c->scrollBar->setValue(scrollBar->getValue());
				c->updateValues();
				c->suspendUpdates = false;
			}
			if (!suspendUpdates)
			{
				this->pushFormEvent(FormEventType::ScrollBarChange, nullptr);
			}
		}
	}
	stockLeft->setText(format("%d", currentStock[indexLeft]));
	stockRight->setText(format("%d", currentStock[indexRight]));
	int curDeltaLeft = currentStock[indexLeft] - initialStock[indexLeft];
	int curDeltaRight = currentStock[indexRight] - initialStock[indexRight];
	deltaLeft->setText(format("%s%d", curDeltaLeft > 0 ? "+" : "", curDeltaLeft));
	deltaRight->setText(format("%s%d", curDeltaRight > 0 ? "+" : "", curDeltaRight));
	deltaLeft->setVisible(indexLeft != 8 && curDeltaLeft != 0);
	deltaRight->setVisible(indexRight != 8 && curDeltaRight != 0);
	setDirty();
}

void TransactionScreen::TransactionControl::link(sp<TransactionControl> control)
{
	for (auto &c : linked)
	{
		c->linked.push_back(control);
		control->linked.push_back(c);
	}
	linked.push_back(control);
	control->linked.push_back(std::static_pointer_cast<TransactionControl>(shared_from_this()));
}

const std::list<sp<TransactionScreen::TransactionControl>> &
TransactionScreen::TransactionControl::getLinked() const
{
	return linked;
}

sp<TransactionScreen::TransactionControl> TransactionScreen::TransactionControl::createControl(
    GameState &state, StateRef<AEquipmentType> agentEquipmentType, int indexLeft, int indexRight)
{
	bool isBio = agentEquipmentType->bioStorage;
	int price = 0;
	int storeSpace = agentEquipmentType->store_space;
	std::vector<int> initialStock;
	bool hasStock = false;
	// Fill out stock
	{
		initialStock.resize(9);
		int baseIndex = 0;
		for (auto &b : state.player_bases)
		{
			int divisor = (agentEquipmentType->type == AEquipmentType::Type::Ammo && !isBio)
			                  ? agentEquipmentType->max_ammo
			                  : 1;
			initialStock[baseIndex] =
			    isBio ? b.second->inventoryBioEquipment[agentEquipmentType.id]
			          : b.second->inventoryAgentEquipment[agentEquipmentType.id];
			initialStock[baseIndex] = (initialStock[baseIndex] + divisor - 1) / divisor;
			if (initialStock[baseIndex] > 0)
			{
				hasStock = true;
			}
			baseIndex++;
		}
	}
	// Fill out economy data
	if (!agentEquipmentType->bioStorage)
	{
		bool economyUnavailable = true;
		if (state.economy.find(agentEquipmentType.id) != state.economy.end())
		{
			auto &economy = state.economy[agentEquipmentType.id];
			int week = state.gameTime.getWeek();
			initialStock[8] = economy.currentStock;
			price = economy.currentPrice;
			economyUnavailable = economy.weekAvailable == 0 || economy.weekAvailable > week ||
			                     agentEquipmentType->artifact;
		}
		if (!hasStock && economyUnavailable)
		{
			return nullptr;
		}
	}
	else
	{
		if (!hasStock)
		{
			return nullptr;
		}
	}
	bool isAmmo = agentEquipmentType->type == AEquipmentType::Type::Ammo;
	auto name = agentEquipmentType->name;
	auto manufacturer =
	    agentEquipmentType->bioStorage ? "" : agentEquipmentType->manufacturer->name;
	auto canBuy = agentEquipmentType->manufacturer->canPurchaseFrom(
	    state, state.current_base->building, false);
	bool manufacturerHostile = canBuy == Organisation::PurchaseResult::OrgHostile;
	bool manufacturerUnavailable = canBuy == Organisation::PurchaseResult::OrgHasNoBuildings;
	// If we create a non-purchase control we never become one so clear the values
	if (indexLeft != 8 && indexRight != 8)
	{
		manufacturer = "";
		price = 0;
	}
	return createControl(
	    agentEquipmentType.id, isBio ? Type::AgentEquipmentBio : Type::AgentEquipmentCargo,
	    agentEquipmentType->name, manufacturer, isAmmo, isBio, manufacturerHostile,
	    manufacturerUnavailable, price, storeSpace, initialStock, indexLeft, indexRight);
}

sp<TransactionScreen::TransactionControl> TransactionScreen::TransactionControl::createControl(
    GameState &state, StateRef<VEquipmentType> vehicleEquipmentType, int indexLeft, int indexRight)
{
	bool isBio = false;
	int price = 0;
	int storeSpace = vehicleEquipmentType->store_space;
	std::vector<int> initialStock;
	bool hasStock = false;
	// Fill out stock
	{
		initialStock.resize(9);
		int baseIndex = 0;
		for (auto &b : state.player_bases)
		{
			initialStock[baseIndex] = b.second->inventoryVehicleEquipment[vehicleEquipmentType.id];
			if (initialStock[baseIndex] > 0)
			{
				hasStock = true;
			}
			baseIndex++;
		}
	}
	// Fill out economy data
	{
		bool economyUnavailable = true;
		if (state.economy.find(vehicleEquipmentType.id) != state.economy.end())
		{
			auto &economy = state.economy[vehicleEquipmentType.id];
			int week = state.gameTime.getWeek();
			initialStock[8] = economy.currentStock;
			price = economy.currentPrice;
			economyUnavailable = economy.weekAvailable == 0 || economy.weekAvailable > week;
		}
		if (!hasStock && economyUnavailable)
		{
			return nullptr;
		}
	}

	bool isAmmo = false;
	auto name = vehicleEquipmentType->name;
	auto manufacturer = vehicleEquipmentType->manufacturer->name;
	// Expecting all bases to be in one city
	auto canBuy = vehicleEquipmentType->manufacturer->canPurchaseFrom(
	    state, state.current_base->building, false);
	bool manufacturerHostile = canBuy == Organisation::PurchaseResult::OrgHostile;
	bool manufacturerUnavailable = canBuy == Organisation::PurchaseResult::OrgHasNoBuildings;
	// If we create a non-purchase control we never become one so clear the values
	if (indexLeft != 8 && indexRight != 8)
	{
		manufacturer = "";
		price = 0;
	}
	return createControl(vehicleEquipmentType.id, Type::VehicleEquipment,
	                     vehicleEquipmentType->name, manufacturer, isAmmo, isBio,
	                     manufacturerHostile, manufacturerUnavailable, price, storeSpace,
	                     initialStock, indexLeft, indexRight);
}

sp<TransactionScreen::TransactionControl> TransactionScreen::TransactionControl::createControl(
    GameState &state, StateRef<VAmmoType> vehicleAmmoType, int indexLeft, int indexRight)
{
	bool isBio = false;
	int price = 0;
	int storeSpace = vehicleAmmoType->store_space;
	std::vector<int> initialStock;
	bool hasStock = false;
	// Fill out stock
	{
		initialStock.resize(9);
		int baseIndex = 0;
		for (auto &b : state.player_bases)
		{
			initialStock[baseIndex] = b.second->inventoryVehicleAmmo[vehicleAmmoType.id];
			if (initialStock[baseIndex] > 0)
			{
				hasStock = true;
			}
			baseIndex++;
		}
	}
	// Fill out economy data
	{
		bool economyUnavailable = true;
		if (state.economy.find(vehicleAmmoType.id) != state.economy.end())
		{
			auto &economy = state.economy[vehicleAmmoType.id];
			int week = state.gameTime.getWeek();
			initialStock[8] = economy.currentStock;
			price = economy.currentPrice;
			economyUnavailable = economy.weekAvailable == 0 || economy.weekAvailable > week;
		}
		if (!hasStock && economyUnavailable)
		{
			return nullptr;
		}
	}

	bool isAmmo = true;
	auto name = vehicleAmmoType->name;
	auto manufacturer = vehicleAmmoType->manufacturer->name;
	// Expecting all bases to be in one city
	auto canBuy =
	    vehicleAmmoType->manufacturer->canPurchaseFrom(state, state.current_base->building, false);
	bool manufacturerHostile = canBuy == Organisation::PurchaseResult::OrgHostile;
	bool manufacturerUnavailable = canBuy == Organisation::PurchaseResult::OrgHasNoBuildings;
	// If we create a non-purchase control we never become one so clear the values
	if (indexLeft != 8 && indexRight != 8)
	{
		manufacturer = "";
		price = 0;
	}
	return createControl(vehicleAmmoType.id, Type::VehicleAmmo, vehicleAmmoType->name, manufacturer,
	                     isAmmo, isBio, manufacturerHostile, manufacturerUnavailable, price,
	                     storeSpace, initialStock, indexLeft, indexRight);
}

sp<TransactionScreen::TransactionControl> TransactionScreen::TransactionControl::createControl(
    GameState &state, StateRef<VehicleType> vehicleType, int indexLeft, int indexRight)
{
	// No sense in transfer
	if (indexLeft != 8 && indexRight != 8)
	{
		return nullptr;
	}
	bool isBio = false;
	int price = 0;
	int storeSpace = 0;
	std::vector<int> initialStock;
	// Fill out stock
	{
		initialStock.resize(9);
		// Stock of vehicle types always zero
	}
	// Fill out economy data
	{
		bool economyUnavailable = true;
		if (state.economy.find(vehicleType.id) != state.economy.end())
		{
			auto &economy = state.economy[vehicleType.id];
			int week = state.gameTime.getWeek();
			initialStock[8] = economy.currentStock;
			price = economy.currentPrice;
			economyUnavailable = economy.weekAvailable == 0 || economy.weekAvailable > week;
		}
		if (economyUnavailable)
		{
			return nullptr;
		}
	}

	bool isAmmo = false;
	auto name = vehicleType->name;
	auto manufacturer = vehicleType->manufacturer->name;
	// Expecting all bases to be in one city
	auto canBuy =
	    vehicleType->manufacturer->canPurchaseFrom(state, state.current_base->building, true);
	bool manufacturerHostile = canBuy == Organisation::PurchaseResult::OrgHostile;
	bool manufacturerUnavailable = canBuy == Organisation::PurchaseResult::OrgHasNoBuildings;
	// If we create a non-purchase control we never become one so clear the values
	if (indexLeft != 8 && indexRight != 8)
	{
		manufacturer = "";
		price = 0;
	}
	return createControl(vehicleType.id, Type::VehicleType, vehicleType->name, manufacturer, isAmmo,
	                     isBio, manufacturerHostile, manufacturerUnavailable, price, storeSpace,
	                     initialStock, indexLeft, indexRight);
}

sp<TransactionScreen::TransactionControl>
TransactionScreen::TransactionControl::createControl(GameState &state, StateRef<Vehicle> vehicle,
                                                     int indexLeft, int indexRight)
{
	// Only parked vehicles can be sold
	if (!vehicle->currentBuilding)
	{
		return nullptr;
	}
	bool isBio = false;
	int price = 0;
	int storeSpace = 0;
	std::vector<int> initialStock;
	// Fill out stock
	{
		initialStock.resize(9);
		// Stock of vehicle types always zero on all bases except where it belongs
		int baseIndex = 0;
		for (auto &b : state.player_bases)
		{
			if (b.first == vehicle->homeBuilding->base.id)
			{
				initialStock[baseIndex] = 1;
				break;
			}
			baseIndex++;
		}
	}
	// Fill out economy data
	{
		bool economyUnavailable = true;
		if (state.economy.find(vehicle->type.id) != state.economy.end())
		{
			auto &economy = state.economy[vehicle->type.id];
			int week = state.gameTime.getWeek();
			price = economy.currentPrice;
			economyUnavailable = economy.weekAvailable == 0 || economy.weekAvailable > week;
		}
		if (economyUnavailable)
		{
			// Nothing, we can still sell it for parts or transfer!
		}
	}
	LogWarning("Vehicle type %s starting price %d", vehicle->type.id, price);
	// Add price of ammo and equipment
	for (auto &e : vehicle->equipment)
	{
		if (state.economy.find(e->type.id) != state.economy.end())
		{
			price += state.economy[e->type.id].currentPrice;
			if (e->ammo > 0 && state.economy.find(e->type->ammo_type.id) != state.economy.end())
			{
				price += e->ammo * state.economy[e->type->ammo_type.id].currentPrice;
			}
			LogWarning("Vehicle type %s price increased to %d after counting %s", vehicle->type.id,
			           price, e->type.id);
		}
	}
	// Subtract price of default equipment
	for (auto &e : vehicle->type->initial_equipment_list)
	{
		if (state.economy.find(e.second.id) != state.economy.end())
		{
			price -= state.economy[e.second.id].currentPrice;
			LogWarning("Vehicle type %s price decreased to %d after counting %s", vehicle->type.id,
			           price, e.second.id);
		}
	}
	LogWarning("Vehicle type %s final price %d", vehicle->type.id, price);

	bool isAmmo = false;
	auto name = vehicle->name;
	auto manufacturer = vehicle->type->manufacturer->name;
	bool manufacturerHostile = false;
	bool manufacturerUnavailable = false;
	// If we create a non-purchase control we never become one so clear the values
	if (indexLeft != 8 && indexRight != 8)
	{
		manufacturer = "";
		price = 0;
	}
	return createControl(vehicle.id, Type::Vehicle, vehicle->name, manufacturer, isAmmo, isBio,
	                     manufacturerHostile, manufacturerUnavailable, price, storeSpace,
	                     initialStock, indexLeft, indexRight);
}

sp<TransactionScreen::TransactionControl> TransactionScreen::TransactionControl::createControl(
    UString id, Type type, UString name, UString manufacturer, bool isAmmo, bool isBio,
    bool manufacturerHostile, bool manufacturerUnavailable, int price, int storeSpace,
    std::vector<int> &initialStock, int indexLeft, int indexRight)
{
	auto control = mksp<TransactionControl>();
	control->itemId = id;
	control->itemType = type;
	control->price = price;
	control->storeSpace = storeSpace;
	control->initialStock = initialStock;
	control->currentStock = initialStock;
	control->indexLeft = indexLeft;
	control->indexRight = indexRight;
	control->isAmmo = isAmmo;
	control->isBio = isBio;
	control->manufacturer = manufacturer;
	control->manufacturerHostile = manufacturerHostile;
	control->manufacturerUnavailable = manufacturerUnavailable;

	// Setup vars
	control->Size = Vec2<int>{173 + 178 - 2, 47};

	// Setup resources

	if (!resourcesInitialised)
	{
		initResources();
	}

	// Add controls

	// Name
	if (name.length() > 0)
	{
		auto label = control->createChild<Label>(name, labelFont);
		label->Location = {isAmmo ? 32 : 11, 3};
		label->Size = {256, 16};
		label->TextHAlign = HorizontalAlignment::Left;
		label->TextVAlign = VerticalAlignment::Centre;
	}
	// Manufacturer
	// FIXME: When we have color instead of asterisk color hostile manufacturer's name in red or
	// something?
	if (manufacturer.length() > 0)
	{
		auto label =
		    control->createChild<Label>(format("%s%s%s", manufacturerHostile ? "*" : "",
		                                       manufacturerUnavailable ? "X" : "", manufacturer),
		                                labelFont);
		label->Location = {34, 3};
		label->Size = {256, 16};
		label->TextHAlign = HorizontalAlignment::Right;
		label->TextVAlign = VerticalAlignment::Centre;
	}
	// Price
	if (price != 0)
	{
		auto label = control->createChild<Label>(format("$%d", price), labelFont);
		label->Location = {290, 3};
		label->Size = {47, 16};
		label->TextHAlign = HorizontalAlignment::Right;
		label->TextVAlign = VerticalAlignment::Centre;
	}
	// Stock (values set in updateValues)
	control->stockLeft = control->createChild<Label>("", labelFont);
	control->stockLeft->Location = {11, 26};
	control->stockLeft->Size = {32, 14};
	control->stockLeft->TextHAlign = HorizontalAlignment::Right;
	control->stockLeft->TextVAlign = VerticalAlignment::Centre;
	control->stockRight = control->createChild<Label>("", labelFont);
	control->stockRight->Location = {303, 26};
	control->stockRight->Size = {32, 14};
	control->stockRight->TextHAlign = HorizontalAlignment::Right;
	control->stockRight->TextVAlign = VerticalAlignment::Centre;
	// Delta (values set in updateValues)
	control->deltaLeft = control->createChild<Label>("", labelFont);
	control->deltaLeft->Location = {50, 26};
	control->deltaLeft->Size = {32, 14};
	control->deltaLeft->TextHAlign = HorizontalAlignment::Right;
	control->deltaLeft->TextVAlign = VerticalAlignment::Centre;
	control->deltaRight = control->createChild<Label>("", labelFont);
	control->deltaRight->Location = {264, 26};
	control->deltaRight->Size = {30, 14};
	control->deltaRight->TextHAlign = HorizontalAlignment::Right;
	control->deltaRight->TextVAlign = VerticalAlignment::Centre;
	// ScrollBar
	control->scrollBar = control->createChild<ScrollBar>();
	control->scrollBar->Location = {102, 24};
	control->scrollBar->Size = {147, 20};
	control->scrollBar->setMinimum(0);
	control->scrollBar->setMaximum(0);
	// ScrollBar buttons
	auto buttonScrollLeft = control->createChild<GraphicButton>(nullptr, scrollLeft);
	buttonScrollLeft->Size = scrollLeft->size;
	buttonScrollLeft->Location = {87, 24};
	buttonScrollLeft->ScrollBarPrev = control->scrollBar;
	auto buttonScrollRight = control->createChild<GraphicButton>(nullptr, scrollRight);
	buttonScrollRight->Size = scrollRight->size;
	buttonScrollRight->Location = {247, 24};
	buttonScrollRight->ScrollBarNext = control->scrollBar;
	// Callback
	control->setupCallbacks();
	// Finally set the values
	control->setScrollbarValues();

	return control;
}

void TransactionScreen::TransactionControl::setupCallbacks()
{
	std::function<void(FormsEvent * e)> onScrollChange = [this](FormsEvent *) {
		if (!this->suspendUpdates)
		{
			this->updateValues();
		}
	};
	scrollBar->addCallback(FormEventType::ScrollBarChange, onScrollChange);
}

int TransactionScreen::TransactionControl::getCargoDelta(int index) const
{
	return isBio ? 0 : (currentStock[index] - initialStock[index]) * storeSpace;
}

int TransactionScreen::TransactionControl::getBioDelta(int index) const
{
	return !isBio ? 0 : (currentStock[index] - initialStock[index]) * storeSpace;
}

int TransactionScreen::TransactionControl::getPriceDelta() const
{
	int delta = 0;
	for (int i = 0; i < 8; i++)
	{
		delta -= (currentStock[i] - initialStock[i]) * price;
	}
	return delta;
}

void TransactionScreen::TransactionControl::onRender()
{
	Control::onRender();

	static Vec2<int> bgLeftPos = {0, 2};
	static Vec2<int> bgRightPos = {172, 2};
	static Vec2<int> ammoPos = {4, 2};
	static Vec2<int> iconLeftPos = {58, 24};
	static Vec2<int> iconRightPos = {270, 24};
	static Vec2<int> iconSize = {22, 20};

	// Draw BG
	fw().renderer->draw(bgLeft, bgLeftPos);
	fw().renderer->draw(bgRight, bgRightPos);
	// Draw Ammo Arrow
	if (isAmmo)
	{
		fw().renderer->draw(purchaseArrow, ammoPos);
	}
	// Draw Icons
	if (!deltaLeft->isVisible())
	{
		sp<Image> icon;
		if (isBio)
		{
			icon = indexLeft == 8 ? alienContainedKill : alienContainedDetain;
		}
		else
		{
			icon = indexLeft == 8 ? purchaseBoxIcon : purchaseXComIcon;
		}
		auto iconPos = iconLeftPos + (iconSize - (Vec2<int>)icon->size) / 2;
		fw().renderer->draw(icon, iconPos);
	}
	if (!deltaRight->isVisible())
	{
		sp<Image> icon;
		if (isBio)
		{
			icon = indexRight == 8 ? alienContainedKill : alienContainedDetain;
		}
		else
		{
			icon = indexRight == 8 ? purchaseBoxIcon : purchaseXComIcon;
		}
		auto iconPos = iconRightPos + (iconSize - (Vec2<int>)icon->size) / 2;
		fw().renderer->draw(icon, iconPos);
	}
}

void TransactionScreen::TransactionControl::postRender()
{
	Control::postRender();

	// Draw shade if inactive
	static Vec2<int> shadePos = {0, 0};
	if (indexLeft == indexRight || (currentStock[indexLeft] == 0 && currentStock[indexRight] == 0))
	{
		fw().renderer->draw(transactionShade, shadePos);
	}
}

void TransactionScreen::TransactionControl::unloadResources()
{
	bgLeft.reset();
	bgRight.reset();

	purchaseBoxIcon.reset();
	purchaseXComIcon.reset();
	purchaseArrow.reset();

	alienContainedDetain.reset();
	alienContainedKill.reset();

	scrollLeft.reset();
	scrollRight.reset();

	transactionShade.reset();

	Control::unloadResources();
}

}; // namespace OpenApoc
