#include "stdafx.h"

#include <cmath>
#include <cstdlib>

#include "Engine/AI/ZzzAI.h"
#include "Engine/Object/ZzzCharacter.h"
#include "Engine/Object/ZzzInterface.h"
#include "Engine/Object/ZzzInventory.h"
#include "Engine/Object/ZzzInfomation.h"
#include "UI/NewUI/NewUISystem.h"
#include "UI/NewUI/NPCs/NewUINPCShop.h"
#include "UI/NewUI/Inventory/NewUIStorageInventory.h"
#include "UI/NewUI/Inventory/NewUIMyInventory.h"
#include "World/MapInfra/MapManager.h"
#include "Network/MoveCommandData.h"
#include "Network/Server/WSclient.h"
#include "Dotnet/PacketFunctions_CommonEnums.h"
#include "Core/Utilities/_GlobalFunctions.h"
#include "Core/Utilities/Log/muConsoleDebug.h"

#include "TownRun.h"
#include "MuHelper.h"  // g_MuHelper (AFK arrival callback)

using namespace SEASON3B;

// TargetX / TargetY are file-scope globals defined in ZzzInterface.cpp that drive
// the movement mover. Declared outside any namespace so they bind to ::TargetX.
extern int TargetX;
extern int TargetY;

namespace MUHelper
{
	// The MU Helper timer fires every 250 ms, so four ticks make one second.
	constexpr int TICKS_PER_SECOND = 4;

	constexpr int WARP_WAIT_TICKS = 10 * TICKS_PER_SECOND;
	constexpr int WALK_WAIT_TICKS = 15 * TICKS_PER_SECOND;
	constexpr int DIALOG_WAIT_TICKS = 6 * TICKS_PER_SECOND;
	constexpr int BUY_PACE_TICKS = 4;          // one purchase per second
	constexpr int STORE_PACE_TICKS = 2;        // one vault move every 500 ms
	constexpr int REPATH_TICKS = 8;

	constexpr int POTION_LOW_CHARGES = 30;
	constexpr int POTION_TARGET_CHARGES = 60;
	constexpr int MAX_PURCHASES = 30;
	constexpr int FREE_SLOT_PRESSURE = 8;
	constexpr int BUFF_RETRY_TICKS = 5 * 60 * TICKS_PER_SECOND;

	// Movereq index of Lorencia (the town we run errands in).
	constexpr int LORENCIA_MOVEREQ_INDEX = 2;

	// NPC monster numbers we walk to.
	constexpr int MONSTER_ID_VAULT = 240;     // Baz the Vault Keeper (Lorencia)
	constexpr int MONSTER_ID_POTION_LALA = 242;  // Elf Lala (Lorencia potion girl)
	constexpr int MONSTER_ID_POTION_AMY = 253;   // Potion Girl Amy (Devias)
	constexpr int MONSTER_ID_BUFF = 257;      // Elf Soldier

	namespace
	{
		int ManhattanDist(int ax, int ay, int bx, int by)
		{
			return (int)(std::abs(ax - bx) + std::abs(ay - by));
		}

		// WorldActive (ENUM_WORLD) -> movereq index used to warp back. Returns -1
		// for maps that cannot be warped back to (the run then gives up).
		int MapToMovereqIndex(int world)
		{
			switch (world)
			{
			case WD_0LORENCIA: return 2;
			case WD_1DUNGEON: return 8;
			case WD_2DEVIAS: return 4;
			case WD_3NORIA: return 3;
			case WD_4LOSTTOWER: return 14;
			case WD_7ATLANSE: return 11;
			case WD_8TARKAN: return 21;
			case WD_10HEAVEN: return 23;
			case WD_33AIDA: return 25;
			case WD_64DUELARENA: return 1;
			default: break;
			}

			// Newer maps (Kanturu, Swamp of Peace, Ice City, Raklion/Karutan, Vulcanus):
			// resolve at runtime by matching the map name against the loaded movereq
			// table's main map name instead of hardcoding movereq indexes.
			const wchar_t* mapName = gMapManager.GetMapName(world);
			if (mapName == nullptr || mapName[0] == L'\0')
			{
				return -1;
			}

			const auto& list = CMoveCommandData::GetInstance()->GetMoveCommandDatalist();
			for (const auto* info : list)
			{
				if (info == nullptr || !info->_bCanMove)
				{
					continue;
				}
				if (_wcsicmp(info->_ReqInfo.szMainMapName, mapName) == 0)
				{
					return info->_ReqInfo.index;
				}
			}
			return -1;
		}

		int CountPotionCharges(CNewUIInventoryCtrl* ctrl, short typeFirst, short typeLast)
		{
			int charges = 0;
			for (short t = typeFirst; t <= typeLast; ++t)
			{
				charges += ctrl->GetItemCount(t);
			}
			return charges;
		}

		// Picks the nearest live NPC of one of the accepted monster numbers.
		bool FindNearestNpc(const int* ids, int idCount, int& outKey, POINT& outPos)
		{
			int bestDist = INT_MAX;
			bool found = false;

			for (int i = 0; i < MAX_CHARACTERS_CLIENT; ++i)
			{
				CHARACTER* c = &CharactersClient[i];
				if (!c->Object.Live || c->Object.Kind != KIND_NPC)
				{
					continue;
				}

				bool matches = false;
				for (int j = 0; j < idCount; ++j)
				{
					if ((int)c->MonsterIndex == ids[j])
					{
						matches = true;
						break;
					}
				}
				if (!matches)
				{
					continue;
				}

				int dist = ManhattanDist(Hero->PositionX, Hero->PositionY, c->PositionX, c->PositionY);
				if (dist < bestDist)
				{
					bestDist = dist;
					outKey = c->Key;
					outPos = { c->PositionX, c->PositionY };
					found = true;
				}
			}

			return found;
		}
	}

	void TownRun::Reset()
	{
		m_state = Idle;
		Active = false;
		m_errandMask = 0;
		m_currentErrand = ErrandNone;
		m_subStage = 0;
		m_waitTicks = 0;
		m_stageTicks = 0;
		m_buyTicks = 0;
		m_npcKey = -1;
		m_startHelperOnArrival = false;
	}

	void TownRun::ResetRunState()
	{
		m_stageTicks = 0;
		m_subStage = 0;
		m_waitTicks = 0;
		m_buyTicks = 0;
		m_npcKey = -1;
		m_currentErrand = ErrandNone;
	}

	void TownRun::Abort(const wchar_t* reason)
	{
		g_ConsoleDebug->Write(MCD_NORMAL, L"[MU Helper] Town run aborted: %s", reason);

		// Best-effort cleanup of any dialog we may have left open.
		SocketClient->ToGameServer()->SendCloseNpcRequest();
		SocketClient->ToGameServer()->SendVaultClosed();

		Reset();
	}

	void TownRun::BeginRun(int errands)
	{
		m_errandMask = errands;
		m_originMap = gMapManager.WorldActive;
		m_originPos = { Hero->PositionX, Hero->PositionY };
		ResetRunState();
		m_startHelperOnArrival = false;
		Active = true;
		m_state = WarpingToTown;

		g_ConsoleDebug->Write(MCD_NORMAL, L"[MU Helper] Town run started (buff=%d buy=%d store=%d).",
			(errands & BUFF_BIT) != 0, (errands & BUY_BIT) != 0, (errands & STORE_BIT) != 0);
	}

	void TownRun::RequestAfkRun(int destX, int destY)
	{
		// Buff + potions only (no vault): the requested AFK sequence.
		m_errandMask = BUFF_BIT | BUY_BIT;
		// Preset the "return" point to the chosen hunting spot. The existing
		// WarpingBack/WalkingHome legs then deliver the character there instead of
		// back to where they were standing.
		m_originMap = gMapManager.WorldActive;
		m_originPos = { destX, destY };
		ResetRunState();
		m_startHelperOnArrival = true;
		Active = true;
		m_state = WarpingToTown;

		g_ConsoleDebug->Write(MCD_NORMAL,
			L"[MU Helper] AFK spot run started -> map %d (%d,%d), buff+buy.", m_originMap, destX, destY);
	}

	void TownRun::FinishRun()
	{
		bool startHelper = m_startHelperOnArrival;
		Hero->Movement = 0;
		g_ConsoleDebug->Write(MCD_NORMAL,
			startHelper ? L"[MU Helper] AFK spot reached; starting helper."
			            : L"[MU Helper] Town run complete, resuming hunt.");
		Reset();
		if (startHelper)
		{
			g_MuHelper.OnAfkArrived();
		}
	}

	void TownRun::EvaluateTriggers()
	{
		const ConfigData& cfg = g_MuHelper.GetConfig();

		int errands = ErrandNone;

		// A recently rejected buff (e.g. level-gated) backs off only the buff errand;
		// buy/store triggers must still be evaluated independently.
		if (cfg.bAutoNpcBuff && m_buffCooldownTicks == 0 &&
			!g_isCharacterBuff((&Hero->Object), eBuff_HelpNpc))
		{
			errands |= BUFF_BIT;
		}

		if (cfg.bAutoBuyPotions)
		{
			CNewUIInventoryCtrl* inv = g_pMyInventory->GetInventoryCtrl();
			int heal = CountPotionCharges(inv, (short)(ITEM_POTION + 1), (short)(ITEM_POTION + 3));
			int mana = CountPotionCharges(inv, (short)(ITEM_POTION + 4), (short)(ITEM_POTION + 6));
			if (heal < POTION_LOW_CHARGES || mana < POTION_LOW_CHARGES)
			{
				errands |= BUY_BIT;
			}
		}

		if (cfg.bAutoStoreVault &&
			g_pMyInventory->GetInventoryCtrl()->GetEmptySlotCount() < FREE_SLOT_PRESSURE)
		{
			errands |= STORE_BIT;
		}

		if (errands != ErrandNone)
		{
			BeginRun(errands);
		}
	}

	void TownRun::WalkTo(int tx, int ty)
	{
		Hero->MovementType = MOVEMENT_MOVE;
		TargetX = tx;
		TargetY = ty;

		if (!CheckTile(Hero, &Hero->Object, 1.5f))
		{
			if (PathFinding2(Hero->PositionX, Hero->PositionY, tx, ty, &Hero->Path))
			{
				SendMove(Hero, &Hero->Object);
			}
		}
	}

	void TownRun::AdvanceErrand()
	{
		m_currentErrand = ErrandNone;
		m_subStage = 0;
		m_waitTicks = 0;
		m_npcKey = -1;

		if (m_errandMask == ErrandNone)
		{
			// All done - warp back to the hunting ground.
			m_state = WarpingBack;
			m_stageTicks = 0;
			g_ConsoleDebug->Write(MCD_NORMAL, L"[MU Helper] Town errands done, warping back.");
		}
		else
		{
			// Find the next NPC on foot.
			m_state = WalkingToNpc;
			m_stageTicks = 0;
		}
	}

	bool TownRun::Update()
	{
		if (m_buffCooldownTicks > 0)
		{
			--m_buffCooldownTicks;
		}

		if (!Active)
		{
			EvaluateTriggers();
			// If triggers just began a run, own this tick immediately rather than
			// letting combat run once before the warp is sent.
			return Active;
		}

		++m_stageTicks;

		switch (m_state)
		{
		case WarpingToTown:
		{
			if (m_stageTicks == 1)
			{
				SocketClient->ToGameServer()->SendWarpCommandRequest(
					g_pMoveCommandWindow->GetMoveCommandKey(), LORENCIA_MOVEREQ_INDEX);
			}

			if (Hero->SafeZone)
			{
				m_state = WalkingToNpc;
				m_stageTicks = 0;
				m_currentErrand = ErrandNone;
			}
			else if (m_stageTicks > WARP_WAIT_TICKS)
			{
				Abort(L"warp to town timed out");
			}
			break;
		}

		case WalkingToNpc:
		{
			// Pick the next pending errand and its NPC.
			if (m_currentErrand == ErrandNone)
			{
				if (m_errandMask & BUFF_BIT)
				{
					m_currentErrand = ErrandBuff;
				}
				else if (m_errandMask & BUY_BIT)
				{
					m_currentErrand = ErrandBuy;
				}
				else if (m_errandMask & STORE_BIT)
				{
					m_currentErrand = ErrandStore;
				}
				else
				{
					AdvanceErrand();
					break;
				}
				m_stageTicks = 0;
				m_npcKey = -1;
			}

			int ids[4] = {};
			int idCount = 0;
			switch (m_currentErrand)
			{
			case ErrandBuff:
				ids[idCount++] = MONSTER_ID_BUFF;
				break;
			case ErrandBuy:
				ids[idCount++] = MONSTER_ID_POTION_LALA;
				ids[idCount++] = MONSTER_ID_POTION_AMY;
				break;
			case ErrandStore:
				ids[idCount++] = MONSTER_ID_VAULT;
				break;
			default:
				break;
			}

			POINT npcPos = { 0, 0 };
			int npcKey = -1;
			if (FindNearestNpc(ids, idCount, npcKey, npcPos))
			{
				m_npcKey = npcKey;
				m_npcPos = npcPos;
			}

			if (m_npcKey == -1)
			{
				if (m_stageTicks > WALK_WAIT_TICKS)
				{
					Abort(L"target NPC not found");
				}
				break;
			}

			int dist = ManhattanDist(Hero->PositionX, Hero->PositionY, m_npcPos.x, m_npcPos.y);
			if (dist <= 2)
			{
				Hero->Movement = 0;
				SocketClient->ToGameServer()->SendTalkToNpcRequest((uint16_t)m_npcKey);
				m_state = Interacting;
				m_stageTicks = 0;
				m_subStage = 0;
				m_waitTicks = 0;
				m_buyTicks = 0;
				break;
			}
			else if (!Hero->Movement || (m_stageTicks % REPATH_TICKS) == 0)
			{
				WalkTo(m_npcPos.x, m_npcPos.y);
			}

			if (m_stageTicks > WALK_WAIT_TICKS)
			{
				Abort(L"walk to NPC timed out");
			}
			break;
		}

		case Interacting:
		{
			bool advance = false;
			switch (m_currentErrand)
			{
			case ErrandBuff:
			{
				if (m_subStage == 0)
				{
					if (g_pNewUISystem->IsVisible(INTERFACE_NPC_DIALOGUE))
					{
						SocketClient->ToGameServer()->SendNpcBuffRequest();
						m_subStage = 1;
						m_waitTicks = 0;
					}
					else if (m_stageTicks > DIALOG_WAIT_TICKS)
					{
						Abort(L"NPC dialog did not open");
					}
				}
				else
				{
					if (g_isCharacterBuff((&Hero->Object), eBuff_HelpNpc))
					{
						advance = true;
					}
					else if (m_waitTicks++ > DIALOG_WAIT_TICKS)
					{
						// Likely level-gated (Elf Soldier buff stops above a certain level).
						// Back off instead of spamming a town trip every cycle.
						m_buffCooldownTicks = BUFF_RETRY_TICKS;
						g_ConsoleDebug->Write(MCD_NORMAL,
							L"[MU Helper] Elf Soldier buff was not applied; backing off.");
						advance = true;
					}
				}
				break;
			}

			case ErrandBuy:
			{
				if (m_subStage == 0)
				{
					if (g_pNewUISystem->IsVisible(INTERFACE_NPCSHOP))
					{
						m_subStage = 1;
						m_buyTicks = 0;
					}
					else if (m_stageTicks > DIALOG_WAIT_TICKS)
					{
						Abort(L"NPC shop did not open");
					}
				}
				else
				{
					CNewUIInventoryCtrl* shop = g_pNPCShop->GetInventoryCtrl();
					CNewUIInventoryCtrl* inv = g_pMyInventory->GetInventoryCtrl();

					int heal = CountPotionCharges(inv, (short)(ITEM_POTION + 1), (short)(ITEM_POTION + 3));
					int mana = CountPotionCharges(inv, (short)(ITEM_POTION + 4), (short)(ITEM_POTION + 6));

					bool needHeal = heal < POTION_TARGET_CHARGES;
					bool needMana = mana < POTION_TARGET_CHARGES;

					if ((!needHeal && !needMana) || m_buyTicks >= MAX_PURCHASES)
					{
						advance = true;
					}
					else if ((m_stageTicks % BUY_PACE_TICKS) == 0)
					{
						short wanted = 0;
						if (needHeal)
						{
							// Prefer large potions, fall back to smaller ones.
							for (short t = (short)(ITEM_POTION + 3); t >= (short)(ITEM_POTION + 1); --t)
							{
								if (shop->IsItem(t)) { wanted = t; break; }
							}
						}
						if (wanted == 0 && needMana)
						{
							for (short t = (short)(ITEM_POTION + 6); t >= (short)(ITEM_POTION + 4); --t)
							{
								if (shop->IsItem(t)) { wanted = t; break; }
							}
						}

						if (wanted == 0)
						{
							if (shop->GetNumberOfItems() == 0)
							{
								// Shop window is visible but its item list has not arrived yet;
								// wait for it rather than treating "empty" as "nothing to buy".
								break;
							}
							// Merchant stocks nothing we need.
							advance = true;
						}
						else
						{
							ITEM* offer = shop->FindTypeItem(wanted);
							if (offer != nullptr)
							{
								int price = (int)ItemValue(offer, 0);
								if ((int64_t)CharacterMachine->Gold >= price)
								{
									int slot = shop->GetIndexByItem(offer);
								if (slot >= 0)
								{
									SocketClient->ToGameServer()->SendBuyItemFromNpcRequest((BYTE)slot);
									++m_buyTicks;
								}
								else
								{
									advance = true;
								}
							}
							else
							{
								g_ConsoleDebug->Write(MCD_NORMAL,
									L"[MU Helper] Not enough zen to buy potions; stopping.");
								advance = true;
							}
							}
						}
					}
				}
				break;
			}

			case ErrandStore:
			{
				if (m_subStage == 0)
				{
					if (g_pNewUISystem->IsVisible(INTERFACE_STORAGE))
					{
						if (g_pStorageInventory->IsStorageLocked())
						{
							// A vault PIN is set and cannot be entered headless - skip this errand.
							// PIN-less vaults have m_bLock == false and move items freely.
							g_ConsoleDebug->Write(MCD_NORMAL,
								L"[MU Helper] Vault PIN is set; skipping auto-store.");
							SocketClient->ToGameServer()->SendVaultClosed();
							m_errandMask &= ~STORE_BIT;
							AdvanceErrand();
							break;
						}
						m_subStage = 1;
					}
					else if (m_stageTicks > DIALOG_WAIT_TICKS)
					{
						Abort(L"storage did not open");
					}
				}
				else
				{
					bool moved = false;
					bool anyMovable = false;

					// Pace item moves so the server can process each one before the next.
					if ((m_stageTicks % STORE_PACE_TICKS) != 0)
					{
						break;
					}

					CNewUIInventoryCtrl* inv = g_pMyInventory->GetInventoryCtrl();
					size_t count = inv->GetNumberOfItems();
					for (size_t i = 0; i < count; ++i)
					{
						ITEM* item = inv->GetItem((int)i);
						if (item == nullptr)
						{
							continue;
						}

						int fromSlot = inv->GetIndexByItem(item);
						if (fromSlot < MAX_EQUIPMENT)
						{
							continue; // equipped gear / wear slots
						}

						anyMovable = true;
						int toSlot = g_pStorageInventory->FindEmptySlot(item);
						if (toSlot < 0)
						{
							continue; // vault full for this shape, try others
						}

						SocketClient->ToGameServer()->SendItemMoveRequestExtended(
							ItemStorageKind::Inventory, (BYTE)fromSlot,
							ItemStorageKind::Vault, (BYTE)toSlot);
						moved = true;
						break;
					}

					if (!moved)
					{
						// Nothing left to move (or vault full) - done.
						if (!anyMovable || inv->GetEmptySlotCount() >= FREE_SLOT_PRESSURE)
						{
							advance = true;
						}
					}

					if (m_stageTicks > WALK_WAIT_TICKS)
					{
						advance = true;
					}
				}
				break;
			}

			default:
				Abort(L"unknown errand");
				break;
			}

			if (advance)
			{
				switch (m_currentErrand)
				{
				case ErrandBuff:
					SocketClient->ToGameServer()->SendCloseNpcRequest();
					m_errandMask &= ~BUFF_BIT;
					break;
				case ErrandBuy:
					SocketClient->ToGameServer()->SendCloseNpcRequest();
					m_errandMask &= ~BUY_BIT;
					break;
				case ErrandStore:
					SocketClient->ToGameServer()->SendVaultClosed();
					m_errandMask &= ~STORE_BIT;
					break;
				default:
					break;
				}
				AdvanceErrand();
			}
			break;
		}

		case WarpingBack:
		{
			if (m_stageTicks == 1)
			{
				int idx = MapToMovereqIndex(m_originMap);
				if (idx < 0)
				{
					// Cannot warp back - no movereq entry for this map. The character is
					// left in Lorencia; log it (the safe-zone kill switch will stop the
					// helper rather than leave it running in town).
					g_ConsoleDebug->Write(MCD_NORMAL,
						L"[MU Helper] No warp-back route for map %d; staying in town.", m_originMap);
					Reset();
					break;
				}
				SocketClient->ToGameServer()->SendWarpCommandRequest(
					g_pMoveCommandWindow->GetMoveCommandKey(), (uint16_t)idx);
			}

			if (gMapManager.WorldActive == m_originMap)
			{
				// Back on the hunting map; walk to the remembered spot even if the
				// landing tile is a safe-zone (Active keeps the kill-switch bypassed).
				m_state = WalkingHome;
				m_stageTicks = 0;
			}
			else if (m_stageTicks > WARP_WAIT_TICKS)
			{
				Abort(L"warp back timed out");
			}
			break;
		}

		case WalkingHome:
		{
			int dist = ManhattanDist(Hero->PositionX, Hero->PositionY, m_originPos.x, m_originPos.y);
			if (dist <= 2)
			{
				FinishRun();
			}
			else if (!Hero->Movement || (m_stageTicks % REPATH_TICKS) == 0)
			{
				WalkTo(m_originPos.x, m_originPos.y);
			}

			if (m_stageTicks > WALK_WAIT_TICKS)
			{
				// Even if we could not reach the exact tile, resume hunting from here.
				g_ConsoleDebug->Write(MCD_NORMAL,
					L"[MU Helper] Walk-home timed out; resuming from current position.");
				FinishRun();
			}
			break;
		}

		default:
			Reset();
			break;
		}

		return true;
	}
}
