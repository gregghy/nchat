/*
  kittyimg.h

  Copyright (c) 2025 gregghy. All rights reserved.

  nchat is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  nchat is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with nchat.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <string>

class KittyImg
{
public:
  static bool Supported();
  static bool GetImageDims(const std::string& p_Path, int& p_W, int& p_H);

  // terminal cell aspect (cell pixel height / width), ~2.0 typical
  static float CellAspect();

  // ensures image is transmitted + virtually placed; returns a stable
  // 1-based handle for the image, or -1 on failure
  static int EnsureImage(const std::string& p_Path, int p_Cols, int p_Rows);

  // ncurses color pair idx whose fg encodes the image id's low byte
  static int ImagePair(int p_Seq);

  // wstring of placeholder cells for one row of an image (kitty unicode
  // placeholders: U+10EEEE + row + column + msb diacritics)
  static std::wstring PlaceholderLine(int p_Seq, int p_Row, int p_Cols);

  static void ClearAll();
};
