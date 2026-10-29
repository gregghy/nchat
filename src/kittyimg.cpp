/*
  kittyimg.cpp

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

#include "kittyimg.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <unordered_map>
#include <vector>

#include <curses.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "kittydiacritics.h"

namespace
{

const char* s_EscG = "\033_G";
const char* s_EscEnd = "\033\\";

// kitty image placeholder base character
const wchar_t s_PlaceholderChar = (wchar_t)0x10EEEE;

// stable per-path image id (kitty ids are uint32; use 1-based counter)
std::unordered_map<std::string, int> s_PathIds;
std::unordered_map<int, std::string> s_IdPaths;
int s_NextId = 1;

std::unordered_map<int, bool> s_Transmitted;
std::unordered_map<int, std::pair<int, int> > s_Placed;

// diacritics that glibc considers combining (wcwidth == 0), built lazily
std::vector<int> s_UsableDiacritics;
bool s_DiacriticsInit = false;

int GetId(const std::string& p_Path)
{
  auto it = s_PathIds.find(p_Path);
  if (it != s_PathIds.end()) return it->second;
  int id = s_NextId++;
  s_PathIds[p_Path] = id;
  s_IdPaths[id] = p_Path;
  return id;
}

void InitDiacritics()
{
  if (s_DiacriticsInit) return;
  s_DiacriticsInit = true;
  for (int i = 0; i < s_RowColDiacriticsCount; ++i)
  {
    if (wcwidth((wchar_t)s_RowColDiacritics[i]) == 0)
    {
      s_UsableDiacritics.push_back(s_RowColDiacritics[i]);
    }
  }
}

wchar_t Diacritic(int p_Idx)
{
  InitDiacritics();
  if ((p_Idx <= 0) || (p_Idx > (int)s_UsableDiacritics.size())) return 0;
  return (wchar_t)s_UsableDiacritics[p_Idx - 1];
}

std::string Base64(const unsigned char* p_Data, size_t p_Len)
{
  static const char* tbl =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((p_Len + 2) / 3) * 4);
  for (size_t i = 0; i < p_Len; i += 3)
  {
    unsigned int v = (unsigned int)p_Data[i] << 16;
    if (i + 1 < p_Len) v |= (unsigned int)p_Data[i + 1] << 8;
    if (i + 2 < p_Len) v |= (unsigned int)p_Data[i + 2];
    out += tbl[(v >> 18) & 63];
    out += tbl[(v >> 12) & 63];
    out += (i + 1 < p_Len) ? tbl[(v >> 6) & 63] : '=';
    out += (i + 2 < p_Len) ? tbl[v & 63] : '=';
  }
  return out;
}

void Transmit(int p_Id)
{
  if (s_Transmitted[p_Id]) return;

  const std::string& path = s_IdPaths[p_Id];
  int w = 0, h = 0, comp = 0;
  unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &comp, 4);
  if (!pixels) return;

  int pngLen = 0;
  unsigned char* png = stbi_write_png_to_mem(pixels, w * 4, w, h, 4, &pngLen);
  stbi_image_free(pixels);
  if (!png) return;

  std::string b64 = Base64(png, (size_t)pngLen);
  STBIW_FREE(png);

  // chunked transmission, 4096 bytes per chunk
  const size_t chunkSize = 4096;
  size_t pos = 0;
  bool first = true;
  while (pos < b64.size() || first)
  {
    size_t n = std::min(chunkSize, b64.size() - pos);
    bool last = (pos + n) >= b64.size();
    std::string esc = s_EscG;
    esc += "q=2,";
    if (first)
    {
      esc += "a=t,t=d,f=100,i=" + std::to_string(p_Id) + ",";
    }
    esc += std::string("m=") + (last ? "0" : "1");
    esc += ";";
    esc += b64.substr(pos, n);
    esc += s_EscEnd;
    fwrite(esc.data(), 1, esc.size(), stdout);
    pos += n;
    first = false;
    if (b64.empty()) break;
  }
  fflush(stdout);
  s_Transmitted[p_Id] = true;
}

void Place(int p_Id, int p_Cols, int p_Rows)
{
  std::pair<int, int> box(p_Cols, p_Rows);
  if (s_Placed[p_Id] == box) return;

  // U=1: virtual placement, shown only through unicode placeholder cells
  std::string esc = s_EscG;
  esc += "q=2,a=p,U=1,i=" + std::to_string(p_Id);
  esc += ",c=" + std::to_string(p_Cols) + ",r=" + std::to_string(p_Rows);
  esc += s_EscEnd;
  fwrite(esc.data(), 1, esc.size(), stdout);
  fflush(stdout);
  s_Placed[p_Id] = box;
}

// ncurses color pair whose fg resolves to the image id's 24-bit value;
// kitty matches placeholder cells to images by their fg color.
// the palette entry is written directly via OSC 4 (init_color's 0-1000
// scale truncates and would not round-trip small ids). slots are bounded
// (theme colors use the low palette range) and re-initialized per call —
// far more slots than visible image rows exist.
int ColorPair(int p_Id)
{
  const int base = 200;
  int avail = COLORS - base;
  if (avail <= 0) return -1;

  int slot = (p_Id - 1) % avail;
  int colorIdx = base + slot;
  int pairIdx = base + slot;

  char osc[64];
  snprintf(osc, sizeof(osc), "\033]4;%d;rgb:%02x/%02x/%02x\033\\",
           colorIdx, (p_Id >> 16) & 0xFF, (p_Id >> 8) & 0xFF, p_Id & 0xFF);
  fwrite(osc, 1, strlen(osc), stdout);

  if (init_pair(pairIdx, colorIdx, -1) != OK) return -1;
  return pairIdx;
}

} // namespace

bool KittyImg::Supported()
{
  static int supported = -1;
  if (supported < 0)
  {
    const char* term = getenv("TERM");
    const char* kittyId = getenv("KITTY_WINDOW_ID");
    supported = (kittyId && *kittyId) ||
                (term && strstr(term, "kitty")) ? 1 : 0;
  }
  return supported == 1;
}

bool KittyImg::GetImageDims(const std::string& p_Path, int& p_W, int& p_H)
{
  if (!Supported()) return false;
  int comp = 0;
  if (!stbi_info(p_Path.c_str(), &p_W, &p_H, &comp)) return false;
  return (p_W > 0) && (p_H > 0);
}

int KittyImg::EnsureImage(const std::string& p_Path, int p_Cols, int p_Rows)
{
  if (!Supported() || (p_Cols <= 0) || (p_Rows <= 0)) return -1;

  int id = GetId(p_Path);
  Transmit(id);
  if (!s_Transmitted[id]) return -1;
  Place(id, p_Cols, p_Rows);
  return ColorPair(id);
}

std::wstring KittyImg::PlaceholderLine(int p_Row, int p_Cols)
{
  std::wstring ws;
  ws.reserve((size_t)p_Cols * 3);
  wchar_t rowD = Diacritic(p_Row);
  for (int c = 0; c < p_Cols; ++c)
  {
    ws += s_PlaceholderChar;
    if (rowD) ws += rowD;
    wchar_t colD = Diacritic(c);
    if (colD) ws += colD;
  }
  return ws;
}

void KittyImg::ClearAll()
{
  if (!Supported()) return;
  std::string esc = std::string(s_EscG) + "q=2,a=d,d=A" + s_EscEnd;
  // restore palette slots used for image-id colors
  for (int i = 200; i < COLORS; ++i)
  {
    char buf[24];
    snprintf(buf, sizeof(buf), "\033]104;%d\033\\", i);
    esc += buf;
  }
  fwrite(esc.data(), 1, esc.size(), stdout);
  fflush(stdout);
  s_Transmitted.clear();
  s_Placed.clear();
}
