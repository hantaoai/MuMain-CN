#pragma once

#include "MuHelperData.h"

namespace MUHelper
{
	// Drives the online MU Helper "town run": when potions run low, the backpack
	// silts up, or the Elf Soldier buff wears off, the helper warps to Lorencia,
	// walks to the relevant NPC (vault keeper / potion girl / elf soldier),
	// performs the interaction, then warps back to the remembered hunting spot
	// and resumes. While Active the normal combat/pickup pipeline is skipped and
	// the safe-zone kill-switch is bypassed.
	class TownRun
	{
	public:
		enum State
		{
			Idle = 0,
			WarpingToTown,
			WalkingToNpc,
			Interacting,
			WarpingBack,
			WalkingHome,
		};

		enum Errand
		{
			ErrandNone = 0,
			ErrandBuff,
			ErrandBuy,
			ErrandStore,
			ErrandCount,
		};

		TownRun() = default;

		bool Active = false;

		// Returns true while the run owns the tick (caller skips combat/pickup).
		bool Update();

		void Reset();

		// Kick off a player-requested AFK run: warp to town, grab the Elf Soldier buff,
		// stock up on potions, then warp back to the current map and walk to (destX,destY).
		// On arrival the helper is started automatically. The destination need not be
		// the character's current position - this is what the "recommended AFK spots"
		// window uses to send the player to a chosen hunting spot.
		void RequestAfkRun(int destX, int destY);

	private:
		enum
		{
			BUFF_BIT = 1 << 0,
			BUY_BIT = 1 << 1,
			STORE_BIT = 1 << 2,
		};

		void EvaluateTriggers();
		void BeginRun(int errands);
		void ResetRunState();
		void FinishRun();
		void Abort(const wchar_t* reason);
		void AdvanceErrand();
		void WalkTo(int tx, int ty);

		State m_state = Idle;
		int m_errandMask = 0;
		Errand m_currentErrand = ErrandNone;
		int m_subStage = 0;       // per-interaction progress
		int m_waitTicks = 0;      // ticks spent waiting in the current sub-stage
		int m_stageTicks = 0;     // ticks spent in the current state
		int m_buyTicks = 0;       // pacing for buy packets

		int m_originMap = 0;
		POINT m_originPos = { 0, 0 };

		int m_npcKey = -1;
		POINT m_npcPos = { 0, 0 };

		int m_buffCooldownTicks = 0;  // back-off after a failed/rejected buff request
		bool m_startHelperOnArrival = false;  // RequestAfkRun: auto-Start() when walk-home finishes
	};
}
