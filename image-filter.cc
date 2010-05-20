/* Copyright © 2008, 2009, 2010 Jakub Wilk
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 dated June, 1991.
 */

#include "version.hh"

#if HAVE_GRAPHICSMAGICK
#include <Magick++.h>
#endif

#include <bitset>
#include <map>
#include <vector>

#include "image-filter.hh"

#include "config.hh"
#include "djvuconst.hh"
#include "rle.hh"

static void dummy_quantizer(int width, int height, int *background_color, std::ostream &stream);

void WebSafeQuantizer::output_web_palette(std::ostream &stream)
{
  stream << "216" << std::endl;
  for (int r = 0; r < 6; r++)
  for (int g = 0; g < 6; g++)
  for (int b = 0; b < 6; b++)
  {
    unsigned char buffer[] = { 51 * r, 51 * g, 51 * b };
    stream.write(reinterpret_cast<char*>(buffer), 3);
  }
}

static inline void write_uint32(std::ostream &stream, uint32_t item)
{
  unsigned char buffer[4];
  for (int i = 0; i < 4; i++)
    buffer[i] = item >> ((3 - i) * 8);
  stream.write(reinterpret_cast<char*>(buffer), 4);
}

void MaskQuantizer::operator()(pdf::Renderer *out_fg, pdf::Renderer *out_bg, int width, int height,
  int *background_color, bool &has_foreground, bool &has_background, std::ostream &stream)
{
  if (out_fg == out_bg)
  { /* Don't bother to analyze images if they are obviously identical. */
    dummy_quantizer(width, height, background_color, stream);
    has_background = true;
    return;
  }
  rle::R4 r4(stream, width, height);
  pdf::Pixmap bmp_fg(out_fg);
  pdf::Pixmap bmp_bg(out_bg);
  pdf::Pixmap::iterator p_fg = bmp_fg.begin();
  pdf::Pixmap::iterator p_bg = bmp_bg.begin();
  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      if (!has_background)
      {
        for (int i = 0; i < 3; i++)
        if (background_color[i] != p_bg[i])
        {
          has_background = true;
          break;
        }
      }
      if (p_fg[0] != p_bg[0] || p_fg[1] != p_bg[1] || p_fg[2] != p_bg[2])
      {
        if (!has_foreground && (p_fg[0] || p_fg[1] || p_fg[2]))
          has_foreground = true;
        r4 << 1;
      }
      else
        r4 << 0;
      p_fg++, p_bg++;
    }
    p_fg.next_row(), p_bg.next_row();
  }

}

void WebSafeQuantizer::operator()(pdf::Renderer *out_fg, pdf::Renderer *out_bg, int width, int height,
  int *background_color, bool &has_foreground, bool &has_background, std::ostream &stream)
{
  if (out_fg == out_bg)
  { /* Don't bother to analyze images if they are obviously identical. */
    dummy_quantizer(width, height, background_color, stream);
    has_background = true;
    return;
  }
  stream << "R6 " << width << " " << height << " ";
  output_web_palette(stream);
  pdf::Pixmap bmp_fg(out_fg);
  pdf::Pixmap bmp_bg(out_bg);
  pdf::Pixmap::iterator p_fg = bmp_fg.begin();
  pdf::Pixmap::iterator p_bg = bmp_bg.begin();
  for (int i = 0; i < 3; i++)
    background_color[i] = p_bg[i];
  for (int y = 0; y < height; y++)
  {
    int new_color, color = 0xfff;
    int length = 0;
    for (int x = 0; x < width; x++)
    {
      if (!has_background)
      {
        for (int i = 0; i < 3; i++)
        if (background_color[i] != p_bg[i])
        {
          has_background = true;
          break;
        }
      }
      if (p_fg[0] != p_bg[0] || p_fg[1] != p_bg[1] || p_fg[2] != p_bg[2])
      {
        if (!has_foreground && (p_fg[0] || p_fg[1] || p_fg[2]))
          has_foreground = true;
        new_color = ((p_fg[2] + 1) / 43) + 6 * (((p_fg[1] + 1) / 43) + 6 * ((p_fg[0] + 1) / 43));
      }
      else
        new_color = 0xfff;
      if (color == new_color)
        length++;
      else
      {
        if (length > 0)
          write_uint32(stream, ((uint32_t)color << 20) + length);
        color = new_color;
        length = 1;
      }
      p_fg++, p_bg++;
    }
    p_fg.next_row(), p_bg.next_row();
    write_uint32(stream, ((uint32_t)color << 20) + length);
  }
}

class Rgb18
{
protected:
  int value;
public:
  explicit Rgb18()
  : value(-1)
  { }

  explicit Rgb18(int r, int g, int b)
  : value((r >> 2) | ((g >> 2) << 6) | ((b >> 2) << 12))
  { }

  explicit Rgb18(size_t n)
  : value(n)
  { }

  template <typename tp>
  explicit Rgb18(const tp &components)
  : value((components[0] >> 2) | ((components[1] >> 2) << 6) | ((components[2] >> 2) << 12))
  { }

  int operator [](int i) const
  {
    return
      (((this->value >> (6 * i)) << 2) & 0xff) |
      ((this->value >> (6 * i + 4)) & 3);
  }

  bool operator ==(Rgb18 other) const
  {
    return this->value == other.value;
  }

  operator int () const
  {
    return this->value;
  }

  Rgb18 reduce(int k) const
  {
    int components[3];
    const int n = 1 << 8;
    const int c = (n + k - 1) / k;
    for (int i = 0; i < 3; i++)
    {
      const int m = ((*this)[i] * c) / n;
      components[i] = (n - 1) * m / (c - 1);
    }
    return Rgb18(components);
  }

};

class Run
{
protected:
  Rgb18 color;
  size_t length;
public:
  explicit Run(Rgb18 color)
  : color(color), length(0)
  { }
  explicit Run()
  : color(Rgb18()), length(0)
  { }
  void operator ++(int)
  {
    this->length++;
  }
  bool same_color(Rgb18 other_color) const
  {
    return this->color == other_color;
  }
  Rgb18 get_color() const
  {
    return this->color;
  }
  size_t get_length() const
  {
    return this->length;
  }
};

void DefaultQuantizer::operator()(pdf::Renderer *out_fg, pdf::Renderer *out_bg, int width, int height,
  int *background_color, bool &has_foreground, bool &has_background, std::ostream &stream)
{
  if (out_fg == out_bg)
  { /* Don't bother to analyze images if they are obviously identical. */
    dummy_quantizer(width, height, background_color, stream);
    has_background = true;
    return;
  }
  stream << "R6 " << width << " " << height << " ";
  pdf::Pixmap bmp_fg(out_fg);
  pdf::Pixmap bmp_bg(out_bg);
  pdf::Pixmap::iterator p_fg = bmp_fg.begin();
  pdf::Pixmap::iterator p_bg = bmp_bg.begin();
  size_t color_counter = 0;
  std::bitset<1 << 18> original_colors;
  std::bitset<1 << 18> quantized_colors;
  std::vector<std::vector<Run> > runs(height);
  for (int i = 0; i < 3; i++)
    background_color[i] = p_bg[i];
  for (int y = 0; y < height; y++)
  {
    Run run;
    Rgb18 new_color;
    for (int x = 0; x < width; x++)
    {
      if (!has_background)
      {
        for (int i = 0; i < 3; i++)
        if (background_color[i] != p_bg[i])
        {
          has_background = true;
          break;
        }
      }
      if (p_fg[0] != p_bg[0] || p_fg[1] != p_bg[1] || p_fg[2] != p_bg[2])
      {
        if (!has_foreground && (p_fg[0] || p_fg[1] || p_fg[2]))
          has_foreground = true;
        new_color = Rgb18(p_fg[0], p_fg[1], p_fg[2]);
        if (!original_colors[new_color])
        {
          color_counter++;
          original_colors.set(new_color);
        }
      }
      else
        new_color = Rgb18();
      if (run.same_color(new_color))
        run++;
      else
      {
        if (run.get_length() > 0)
          runs[y].push_back(run);
        run = Run(new_color);
        run++;
      }
      p_fg++, p_bg++;
    }
    p_fg.next_row(), p_bg.next_row();
    if (run.get_length() > 0)
      runs[y].push_back(run);
  }
  /* Find appropriate color palette: */
  int divisor = 4;
  while (color_counter > djvu::max_fg_colors)
  {
    size_t new_color_counter = 0;
    quantized_colors.reset();
    divisor++;
    for (size_t color = 0; color < original_colors.size(); color++)
    {
      if (!original_colors[color])
        continue;
      Rgb18 new_color = Rgb18(color).reduce(divisor);
      if (!quantized_colors[new_color])
      {
        quantized_colors.set(new_color);
        new_color_counter++;
        if (new_color_counter > djvu::max_fg_colors)
          break;
      }
    }
    color_counter = new_color_counter;
  }
  if (divisor == 4)
    quantized_colors = original_colors;
  /* Output the palette: */
  if (color_counter == 0)
  {
    stream << 1 << std::endl << "\xff\xff\xff";
  }
  else
  {
    stream << color_counter << std::endl;
    for (size_t color = 0; color < quantized_colors.size(); color++)
    {
      if (quantized_colors[color])
      {
        Rgb18 rgb18(color);
        unsigned char buffer[3];
        for (int i = 0; i < 3; i++)
          buffer[i] = rgb18[i];
        stream.write(reinterpret_cast<char*>(buffer), 3);
      }
    }
  }
  /* Map colors into color indices: */
  std::map<int, uint32_t> color_map;
  uint32_t last_color_index = 0;
  color_map[-1] = 0xfff;
  if (divisor == 4)
    for (size_t color = 0; color < original_colors.size(); color++)
    {
      if (!original_colors[color])
        continue;
      color_map[color] = last_color_index++;
    }
  else
  {
    std::map<int, uint32_t> quantized_color_map;
    for (size_t color = 0; color < quantized_colors.size(); color++)
    {
      if (!quantized_colors[color])
        continue;
      quantized_color_map[color] = last_color_index++;
    }
    for (size_t color = 0; color < original_colors.size(); color++)
    {
      Rgb18 new_color = Rgb18(color).reduce(divisor);
      color_map[color] = quantized_color_map[new_color];
    }
  }
  /* Output runs: */
  for (int y = 0; y < height; y++)
  {
    const std::vector<Run> line_runs = runs[y];
    for (std::vector<Run>::const_iterator run = line_runs.begin(); run != line_runs.end(); run++)
    {
      uint32_t color_index = color_map[run->get_color()];
      write_uint32(stream, ((uint32_t)color_index << 20) + run->get_length());
    }
  }
}

static void dummy_quantizer(int width, int height, int *background_color, std::ostream &stream)
{
  rle::R4 r4(stream, width, height);
  for (int y = 0; y < height; y++)
    r4.output_run(width);
  background_color[0] = background_color[1] = background_color[2] = 0xff;
}

void DummyQuantizer::operator()(pdf::Renderer *out_fg, pdf::Renderer *out_bg, int width, int height,
  int *background_color, bool &has_foreground, bool &has_background, std::ostream &stream)
{
  dummy_quantizer(width, height, background_color, stream);
}

#if HAVE_GRAPHICSMAGICK

GraphicsMagickQuantizer::GraphicsMagickQuantizer(const Config &config)
: Quantizer(config)
{ 
  static bool initialized = false;
  if (!initialized)
  {
    /* Call to InitializeMagick() is obligatory with GraphicsMagick ≥ 1.3.8.
     */
    Magick::InitializeMagick("");
    initialized = true;
  }
}

void GraphicsMagickQuantizer::operator()(pdf::Renderer *out_fg, pdf::Renderer *out_bg, int width, int height,
  int *background_color, bool &has_foreground, bool &has_background, std::ostream &stream)
{
  if (out_fg == out_bg)
  { /* Don't bother to analyze images if they are obviously identical. */
    dummy_quantizer(width, height, background_color, stream);
    has_background = true;
    return;
  }
  stream << "R6 " << width << " " << height << " ";
  Magick::Image image(Magick::Geometry(width, height), Magick::Color());
  image.type(Magick::TrueColorMatteType);
  image.modifyImage();
  pdf::Pixmap bmp_fg(out_fg);
  pdf::Pixmap bmp_bg(out_bg);
  pdf::Pixmap::iterator p_fg = bmp_fg.begin();
  pdf::Pixmap::iterator p_bg = bmp_bg.begin();
  for (int i = 0; i < 3; i++)
    background_color[i] = p_bg[i];
  for (int y = 0; y < height; y++)
  {
    Magick::PixelPacket* ipixel = image.setPixels(0, y, width, 1);
    for (int x = 0; x < width; x++)
    {
      if (!has_background)
      {
        for (int i = 0; i < 3; i++)
        if (background_color[i] != p_bg[i])
        {
          has_background = true;
          break;
        }
      }
      if (p_fg[0] != p_bg[0] || p_fg[1] != p_bg[1] || p_fg[2] != p_bg[2])
      {
        if (!has_foreground && (p_fg[0] || p_fg[1] || p_fg[2]))
          has_foreground = true;
        *ipixel = Magick::Color(p_fg[0], p_fg[1], p_fg[2], 0);
      }
      else
        *ipixel = Magick::Color(0, 0, 0, 0xff);
      p_fg++, p_bg++, ipixel++;
    }
    p_fg.next_row(), p_bg.next_row(), image.syncPixels();
  }
  image.quantizeColorSpace(Magick::TransparentColorspace);
  image.quantizeColors(this->config.fg_colors);
  image.quantize();
  image.colorSpace(Magick::RGBColorspace);
  image.quantizeColorSpace(Magick::RGBColorspace);
  image.quantizeColors(9999);
  image.quantize();
  unsigned int n_colors = image.colorMapSize();
  stream << n_colors << std::endl;
  for (unsigned int i = 0; i < n_colors; i++)
  {
    const Magick::Color &color = image.colorMap(i);
    char buffer[] = { color.redQuantum(), color.greenQuantum(), color.blueQuantum() };
    stream.write(buffer, 3);
  }
  for (int y = 0; y < height; y++)
  {
    int new_color, color = 0xfff;
    Magick::PixelPacket *ipixel = image.getPixels(0, y, width, 1);
    Magick::IndexPacket *ppixel = image.getIndexes();
    int length = 0;
    for (int x = 0; x < width; x++)
    {
      if (ipixel->opacity != TransparentOpacity)
        new_color = *ppixel;
      else
        new_color = 0xfff;
      if (color == new_color)
        length++;
      else
      {
        if (length > 0)
          write_uint32(stream, ((uint32_t)color << 20) + length);
        color = new_color;
        length = 1;
      }
      ipixel++, ppixel++;
    }
    write_uint32(stream, ((uint32_t)color << 20) + length);
  }
}

#else

GraphicsMagickQuantizer::GraphicsMagickQuantizer(const Config &config)
: Quantizer(config)
{
  throw NotImplementedError();
}

void GraphicsMagickQuantizer::operator()(pdf::Renderer *out_fg, pdf::Renderer *out_bg, int width, int height,
  int *background_color, bool &has_foreground, bool &has_background, std::ostream &stream)
{ /* just to satisfy compilers */ }

#endif

// vim:ts=2 sw=2 et