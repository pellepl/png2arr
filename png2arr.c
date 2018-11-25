#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <png.h>
#include <errno.h>
#include <string.h>

static int i_width, i_height;
static png_structp png;
static png_byte color_type;
static png_byte bit_depth;
static png_bytep *row_pointers;
static png_infop info;
static FILE *out = NULL;
static int *width_infos = NULL;

static char infile[1024];
static char outfile[1024];
static char arrname[256];
static int d_width;
static int d_height;
static int d_tilew;
static int d_tileh;
static int d_offsx;
static int d_offsy;
static int min_width;
static int d_inv = 0;

static int read_png_file(const char *filename) {
  int res = 0;
  FILE *fp = fopen(filename, "rb");
  if (fp == NULL) return errno;

  png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png) {res = -1; goto end;}

  info = png_create_info_struct(png);
  if (!info) {res = -1; goto end;}

  if (setjmp(png_jmpbuf(png))) {res = -1; goto end;}

  png_init_io(png, fp);

  png_read_info(png, info);

  i_width      = png_get_image_width(png, info);
  i_height     = png_get_image_height(png, info);
  color_type = png_get_color_type(png, info);
  bit_depth  = png_get_bit_depth(png, info);

  // Read any color_type into 8bit depth, RGBA format.
  // See http://www.libpng.org/pub/png/libpng-manual.txt

  if(bit_depth == 16)
    png_set_strip_16(png);

  if(color_type == PNG_COLOR_TYPE_PALETTE)
    png_set_palette_to_rgb(png);

  // PNG_COLOR_TYPE_GRAY_ALPHA is always 8 or 16bit depth.
  if(color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
    png_set_expand_gray_1_2_4_to_8(png);

  if(png_get_valid(png, info, PNG_INFO_tRNS))
    png_set_tRNS_to_alpha(png);

  // These color_type don't have an alpha channel then fill it with 0xff.
  if(color_type == PNG_COLOR_TYPE_RGB ||
     color_type == PNG_COLOR_TYPE_GRAY ||
     color_type == PNG_COLOR_TYPE_PALETTE)
    png_set_filler(png, 0xFF, PNG_FILLER_AFTER);

  if(color_type == PNG_COLOR_TYPE_GRAY ||
     color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
    png_set_gray_to_rgb(png);

  png_read_update_info(png, info);

  row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * i_height);
  for (int y = 0; y < i_height; y++) {
    row_pointers[y] = (png_byte*)malloc(png_get_rowbytes(png,info));
  }

  png_read_image(png, row_pointers);

end:
  fclose(fp);

  return res;
}


int min_mark_x, max_mark_x;

static void reset_width_info(void) {
  min_mark_x = -1; max_mark_x = -1;
}

static void handle_width_info(int glyph) {
  if (min_width < 0) return;
  if (min_mark_x == -1) {
    width_infos[glyph] = min_width;
  } else {
    int w = max_mark_x - min_mark_x + 1;
    width_infos[glyph] = w < min_width ? min_width : w;
  }
}

static int is_marked(int x, int y) {
  if (x >= i_width || y >= i_height) return d_inv ? 1 : 0;
  png_bytep row = row_pointers[y];
  png_bytep px = &(row[x * 4]);
  if (px[0] != 0xff && px[3] > 0) { //Rgba
    if (min_mark_x == -1) min_mark_x = x;
    max_mark_x = x;
    return d_inv ? 0 : 1;
  }
  return d_inv ? 1 : 0;
}

// SSD1306 processing
static void process_tile(int x, int y, int w, int h) {
  int vals = 0;
  for (int py = y; py < y+h; py+=8) {
  for (int px = x; px < x+w; px++) {

  uint8_t c = 0;
  for (int ppy = py+7; ppy >= py; ppy--) {
    if (is_marked(px, ppy)) {
      c = (c << 1) | 1;
    } else {
      c <<= 1;
    }
  }
  if (vals) fprintf(out, ",");
  if ((vals & 0xf) == 0) fprintf(out, "\n  ");
  vals++;
  fprintf(out, "0x%02x", c);

  }
  }
}

static int process_tile_size(int w, int h) {
  return (w * ((h+7)/8));
}

static int process_png_file(void) {
  fprintf(out, "/* image '%s' %dx%d */\n", infile, i_width, i_height);
  int tiles_x = (d_width - d_offsx + d_tilew -1) / d_tilew;
  int tiles_y = (d_height - d_offsy + d_tileh -1) / d_tileh;
  if (tiles_x * tiles_y > 1) {
    fprintf(out, "/* %d x %d tiles, %dx%d pixels each */\n", tiles_x, tiles_y, d_tilew, d_tileh);
    fprintf(out, "const unsigned char %s[%d*%d][%d] = {",arrname, tiles_x, tiles_y, process_tile_size(d_tilew, d_tileh));
  } else {
    fprintf(out, "const unsigned char %s[] = ", arrname);
  }
  if (min_width >= 0) {
    int tiles = tiles_x * tiles_y;
    width_infos = malloc(sizeof(int) * (tiles <= 0 ? 1 : tiles));
  }
  int glyph = 0;
  for (int y = d_offsy; y < d_height; y += d_tileh) {
  for (int x = d_offsx; x < d_width; x += d_tilew) {
    if (glyph && tiles_x * tiles_y > 1) fprintf(out, ",\n  ");
    reset_width_info();
    fprintf(out, "{");
    process_tile(x, y, d_tilew, d_tileh);
    fprintf(out, "  }");
    handle_width_info(glyph);
    glyph++;
  }
  }
  if (tiles_x * tiles_y > 1) fprintf(out, "}");
  fprintf(out, ";\n");
  return 0;
}

static int process_widths(void) {
  int tiles_x = (d_width - d_offsx + d_tilew -1) / d_tilew;
  int tiles_y = (d_height - d_offsy + d_tileh -1) / d_tileh;
  int glyphs = tiles_x * tiles_y;
  fprintf(out, "const unsigned char %s_widths[%d] = {", arrname, glyphs);
  for (int i = 0; i < glyphs; i++) {
    if ((i & 0xf) == 0) fprintf(out, "\n  ");
    fprintf(out, "%4d,", width_infos[i]);
  }
  fprintf(out, "};\n");
  return 0;
}

static void help(void) {
  printf("  -i input-file               Input PNG file\n");
  printf("  -o output-file              Output C file\n");
  printf("     Will output to stdout if omitted\n");
  printf("  -n array-name               C array name\n");
  printf("  -s <width>x<height>         Image dimensions\n");
  printf("     Image size is used if omitted\n");
  printf("  -d <width>x<height>         Tile/glyph dimensions\n");
  printf("     Image size is used if omitted\n");
  printf("  -f <offset x>,<offset y>    Image offset\n");
  printf("     Offset 0,0 if omitted\n");
  printf("  -w <min width>              Output width info\n");
  printf("  -inv                        Invert colors\n");
}

static void set_defaults(void) {
  strcpy(arrname, "__data");
  strcpy(infile, "in.png");
  memset(outfile, 0, sizeof(outfile));
  d_width = 0;
  d_height = 0;
  d_tilew = 0;
  d_tileh = 0;
  d_offsx = 0;
  d_offsy = 0;
  min_width = -1;
}

static int read_params(int argc, char *argv[]) {
  int a = 1;
  while (a < argc) {
    if (strcmp("-i", argv[a])==0 && a+1 < argc) {
      a++;
      strcpy(infile, argv[a]);
    }
    else if (strcmp("-o", argv[a])==0 && a+1 < argc) {
      a++;
      strcpy(outfile, argv[a]);
    }
    else if (strcmp("-s", argv[a])==0 && a+1 < argc) {
      a++;
      int res = sscanf(argv[a], "%dx%d", &d_width, &d_height);
      if (res != 2) return EINVAL;
    }
    else if (strcmp("-d", argv[a])==0 && a+1 < argc) {
      a++;
      int res = sscanf(argv[a], "%dx%d", &d_tilew, &d_tileh);
      if (res != 2) return EINVAL;
    }
    else if (strcmp("-f", argv[a])==0 && a+1 < argc) {
      a++;
      int res = sscanf(argv[a], "%d,%d", &d_offsx, &d_offsy);
      if (res != 2) return EINVAL;
    }
    else if (strcmp("-n", argv[a])==0 && a+1 < argc) {
      a++;
      strcpy(arrname, argv[a]);
    }
    else if (strcmp("-w", argv[a])==0 && a+1 < argc) {
      a++;
      int res = sscanf(argv[a], "%d", &min_width);
      if (res != 1) return EINVAL;
    }
    else if (strcmp("-inv", argv[a])==0) {
      d_inv = 1;
    } else {
      return EINVAL;
    }
    a++;
  }
  return 0;
}

int main(int argc, char *argv[]) {
  int res;
  set_defaults();
  res = read_params(argc, argv);
  if (res) goto end;
  res = read_png_file(infile);
  if (res) goto end;
  if (strlen(outfile)) {
    out = fopen(outfile, "w");
  } else {
    out = stdout;
  }
  if (out == NULL) { res = errno; goto end; }
  if (d_width == 0 || d_height == 0) {
    d_width = i_width;
    d_height = i_height;
  }
  if (d_tilew == 0 || d_tileh == 0) {
    d_tilew = i_width;
    d_tileh = i_height;
  }
  res = process_png_file();
  if (res) goto end;
  if (min_width >= 0) res = process_widths();

end:
  png_destroy_read_struct(&png, NULL, NULL);
  if (min_width >= 0) free(width_infos);
  if (out) fclose(out);
  png = NULL;
  info = NULL;
  if (res) {
    printf("error %d (%s)\n", res, strerror(res));
    help();
  }
  return res;
}
