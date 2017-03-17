#include "framework/data.h"
#include "framework/framework.h"
#include "framework/palette.h"
#include "game/state/agent.h"
#include "game/state/battle/battleunitanimationpack.h"
#include "game/state/gamestate.h"
#include "tools/extractors/common/animation.h"
#include "tools/extractors/extractors.h"

namespace OpenApoc
{

	void extractAnimationPackPopperInternal(sp<BattleUnitAnimationPack> p, int x, int y, const InitialGameStateExtractor &e)
	{
		// Units per 100 frames
		static const int wFrames = 300; // Walk
		static const int rFrames = 300; // Run

		// Standing state: 
		p->standart_animations[{ItemWieldMode::None, HandState::AtEase, MovementState::None,
			BodyState::Standing}][{x, y}] =
			e.makeUpAnimationEntry(48, 2, 48, 1, 1, { x, y }, { 0,0 });

		// Downed state: 
		p->standart_animations[{ItemWieldMode::None, HandState::AtEase, MovementState::None,
			BodyState::Downed}][{x, y}] =
			e.makeUpAnimationEntry(64, 1, 0, 0, 1, { x, y }, { 0,0 });
		
		// Moving state normal: 
		p->standart_animations[{ItemWieldMode::None, HandState::AtEase, MovementState::Normal,
			BodyState::Standing}][{x, y}] =
			e.makeUpAnimationEntry(0, 6, 0, 6, 1, { x,y }, { 0,0 }, wFrames);
		
		// Moving state running: 
		p->standart_animations[{ItemWieldMode::None, HandState::AtEase, MovementState::Running,
			BodyState::Standing}][{x, y}] =
			e.makeUpAnimationEntry(0, 6, 0, 6, 1, { x,y }, { 0,0 }, rFrames);
	}

	void InitialGameStateExtractor::extractAnimationPackPopper(
		sp<BattleUnitAnimationPack> p) const
	{
		for (int x = -1; x <= 1; x++)
		{
			for (int y = -1; y <= 1; y++)
			{
				// 0, 0 facing does not exist
				if (x == 0 && y == 0)
					continue;

				extractAnimationPackPopperInternal(p, x, y, *this);
			}
		}
	}
}