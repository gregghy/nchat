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
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

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

// stable per-path image id (kitty ids are uint32; use 1-based counter)
std::unordered_map<std::string, int> s_PathIds;
std::unordered_map<int, std::string> s_IdPaths;
int s_NextId = 1;

// per id: transmitted? + placement box last emitted
std::unordered_map<int, bool> s_Transmitted;
std::unordered_map<int, std::pair<int, int> > s_Placed;

int GetId(const std::string& p_Path)
{
  auto it = s_PathIds.find(p_Path);
  if (it != s_PathIds.end()) return it->second;
  int id = s_NextId++;
  s_PathIds[p_Path] = id;
  s_IdPaths[id] = p_Path;
  return id;
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

std::string U8(unsigned int p_Cp)
{
  std::string s;
  if (p_Cp < 0x80)
  {
    s += (char)p_Cp;
  }
  else if (p_Cp < 0x800)
  {
    s += (char)(0xC0 | (p_Cp >> 6));
    s += (char)(0x80 | (p_Cp & 0x3F));
  }
  else if (p_Cp < 0x10000)
  {
    s += (char)(0xE0 | (p_Cp >> 12));
    s += (char)(0x80 | ((p_Cp >> 6) & 0x3F));
    s += (char)(0x80 | (p_Cp & 0x3F));
  }
  else
  {
    s += (char)(0xF0 | (p_Cp >> 18));
    s += (char)(0x80 | ((p_Cp >> 12) & 0x3F));
    s += (char)(0x80 | ((p_Cp >> 6) & 0x3F));
    s += (char)(0x80 | (p_Cp & 0x3F));
  }
  return s;
}

// diacritic for row/col index; 0 is "no diacritic" (empty)
std::string Diacritic(int p_Idx)
{
  if (p_Idx <= 0) return std::string();
  if (p_Idx - 1 >= s_RowColDiacriticsCount) return std::string();
  return U8((unsigned int)s_RowColDiacritics[p_Idx - 1]);
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
  s_Transmitted[p_Id] = true;
}

void Place(int p_Id, int p_Cols, int p_Rows)
{
  std::pair<int, int> box(p_Cols, p_Rows);
  if (s_Placed[p_Id] == box) return;

  // U=1: virtual placement, shown only through unicode placeholders
  std::string esc = s_EscG;
  esc += "q=2,a=p,U=1,i=" + std::to_string(p_Id);
  esc += ",c=" + std::to_string(p_Cols) + ",r=" + std::to_string(p_Rows);
  esc += s_EscEnd;
  fwrite(esc.data(), 1, esc.size(), stdout);
  s_Placed[p_Id] = box;
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

void KittyImg::DrawCells(int p_X, int p_Y, int p_Cols, int p_Rows,
                         int p_FirstRow, const std::string& p_Path)
{
  if (!Supported() || (p_Cols <= 0) || (p_Rows <= 0) ||
      (p_FirstRow >= p_Rows)) return;

  int id = GetId(p_Path);
  Transmit(id);
  if (!s_Transmitted[id]) return;
  Place(id, p_Cols, p_Rows);

  // image id encoded as 24-bit fg color on the placeholder cells
  int r8 = (id >> 16) & 0xFF;
  int g8 = (id >> 8) & 0xFF;
  int b8 = id & 0xFF;

  std::string out;
  out.reserve(p_Rows * p_Cols * 8);
  out += "\0337"; // save cursor
  for (int r = p_FirstRow; r < p_Rows; ++r)
  {
    out += "\033[" + std::to_string(p_Y + r - p_FirstRow + 1) + ";" +
           std::to_string(p_X + 1) + "H";
    out += "\033[38;2;" + std::to_string(r8) + ";" + std::to_string(g8) +
           ";" + std::to_string(b8) + "m";
    std::string rowD = Diacritic(r);
    std::string cell = U8(0x10EEEE) + rowD;
    for (int c = 0; c < p_Cols; ++c)
    {
      out += cell + Diacritic(c);
    }
  }
  out += "\033[0m\0338"; // reset color, restore cursor
  fwrite(out.data(), 1, out.size(), stdout);
  fflush(stdout);
}

void KittyImg::ClearAll()
{
  if (!Supported()) return;
  std::string esc = std::string(s_EscG) + "q=2,a=d,d=A" + s_EscEnd;
  fwrite(esc.data(), 1, esc.size(), stdout);
  fflush(stdout);
  s_Transmitted.clear();
  s_Placed.clear();
}
