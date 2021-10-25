// these magic numbers are fixed to the beginning/end of the file.
typedef struct magic {
  char *suffix, *header, *footer;
  int  hdr_len, ftr_len;
  long hdr_pos; /* TODO: add footer pos */
  char has_footer;
} magic;

/*
  similar suffixes must be next to each other
    source: https://github.com/korczis/foremost/blob/master/state.c
    source: https://www.garykessler.net/library/file_sigs.html
*/
static magic magics[] = {
  {"jpg", "\xff\xd8\xff\xe0\x00\x10JFIF\x00", "\xff\xd9", 11, 2, 0},
  {"jpg", "\xff\xd8\xff\xe1", "\xff\xd9", 4, 2, 0}, // EXIF
  {"gif", "\x47\x49\x46\x38\x37\x61", "\x00\x3b", 6, 2, 0},
  {"gif", "\x47\x49\x46\x38\x39\x61", "\x00\x3b", 6, 2, 0},
  {"tif", "\x49\x20\x49", NULL, 3, 0, 0},
  {"tif", "\x49\x49\x2A\x00", NULL, 4, 0, 0},
  {"bmp", "BM", NULL, 2, 0, 0},
  {"mp3", "\x49\x44\x33", NULL, 3, 0, 0},
  {"mp4", "ftypisom", NULL, 8, 0, 4},
  {"exe", "MZ", NULL, 2, 0, 0},
  {"elf", "\x7f""ELF", NULL, 4, 0, 0},
  {"reg", "regf", NULL, 4, 0, 0},
  {"mpg", "\x00\x00\x01\xba", "\x00\x00\x01\xb9", 4, 4, 0},
  {"wmv", "\x30\x26\xB2\x75\x8E\x66\xCF\x11", "\xA1\xDC\xAB\x8C\x47\xA9",
    8, 6, 0},
  {"avi", "RIFF", "INFO", 4, 4, 0},
  {"rif", "RIFF", "INFO", 4, 4, 0},
  {"wav", "RIFF", "INFO", 4, 4, 0},
  {"htm", "<?html", "</html>", 5, 7, 0},
  {"xml", "<?xml", NULL, 5, 0, 0},
  {"ole", "\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1\x00\x00\x00\x00\x00\x00\x00\x00",
    NULL, 16, 0, 0},
  {"doc", "\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1\x00\x00\x00\x00\x00\x00\x00\x00",
    NULL, 16, 0, 0},
  {"xls", "\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1\x00\x00\x00\x00\x00\x00\x00\x00",
    NULL, 16, 0, 0},
  {"ppt", "\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1\x00\x00\x00\x00\x00\x00\x00\x00",
    NULL, 16, 0, 0},
  {"7z", "\x37\x7A\xBC\xAF\x27\x1C", NULL, 6, 0, 0},
  {"zip", "\x50\x4B\x03\x04", "\x50\x4b\x05\x06", 4, 4, 0},
  {"zip", "\x50\x4B\x05\x04", "\x50\x4b\x05\x06", 4, 4, 0},
  {"zip", "\x50\x4B\x07\x04", "\x50\x4b\x05\x06", 4, 4, 0},
  {"rar", "\x52\x61\x72\x21\x1A\x07\x00", "\x00\x00\x00\x00\x00\x00\x00\x00",
    7, 8, 0},
  {"rar", "\x52\x61\x72\x21\x1A\x07\x01\x00",
    "\x00\x00\x00\x00\x00\x00\x00\x00", 8, 8, 0},
  {"sxw", "\x50\x4B\x03\x04", "\x4b\x05\x06\x00", 4, 4, 0},
  {"sxc", "\x50\x4B\x03\x04", "\x4b\x05\x06\x00", 4, 4, 0},
  {"sxi", "\x50\x4B\x03\x04", "\x4b\x05\x06\x00", 4, 4, 0},
  {"docx", "\x50\x4B\x03\x04", "\x4b\x05\x06\x00", 4, 4, 0},
  {"pptx", "\x50\x4B\x03\x04", "\x4b\x05\x06\x00", 4, 4, 0},
  {"xlsx", "\x50\x4B\x03\x04", "\x4b\x05\x06\x00", 4, 4, 0},
  {"gz", "\x1F\x8B\x08", "\x00\x00\x00\x00", 3, 4, 0},
  {"pdf", "%PDF", "\x0A%%EOF", 4, 6, 0},
  {"pdf", "%PDF", "\x0A%%EOF\x0A", 4, 7, 0},
  {"pdf", "%PDF", "\x0D\x0A%%EOF\x0D\x0A", 4, 9, 0},
  {"pdf", "%PDF", "\x0D%%EOF\x0D", 4, 7, 0},
  {"mov", "pnot", NULL, 4, 0, 0},
  {"mov", "moov", NULL, 4, 0, 0},
  {"wpd", "\xff\x57\x50\x43", NULL, 4, 0, 0},
  {"c", "#include", NULL, 8, 0, 0},
  {"png", "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A", "IEND", 8, 4, 0},
  {"mach-o", "\xCA\xFE\xBA\xBE", NULL, 4, 0, 0},
  {"mach-o", "\xCF\xFA\xED\xFE", NULL, 4, 0, 0},
  {"mach-o", "\xCF\xFA\xED\xFF", NULL, 4, 0, 0},
  {"mach-o", "\xFE\xED\xFA\xCE", NULL, 4, 0, 0},
  {"mach-o", "\xFE\xED\xFA\xCE", NULL, 4, 0, 0x1000},
  {"mach-o", "\xFE\xED\xFA\xCF", NULL, 4, 0, 0},
  {"mach-o", "\xFE\xED\xFA\xCF", NULL, 4, 0, 0x1000},
  {"flac","\x66\x4C\x61\x43", NULL, 4, 0, 0},
  {"dmg", "koly", NULL, 4, 0, 0},
  {"pa30", "\x50\x41\x33\x30", NULL, 4, 0, 0},
  {"pcap", "\xA1\xB2\xC3\xD4", NULL, 4, 0, 0},
  {"pcap", "\xD4\xC3\xB2\xA1", NULL, 4, 0, 0},
  {"pcap", "\xA1\xB2\x3C\x4D", NULL, 4, 0, 0},
  {"pcap", "\x4D\x3C\xB2\xA1", NULL, 4, 0, 0},
  {"pcapng", "\x0A\x0D\x0D\x0A", NULL, 4, 0, 0},
  {"vbe", "\x23\x40\x7E\x5E", NULL, 4, 0, 0},
  {"psd", "\x38\x42\x50\x53", NULL, 4, 0, 0},
  {"ttf", "\x00\x01\x00\x00\x00", NULL, 5, 0, 0},
  {"swf", "\x46\x57\x53", NULL, 3, 0, 0},
  {"py", "#!/usr/bin/python", NULL, 17, 0, 0},
  {"pl", "#!/usr/bin/perl", NULL, 15, 0, 0},
  {"sh", "#!/bin/sh", NULL, 9, 0, 0},
  {"sh", "#!/bin/bash", NULL, 11, 0, 0},
  {"wasm", "\x00\x61\x73\x6D\x01\x00\x00\x00", NULL, 8, 0, 0},
  {"tar", "\x75\x73\x74\x61\x72\x00\x30\x30", NULL, 8, 0, 257},
  {"tar", "\x75\x73\x74\x61\x72\x20\x20\x00", NULL, 8, 0, 257},
  {"iso", "CD001", NULL, 5, 0, 0x8001},
  {"iso", "CD001", NULL, 5, 0, 0x8801},
  {"iso", "CD001", NULL, 5, 0, 0x9001},
  {NULL, NULL, NULL, 0, 0}
};

static magic find_magic(const char *s, long size)
{
  int i = 0;
  while (magics[i].suffix != NULL) {
    magic m = magics[i];
    if (m.hdr_len > 0 && size > m.hdr_len && size > m.hdr_pos)
      if (memcmp(s+m.hdr_pos, m.header, m.hdr_len) == 0)
        break;
    i++;
  }

  // let's look for the correct footer if there's any
  if (
    magics[i].suffix != NULL &&
    strcmp(magics[i].suffix, magics[i+1].suffix) == 0
  ) { // there are more?
    int j = i;
    while (magics[j].suffix != NULL) {
      magic m = magics[j];
      if (m.footer == NULL || strcmp(m.suffix, magics[i].suffix) > 0) {
        j++;
        continue;
      }

      if (memcmp(s+size-m.ftr_len, m.footer, m.ftr_len) == 0 &&
          memcmp(s+m.hdr_pos, m.header, m.hdr_len) == 0) {
        i = j;
        break;
      }
      j++;
    }
  }

  // has footer?
  magic m = magics[i];
  m.has_footer = ( memcmp(s+size-m.ftr_len, m.footer, m.ftr_len) == 0 );
  return m;
}

