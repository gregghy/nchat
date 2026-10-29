// uientryview.cpp
//
// Copyright (c) 2019-2025 Kristofer Berggren
// All rights reserved.
//
// nchat is distributed under the MIT license, see LICENSE for details.

#include "uientryview.h"

#include <algorithm>

#include "strutil.h"
#include "uicolorconfig.h"
#include "uimodel.h"

UiEntryView::UiEntryView(const UiViewParams& p_Params)
  : UiViewBase(p_Params)
{
}

void UiEntryView::Draw()
{
  if (!m_Enabled) return;

  if (!m_Dirty)
  {
    wmove(m_Win, m_CursY, m_CursX);
    wrefresh(m_Win);
    return;
  }

  m_Dirty = false;

  curs_set(0);

  std::wstring input = m_Model->GetEntryStrLocked();
  const int inputPos = m_Model->GetEntryPosLocked();

  static int colorPair = UiColorConfig::GetColorPair("entry_color");
  static int attribute = UiColorConfig::GetAttribute("entry_attr");
  static int colorPairPrompt = UiColorConfig::GetColorPair("entry_prompt_color");
  static int colorPairHint = UiColorConfig::GetColorPair("entry_hint_color");

  // prompt glyph + indent; wrapped lines align under the text column
  static const wchar_t* prompt = L"❯ ";
  const int promptW = 2;

  std::wstring line;
  std::vector<std::wstring> lines;
  int cx = 0;
  int cy = 0;
  const int wrapW = std::max(1, m_W - promptW);
  lines = StrUtil::WordWrap(input, wrapW, false, false, false, 2, inputPos, cy, cx);

  werase(m_Win);
  wbkgd(m_Win, attribute | colorPair | ' ');
  wattron(m_Win, attribute | colorPair);

  int yoffs = (cy < (m_H - 1)) ? 0 : (cy - (m_H - 1));

  for (int i = 0; i < m_H; ++i)
  {
    if ((i + yoffs) < (int)lines.size())
    {
      line = lines.at(i + yoffs).c_str();
      line.erase(std::remove(line.begin(), line.end(), EMOJI_PAD), line.end());
      mvwaddwstr(m_Win, i, promptW, line.c_str());
    }
  }

  wattroff(m_Win, attribute | colorPair);

  wattron(m_Win, colorPairPrompt);
  mvwaddwstr(m_Win, 0, 0, prompt);
  wattroff(m_Win, colorPairPrompt);

  if (input.empty())
  {
    wattron(m_Win, colorPairHint);
    mvwaddwstr(m_Win, 0, promptW, L"Message");
    wattroff(m_Win, colorPairHint);
  }

  m_CursX = cx + promptW;
  m_CursY = (cy - yoffs);

  wmove(m_Win, m_CursY, m_CursX);
  wrefresh(m_Win);
}
