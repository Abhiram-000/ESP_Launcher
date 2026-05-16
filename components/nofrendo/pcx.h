#ifndef _PCX_H_
#define _PCX_H_

#include "osd.h"
#include "bitmap.h"
 
typedef struct pcxheader_s
{
   uint8 Manufacturer;
   uint8 Version;
   uint8 Encoding;
   uint8 BitsPerPixel;
   uint16 Xmin;
   uint16 Ymin;
   uint16 Xmax;
   uint16 Ymax;
   uint16 HDpi;
   uint16 VDpi;
   uint8 Colormap[48];
   uint8 Reserved;
   uint8 NPlanes;
   uint16 BytesPerLine;
   uint16 PaletteInfo;
   uint16 HscreenSize;
   uint16 VscreenSize;
   uint8 Filler[54];
} __attribute__((packed)) pcxheader_t;

extern int pcx_write(char *filename, bitmap_t *bmp, rgb_t *pal);

#endif
