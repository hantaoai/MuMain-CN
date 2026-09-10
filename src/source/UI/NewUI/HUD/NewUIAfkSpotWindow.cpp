//////////////////////////////////////////////////////////////////////
// NewUIAfkSpotWindow.cpp: implementation of the CNewUIAfkSpotWindow class.
//
// Lists recommended AFK hunting spots for the map the player is currently on
// (data from AfkSpotsData.h). Clicking a row hands off to CMuHelper::GoAfkSpot,
// which runs the TownRun sequence (Lorencia buff + potions -> warp back -> walk
// to the tile -> start hunting). At most 8 spots per map, so no scrollbar.
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "UI/NewUI/HUD/NewUIAfkSpotWindow.h"
#include "UI/NewUI/NewUISystem.h"
#include "World/MapInfra/MapManager.h"
#include "Engine/Object/ZzzInterface.h"
#include "MUHelper/MuHelper.h"
#include "Audio/DSPlaySound.h"
#include "I18N/All.h"

#include <cstdlib>

using namespace SEASON3B;

namespace
{
    const int AFK_MAX_ROWS = 8;
    const int AFK_TITLE_H = 22;
    const int AFK_HEADER_H = 18;
    const int AFK_CLOSE_H = 18;
    const int AFK_LINE_PAD = 2;
    const int AFK_WIN_WIDTH = 250;
    const int AFK_COL_XY = 12;
    const int AFK_COL_LV = 96;
    const int AFK_COL_MON = 132;
}

CNewUIAfkSpotWindow::CNewUIAfkSpotWindow()
    : m_pNewUIMng(nullptr)
    , m_width(AFK_WIN_WIDTH)
    , m_height(0)
    , m_lineHeight(0)
    , m_iSelectedRow(-1)
    , m_pSpots(nullptr)
{
    m_Pos.x = m_Pos.y = 0;
}

CNewUIAfkSpotWindow::~CNewUIAfkSpotWindow()
{
    Release();
}

bool CNewUIAfkSpotWindow::Create(CNewUIManager* pNewUIMng, int x, int y)
{
    if (pNewUIMng == nullptr)
    {
        return false;
    }

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_AFK_SPOTS, this);

    SetPos(x, y);
    Show(false);
    return true;
}

void CNewUIAfkSpotWindow::Release()
{
    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = nullptr;
    }
}

void CNewUIAfkSpotWindow::SetPos(int x, int y)
{
    m_Pos.x = x;
    m_Pos.y = y;

    m_lineHeight = FontHeight * REFERENCE_WIDTH / WindowWidth + 2;
    int rows = (m_pSpots != nullptr) ? m_pSpots->count : 0;
    if (rows > AFK_MAX_ROWS)
    {
        rows = AFK_MAX_ROWS;
    }
    if (rows < 1)
    {
        rows = 1;  // room for the "no spots" message
    }
    m_height = AFK_TITLE_H + AFK_HEADER_H + (rows * (m_lineHeight + AFK_LINE_PAD)) + AFK_CLOSE_H + 6;
}

bool CNewUIAfkSpotWindow::UpdateKeyEvent()
{
    if (IsVisible() && SEASON3B::IsPress(VK_ESCAPE))
    {
        Show(false);
        PlayBuffer(SOUND_CLICK01);
        return false;
    }
    return true;
}

bool CNewUIAfkSpotWindow::Update()
{
    return true;
}

bool CNewUIAfkSpotWindow::BtnProcess()
{
    if (!IsVisible())
    {
        return false;
    }

    // Close bar at the bottom.
    int closeY = m_Pos.y + m_height - AFK_CLOSE_H - 2;
    if (CheckMouseIn(m_Pos.x + 2, closeY, m_width - 4, AFK_CLOSE_H)
        && SEASON3B::IsRelease(VK_LBUTTON))
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_AFK_SPOTS);
        return true;
    }

    if (!CheckMouseIn(m_Pos.x, m_Pos.y, m_width, m_height))
    {
        return false;
    }

    int startY = m_Pos.y + AFK_TITLE_H + AFK_HEADER_H;
    int rows = (m_pSpots != nullptr) ? m_pSpots->count : 0;
    if (rows > AFK_MAX_ROWS)
    {
        rows = AFK_MAX_ROWS;
    }

    int playerLv = (CharacterAttribute != nullptr) ? CharacterAttribute->Level : 1;

    for (int i = 0; i < rows; ++i)
    {
        int ry = startY + i * (m_lineHeight + AFK_LINE_PAD);
        if (CheckMouseIn(m_Pos.x + 2, ry, m_width - 4, m_lineHeight))
        {
            m_iSelectedRow = i;
            if (SEASON3B::IsRelease(VK_LBUTTON) && m_pSpots != nullptr && i < m_pSpots->count)
            {
                const AfkSpot& spot = m_pSpots->spots[i];
                g_ConsoleDebug->Write(MCD_NORMAL,
                    L"[AFK spots] selected (%d,%d) lvMin=%d lv=%d playerLv=%d",
                    spot.x, spot.y, spot.lvMin, spot.lv, playerLv);
                MUHelper::g_MuHelper.GoAfkSpot(spot.x, spot.y);
                PlayBuffer(SOUND_CLICK01);
                g_pNewUISystem->Hide(SEASON3B::INTERFACE_AFK_SPOTS);
                return true;
            }
        }
    }

    return false;
}

bool CNewUIAfkSpotWindow::UpdateMouseEvent()
{
    if (!IsVisible())
    {
        return true;
    }
    if (BtnProcess())
    {
        return false;
    }
    if (CheckMouseIn(m_Pos.x, m_Pos.y, m_width, m_height))
    {
        return false;
    }
    return true;
}

void CNewUIAfkSpotWindow::RenderFrame()
{
    // Window background.
    glColor4f(0.0f, 0.0f, 0.0f, 0.82f);
    RenderColor((float)m_Pos.x, (float)m_Pos.y, (float)m_width, (float)m_height);

    // Title bar.
    glColor4f(0.45f, 0.05f, 0.05f, 1.0f);
    RenderColor((float)m_Pos.x, (float)m_Pos.y, (float)m_width, (float)AFK_TITLE_H);

    // Header separator.
    int headerY = m_Pos.y + AFK_TITLE_H;
    glColor4f(0.6f, 0.0f, 0.0f, 1.0f);
    RenderColor((float)m_Pos.x, (float)(headerY + AFK_HEADER_H - 2), (float)m_width, 1.0f);

    // Close bar.
    int closeY = m_Pos.y + m_height - AFK_CLOSE_H - 2;
    glColor4f(0.45f, 0.05f, 0.05f, 1.0f);
    RenderColor((float)(m_Pos.x + 2), (float)closeY, (float)(m_width - 4), (float)AFK_CLOSE_H);
}

bool CNewUIAfkSpotWindow::Render()
{
    if (!IsVisible())
    {
        return true;
    }

    EnableAlphaTest();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    RenderFrame();

    int titleY = m_Pos.y + 4;
    int centerX = m_Pos.x + m_width / 2;

    g_pRenderText->SetFont(g_hFontBold);
    g_pRenderText->SetBgColor(0);
    g_pRenderText->SetTextColor(255, 204, 26, 255);
    g_pRenderText->RenderText(centerX, titleY, I18N::Game::AfkSpots, 0, 0, RT3_WRITE_CENTER);

    // Column headers.
    int headerY = m_Pos.y + AFK_TITLE_H + 2;
    g_pRenderText->SetFont(g_hFont);
    g_pRenderText->SetTextColor(127, 178, 255, 255);
    g_pRenderText->RenderText(m_Pos.x + AFK_COL_XY, headerY, I18N::Game::AfkSpotsCoord, 0, 0, 0);
    g_pRenderText->RenderText(m_Pos.x + AFK_COL_LV, headerY, I18N::Game::AfkSpotsLevel, 0, 0, 0);
    g_pRenderText->RenderText(m_Pos.x + AFK_COL_MON, headerY, I18N::Game::AfkSpotsMonsters, 0, 0, 0);

    int startY = m_Pos.y + AFK_TITLE_H + AFK_HEADER_H;
    int playerLv = (CharacterAttribute != nullptr) ? CharacterAttribute->Level : 1;
    wchar_t szBuf[96];

    int rows = 0;
    if (m_pSpots != nullptr)
    {
        rows = m_pSpots->count;
        if (rows > AFK_MAX_ROWS)
        {
            rows = AFK_MAX_ROWS;
        }
    }

    if (rows == 0)
    {
        g_pRenderText->SetTextColor(180, 180, 180, 255);
        g_pRenderText->RenderText(centerX, startY + 6, I18N::Game::AfkSpotsNone, 0, 0, RT3_WRITE_CENTER);
    }

    for (int i = 0; i < rows; ++i)
    {
        const AfkSpot& spot = m_pSpots->spots[i];
        int ry = startY + i * (m_lineHeight + AFK_LINE_PAD);

        if (m_iSelectedRow == i)
        {
            glColor4f(0.8f, 0.8f, 0.1f, 0.55f);
            RenderColor((float)(m_Pos.x + 2), (float)(ry - 1), (float)(m_width - 4), (float)m_lineHeight);
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
            EnableAlphaTest();
        }

        // Tier color on the level text.
        // green = safely below player, yellow = around player level, red = above player.
        int r = 255, g = 255, b = 255;
        if (spot.lvMin <= playerLv - 5)
        {
            r = 90; g = 220; b = 90;
        }
        else if (spot.lv > playerLv + 5)
        {
            r = 255; g = 90; b = 70;
        }
        else
        {
            r = 255; g = 220; b = 80;
        }

        g_pRenderText->SetTextColor(255, 255, 255, 255);
        mu_swprintf(szBuf, L"(%d,%d)", spot.x, spot.y);
        g_pRenderText->RenderText(m_Pos.x + AFK_COL_XY, ry, szBuf, 0, 0, 0);

        g_pRenderText->SetTextColor(r, g, b, 255);
        if (spot.lvMin < spot.lv)
        {
            mu_swprintf(szBuf, L"%d~%d", spot.lvMin, spot.lv);
        }
        else
        {
            mu_swprintf(szBuf, L"%d", spot.lv);
        }
        g_pRenderText->RenderText(m_Pos.x + AFK_COL_LV, ry, szBuf, 0, 0, 0);

        g_pRenderText->SetTextColor(210, 210, 210, 255);
        wchar_t wzMons[128] = { 0 };
        if (spot.mons != nullptr)
        {
            MultiByteToWideChar(CP_UTF8, 0, spot.mons, -1, wzMons, _countof(wzMons));
        }
        g_pRenderText->RenderText(m_Pos.x + AFK_COL_MON, ry, wzMons, 0, 0, 0);
    }

    int closeY = m_Pos.y + m_height - AFK_CLOSE_H + 1;
    g_pRenderText->SetTextColor(255, 255, 255, 255);
    g_pRenderText->RenderText(centerX, closeY, I18N::Game::Close388, 0, 0, RT3_WRITE_CENTER);

    DisableAlphaBlend();
    return true;
}

void CNewUIAfkSpotWindow::OpenningProcess()
{
    m_pSpots = GetAfkSpotsForWorld(gMapManager.WorldActive);
    m_iSelectedRow = -1;
    SetPos(m_Pos.x, m_Pos.y);
}

void CNewUIAfkSpotWindow::ClosingProcess()
{
    m_iSelectedRow = -1;
}

float CNewUIAfkSpotWindow::GetLayerDepth()
{
    return 8.35f;  // just above the move command window
}
