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

// kitty unicode placeholder scheme (graphics-protocol doc):
// - transmit image once (a=t), create one virtual placement (a=p,U=1,c=,r=)
// - each rendered cell is text: U+10EEEE + row + column + msb diacritics
// - the cell's foreground color encodes the image id: for 256-color output
//   (38;5;N) the palette index N IS the low byte of the id; a 3rd diacritic
//   encodes the most significant byte (id = N | (msb << 24))
// - all diacritics are emitted per cell: ncurses repaints lines
//   non-contiguously, so the spec's inherit-from-left rules cannot apply

namespace
{

const char* s_EscG = "\033_G";
const char* s_EscEnd = "\033\\";

// kitty image placeholder base character
const wchar_t s_PlaceholderChar = (wchar_t)0x10EEEE;

// ncurses pair base for image-fg pairs (theme uses low pair indices)
const int s_PairBase = 300;

struct ImgRec
{
  unsigned int id = 0;   // kitty image id: lowByte | (msb << 24)
  int lowByte = 0;       // id byte 0 == emitted 256-palette fg index
  int msb = 0;           // id byte 3 == 3rd diacritic value
  int pairIdx = -1;
  int cols = 0;
  int rows = 0;
  bool transmitted = false;
};

// path -> 1-based index into s_Recs (stable handle for callers)
std::unordered_map<std::string, int> s_PathSeq;
std::vector<ImgRec> s_Recs;

// lowByte (0-255) -> ncurses pair idx, -1 = not created yet
int s_PairFor[256];
bool s_PairForInit = false;

// diacritics that glibc considers combining (wcwidth == 0), built lazily
std::vector<int> s_UsableDiacritics;
bool s_DiacriticsInit = false;

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

// diacritic encoding value p_Val (0-based; table[i] encodes number i)
wchar_t DiacriticForValue(int p_Val)
{
  InitDiacritics();
  if ((p_Val < 0) || (p_Val >= (int)s_UsableDiacritics.size())) return 0;
  return (wchar_t)s_UsableDiacritics[p_Val];
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

void SendEsc(const std::string& p_Esc)
{
  fwrite(p_Esc.data(), 1, p_Esc.size(), stdout);
  fflush(stdout);
}

void DeleteImage(unsigned int p_Id)
{
  // d=i deletes the image and all its placements, including virtual ones
  SendEsc(std::string(s_EscG) + "q=2,a=d,d=i,i=" + std::to_string(p_Id) +
          s_EscEnd);
}

bool Transmit(const std::string& p_Path, unsigned int p_Id)
{
  int w = 0, h = 0, comp = 0;
  unsigned char* pixels = stbi_load(p_Path.c_str(), &w, &h, &comp, 4);
  if (!pixels) return false;

  int pngLen = 0;
  unsigned char* png = stbi_write_png_to_mem(pixels, w * 4, w, h, 4, &pngLen);
  stbi_image_free(pixels);
  if (!png) return false;

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
    SendEsc(esc);
    pos += n;
    first = false;
    if (b64.empty()) break;
  }
  return true;
}

void Place(unsigned int p_Id, int p_Cols, int p_Rows)
{
  // U=1: virtual placement, shown only through unicode placeholder cells
  SendEsc(std::string(s_EscG) + "q=2,a=p,U=1,i=" + std::to_string(p_Id) +
          ",c=" + std::to_string(p_Cols) + ",r=" + std::to_string(p_Rows) +
          s_EscEnd);
}

// ncurses pair with fg == p_LowByte; kitty reads 38;5;N as id byte N
int PairFor(int p_LowByte)
{
  if (!s_PairForInit)
  {
    s_PairForInit = true;
    for (int i = 0; i < 256; ++i) s_PairFor[i] = -1;
  }

  int& pairIdx = s_PairFor[p_LowByte];
  if (pairIdx >= 0) return pairIdx;

  int idx = s_PairBase + p_LowByte;
  if (init_pair(idx, p_LowByte, -1) != OK)
  {
    if (init_pair(idx, p_LowByte, COLOR_BLACK) != OK) return -1;
  }
  pairIdx = idx;
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

// returns a stable 1-based handle (index into the image table), or -1
int KittyImg::EnsureImage(const std::string& p_Path, int p_Cols, int p_Rows)
{
  if (!Supported() || (p_Cols <= 0) || (p_Rows <= 0)) return -1;
  if (p_Cols > 255 || p_Rows > 255) return -1;

  auto it = s_PathSeq.find(p_Path);
  int seq;
  if (it == s_PathSeq.end())
  {
    seq = (int)s_Recs.size() + 1;
    ImgRec rec;
    rec.lowByte = ((seq - 1) % 255) + 1;
    rec.msb = (seq - 1) / 255;
    rec.id = (unsigned int)rec.lowByte | ((unsigned int)rec.msb << 24);
    s_Recs.push_back(rec);
    s_PathSeq[p_Path] = seq;
  }
  else
  {
    seq = it->second;
  }

  ImgRec& rec = s_Recs[(size_t)seq - 1];

  if (rec.pairIdx < 0) rec.pairIdx = PairFor(rec.lowByte);
  if (rec.pairIdx < 0) return -1;

  if (!rec.transmitted)
  {
    if (!Transmit(p_Path, rec.id)) return -1;
    rec.transmitted = true;
  }

  // one virtual placement per image; a different box needs a fresh
  // placement, which requires retransmission (d=i drops the image data)
  if ((rec.cols != p_Cols) || (rec.rows != p_Rows))
  {
    if (rec.cols > 0)
    {
      DeleteImage(rec.id);
      rec.transmitted = false;
      if (!Transmit(p_Path, rec.id)) return -1;
      rec.transmitted = true;
    }
    Place(rec.id, p_Cols, p_Rows);
    rec.cols = p_Cols;
    rec.rows = p_Rows;
  }

  return seq;
}

int KittyImg::ImagePair(int p_Seq)
{
  if ((p_Seq <= 0) || (p_Seq > (int)s_Recs.size())) return -1;
  return s_Recs[(size_t)p_Seq - 1].pairIdx;
}

std::wstring KittyImg::PlaceholderLine(int p_Seq, int p_Row, int p_Cols)
{
  if ((p_Seq <= 0) || (p_Seq > (int)s_Recs.size())) return std::wstring();

  const ImgRec& rec = s_Recs[(size_t)p_Seq - 1];
  wchar_t rowD = DiacriticForValue(p_Row);
  wchar_t msbD = DiacriticForValue(rec.msb);

  std::wstring ws;
  ws.reserve((size_t)p_Cols * 4);
  for (int c = 0; c < p_Cols; ++c)
  {
    ws += s_PlaceholderChar;
    if (rowD) ws += rowD;
    wchar_t colD = DiacriticForValue(c);
    if (colD) ws += colD;
    if (msbD) ws += msbD;
  }
  return ws;
}

void KittyImg::ClearAll()
{
  if (!Supported()) return;
  std::string esc;
  for (const ImgRec& rec : s_Recs)
  {
    if (rec.transmitted)
    {
      esc += std::string(s_EscG) + "q=2,a=d,d=i,i=" + std::to_string(rec.id) +
        s_EscEnd;
    }
  }
  if (!esc.empty()) SendEsc(esc);
  for (ImgRec& rec : s_Recs)
  {
    rec.transmitted = false;
    rec.cols = 0;
    rec.rows = 0;
  }
}
