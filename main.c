#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "magic.h"
#include "font.h"

#define TO_SDL_COLOR(c)  ((SDL_Color){c>>16,c>>8 & 0xff,c&0xff})

enum {
  CMD_NONE = 0,
  CMD_INPUT,
  CMD_GO
};

struct Theme {
  unsigned int bgcolor, fgcolor, ngcolor, mgcolor;
  int   font_size;
  char *font_name;
} theme = { // customize
  .bgcolor   = 0x151515, // background
  .fgcolor   = 0xDFDFDF, // foreground
  .ngcolor   = 0xDE8972, // special
  .mgcolor   = 0x71C6DE, // magic
  .font_size = 11
};

struct Win {
  int width, height;
  SDL_Rect offsetcol;
  SDL_Rect content;
  SDL_Rect asciicol;
  SDL_Rect infobar;
  int rows, cols, colsize;
  int amount;
  int curpos;
  int font_width, font_height;
} win = {
  .rows   = 16,
  .cols   = 4,
  .curpos = 0
};

struct Doc {
  char *filepath;
  int   fd;
  long  fsize, fpos, foff;
  int ro;
  char *fdmem;
  // format magic
  magic magic;
  char  has_footer;
} doc = {
  .fd    = -1,
  .ro    = -1,
  .magic = {NULL}
};

SDL_Window *window;
SDL_Surface *screen;
SDL_Renderer *renderer;
TTF_Font *font;

static char dot[8] = {0x00, 0x00, 0x18, 0x3c, 0x3c, 0x18, 0x00, 0x00};

static int dragging, drag_mx, drag_my;
static int currcmd;
static int spaces;
static char input[4];

static void get_font_width(void);
static long input_to_long(char *s);
static int isasciihex(char c);
static void toasciihex(unsigned char s, char *d);
static void off_toasciihex(int off, char *d);
static char toprintable(char s);
static void go(long off);
static void quit(int code, const char *m);
static void copy_bytes(int size);
static void mouse_set_cursor(int x, int y);
static void grab_input(char c);
static void draw_background(void);
static void draw_text(char *s, int x, int y, unsigned int color);
static void draw_cursor(int x, int y, unsigned int b, unsigned int f, int size);
static void draw_infobar(void);
static void draw_offsetcol(void);
static void draw_ascii(char s, int x, int row, char cursor, char special);
static void init_content(void);
static void show_content(void);
static void drag_window(void);
static void running(void);

static void get_font_width(void) /* just using uppercase letters */
{
  char table[27];
  int fontw, fonth;

  for (int i = 0x41;i < 0x5b;i++)
    table[i-0x41] = i;
  table[26] = '\0';
  TTF_SizeText(font, table, &fontw, &fonth);
  win.font_width  = fontw/25;
  win.font_height = fonth;
  // SDL suggests adding a pixel each way but no
  //win.font_width++; win.font_height++;
}

static int isasciihex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return (c - 'a') + 0xa;
  return -1;
}
static long input_to_long(char *s) {
  long r = 0;
  for (int i = 0, n = 0; i < 4; i++) {
    if (input[i] == 0) continue;
    r += isasciihex(input[i]) << (n++*4);
  }
  return r;
}
static void toasciihex(unsigned char s, char *d) { snprintf(d, 3, "%02X", s); }
static void off_toasciihex(int off, char *d) { snprintf(d, 5,"%04X", off); }
static char toprintable(char s) { return (s < 0x20 || s > 0x7e) ? '.' : s; }
static void go(long off)
{
  long base = off - (off%win.amount);
  if (base >= 0 && base < doc.fsize) {
    doc.fpos   = base;
    win.curpos = (off > win.amount ? off%win.amount : off);
  }
}

static void quit(int code, const char *m)
{
  const char *err = SDL_GetError();

  if (code > 0) {
    if (*err == '\0') {
      perror(m == NULL ? "error" : m);
    } else {
      fprintf(stderr, "SDL error: %s\n", err);
    }
  }
  if (font != NULL) TTF_CloseFont(font);
  if (renderer != NULL) SDL_DestroyRenderer(renderer);
  if (window != NULL) SDL_DestroyWindow(window);
  if (doc.fd != -1) {
    munmap(doc.fdmem, doc.fsize);
    close(doc.fd);
  }
  exit(code);
}

static void copy_bytes(int size) /* TODO: `X` should ask for amount of bytes */
{
  int len = (size*4)+1;
  char *hex = malloc(len);
  unsigned char *ptr = (unsigned char *)(doc.fdmem + doc.fpos + win.curpos);

  if (hex == NULL) {
    perror("malloc"); return;
  }
  memset(hex, '\0', len);

  for (int i = 0;i < size; i++) {
    char tmp[5];
    snprintf(tmp, 5, "\\x%02X", *(ptr+i));
    strcat(hex, tmp);
  }
  int r = SDL_SetClipboardText(hex);
  if (r == -1)
    fprintf(stderr, "SDL error: %s\n", SDL_GetError());
}

static void mouse_set_cursor(int x, int y)
{
  int clickx = x - win.content.x,
      clicky = y - win.content.y;

  if (clickx < 0 || clicky < 0) return;
  if (clickx > win.content.w || clicky > (win.content.h - win.infobar.h))
    return;

  int posx = clickx / win.font_width;
  int posy = clicky / win.font_height;
  int blank_columns = posx/9;

  if ((posx+1)/9 > blank_columns) return;
  posx -= blank_columns;

  posy = posy/2;
  posy = win.colsize*posy;

  long final = doc.fpos + (long)(posx/2 + posy);
  if (final >= doc.fsize) return;
  go(final);
}

static void grab_input(char c)
{
  char clean = 0;
  if (isasciihex(c) == -1) {
    clean = 1;
  } else {
    if (currcmd == CMD_INPUT) {
      if (input[1] == 0) {
        input[1] = c;
      } else {
        input[0] = c;
        *(doc.fdmem + (doc.fpos + win.curpos)) =
          (unsigned char)input_to_long(input);
        clean = 1;
      }
    }
    else if (currcmd == CMD_GO) {
      int i = 3;
      for (; i >= 0; i--) {
        if (input[i] != 0) continue;
        input[i] = c;
        break;
      }

      if (i == 0) {
        go(input_to_long(input));
        clean = 1;
      }
    }
  }

  if (clean) {
    memset(&input, 0, 4);
    currcmd = CMD_NONE;
  }
}

static void draw_bitmap(char *s, int x, int y, unsigned int f)
{
  SDL_Color c = TO_SDL_COLOR(f);
  SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, SDL_ALPHA_OPAQUE);
  for (int i = 0; i<sizeof(s); i++, y++) {
    char c = s[i];
    for (int j = 0; j<8; j++) {
      if ((c>>j)&1)
        SDL_RenderDrawPoint(renderer, x + j, y);
    }
  }
}

static void draw_background(void)
{
  SDL_Color bg = TO_SDL_COLOR(theme.bgcolor);
  SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, SDL_ALPHA_OPAQUE);
}

static void draw_text(char *s, int x, int y, unsigned int color)
{
  SDL_Surface *text_surface;
  SDL_Texture *text_texture;
  SDL_Rect text_rect;
  int line_height, line_width;

  text_surface = TTF_RenderText_Blended(
    font, s, TO_SDL_COLOR(color)
  );
  TTF_SizeText(font, s,&line_width, &line_height);
  text_texture = SDL_CreateTextureFromSurface(renderer, text_surface);
  SDL_FreeSurface(text_surface);
  text_rect = (SDL_Rect){
    x, y, line_width, line_height
  };

  SDL_RenderCopy(renderer, text_texture, 0, &text_rect);
  SDL_DestroyTexture(text_texture);
}

static void draw_cursor(int x, int y, unsigned int b, unsigned int f, int size)
{
  SDL_Color bg = TO_SDL_COLOR(b);
  SDL_Rect cursor = {
    x - 1, y, win.font_width * size, win.font_height+1
  };

  SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, SDL_ALPHA_OPAQUE);
  SDL_RenderFillRect(renderer, &cursor);
}

static void draw_infobar(void)
{
  // print cmd
  if (currcmd) {
    switch(currcmd) {
      case CMD_GO:
        draw_text(">", win.infobar.x, win.infobar.y, theme.ngcolor);
        break;
      case CMD_INPUT:
        draw_text("&", win.infobar.x, win.infobar.y, theme.ngcolor);
        break;
    }
    for (int i = 0, n = 3; i < 4 ; n--,i++) {
      if (input[i] == 0) continue;
      char c[2];
      sprintf(c, "%c", input[i]);
      draw_text(c, win.infobar.x + (win.font_width*(n+1)), win.infobar.y,
        theme.ngcolor);
    }
  } else {
    draw_text((doc.magic.suffix != NULL ? doc.magic.suffix : "*")
      , win.infobar.x, win.infobar.y, theme.mgcolor);
  }


  // print doc info
  char offstr[5];
  long offset = doc.fpos + win.curpos;
  draw_text((doc.ro ? "ro" : "rw"), win.infobar.w-(win.font_width*6),
    win.infobar.y, theme.ngcolor);
  off_toasciihex(offset, offstr);
  draw_text(offstr, win.infobar.w-(win.font_width*3), win.infobar.y,
    theme.fgcolor);
  for (int i = 0;offset > 0xffff; offset -= 0xffff, i++) {
    draw_bitmap(dot,
      win.infobar.x + win.infobar.w - win.font_width - sizeof(dot)*i,
      win.infobar.y + win.font_width, theme.ngcolor);
  }
}

static void draw_offsetcol(void)
{
  int posx, posy;
  posy = win.offsetcol.y;
  posx = win.offsetcol.x;

  long pos = doc.fpos - (doc.fpos % win.colsize);
  long posend = pos + win.colsize * win.rows;
  long cpos = doc.fpos + win.curpos;
  for (int col = 0;pos != posend; col++, pos+=(win.colsize)) {
    char offstr[5];
    off_toasciihex(pos, offstr);

    if (col > 0) posy += win.font_height * 2;
    if (cpos-(cpos%win.colsize) == pos) {
      if (doc.magic.suffix != NULL &&
          (doc.magic.hdr_pos-(doc.magic.hdr_pos%win.colsize)) == pos){
        draw_cursor(posx, posy-1, theme.mgcolor, theme.bgcolor, 4);
      } else {
        draw_cursor(posx, posy-1, theme.ngcolor, theme.bgcolor, 4);
      }
      draw_text(offstr, posx, posy, theme.bgcolor);
    } else {
      draw_text(offstr, posx, posy, theme.ngcolor);
    }
  }
}

static void draw_ascii(char s, int x, int row, char cursor, char special)
{
  int posy = 0;
  char ascii[2] = {toprintable(s), '\0'};

  if (row > 0)
    posy += (win.font_height * row)*2;
  if (cursor)
    draw_cursor(win.asciicol.x + (win.font_width * x), win.asciicol.y + posy-1,
      (special ? theme.mgcolor:theme.ngcolor), theme.bgcolor, 1);

  unsigned int color = theme.ngcolor;
  if (cursor) {
    color = theme.bgcolor;
  } else if (special) {
    color = theme.mgcolor;
  }
  draw_text(ascii, win.asciicol.x + (win.font_width * x),
    win.asciicol.y + posy, color);
}

static void init_content(void)
{
  struct stat st;

  doc.ro = ( access(doc.filepath, W_OK) == -1 );
  doc.fd = open(doc.filepath, (!doc.ro ? O_RDWR : O_RDONLY), 0755);
  if (doc.fd == -1) quit(1, "open");
  stat(doc.filepath, &st);
  doc.fsize = st.st_size;
  doc.fdmem = (char*)mmap(0, doc.fsize,
    (!doc.ro ? PROT_READ|PROT_WRITE : PROT_READ),
    MAP_SHARED|MAP_FILE, doc.fd, 0);
  if (doc.fdmem == MAP_FAILED) quit(1, "mmap");

  doc.foff = doc.fpos = 0;
  if (doc.fsize > 0)
    doc.magic = find_magic(doc.fdmem, doc.fsize);
}

static void show_content(void)
{
  char *docptr = (char*)doc.fdmem;
  long displaypos = doc.fpos;
  int posx = win.content.x, posy = win.content.y;

  SDL_RenderClear(renderer);
  for (int n = 0, nb = 0, r = 0; n < win.amount; n++) {
    char hex[3];
    unsigned char data = docptr[displaypos];
    if (displaypos < doc.fsize) {
      char special = 0;
      toasciihex(data, hex);

      special = (
        // header
        ( displaypos >= doc.magic.hdr_pos
        && displaypos < (doc.magic.hdr_pos+doc.magic.hdr_len) )
        ||
        // footer
        ((doc.fsize - doc.magic.ftr_len) <= displaypos && doc.magic.has_footer)
      );

      if (n == win.curpos) { // cursor
        draw_cursor(posx, posy-1, (special ? theme.mgcolor:theme.ngcolor),
          theme.bgcolor, 2);
        draw_text(hex, posx, posy, theme.bgcolor);
      } else {
        draw_text(hex, posx, posy, (special ? theme.mgcolor:theme.fgcolor));
      }
      draw_ascii(data, nb, r, (n == win.curpos), special);
      nb++; displaypos++;
    }
    posx += win.font_width * 2;
    if (n > 0) {
      if (nb % win.colsize == 0 || displaypos == doc.fsize) { // eol
        posy += win.font_height * 2;
        posx = win.content.x;
        nb = 0;
        r++;
        if (displaypos == doc.fsize) { displaypos++; nb++; }
      } else {
        if (nb%4 == 0) { // space
          draw_text(" ", posx, posy, theme.fgcolor);
          posx += win.font_width;
        }
      }
    }
  }

  draw_offsetcol();
  draw_infobar();
  draw_background();
  SDL_RenderPresent(renderer);
}

static void drag_window(void)
{
  int mousex, mousey;
  SDL_GetGlobalMouseState(&mousex, &mousey);
  SDL_SetWindowPosition(window, (mousex - drag_mx), (mousey - drag_my));
}

static void running(void)
{
  SDL_Event e;

  while (1){
    show_content();
    while (SDL_PollEvent(&e)){
      if (e.type == SDL_QUIT){
        quit(0, NULL);
      }
      SDL_Keymod mod = SDL_GetModState();

      // drag window
      if (e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEBUTTONDOWN) {
        if (e.button.state == SDL_PRESSED) {
          drag_mx  = e.button.x;
          drag_my  = e.button.y;
          dragging = 1;
        } else {
          if (dragging)
            mouse_set_cursor(e.button.x, e.button.y);
          dragging = 0;
        }
      }
      if (e.type == SDL_MOUSEMOTION && dragging)
        drag_window();

      SDL_Keysym ksym = e.key.keysym;
      // key navigation
      if (e.type == SDL_KEYDOWN) {
        int newcurpos = win.curpos;

        if (!doc.ro) {
          unsigned char *pos = (unsigned char *)(
            doc.fdmem + (doc.fpos + win.curpos)
          );
          if (ksym.sym == SDLK_EQUALS || ksym.sym == SDLK_MINUS) {
            unsigned char value = *pos;
            if (ksym.sym == SDLK_EQUALS) {
              value++;
            } else if (ksym.sym == SDLK_MINUS) {
              value--;
            }
            *pos = value;
          }

          if (ksym.sym == SDLK_n) { // NOP
            if (*pos != 0x90)
              *pos = 0x90;
          }
        }

        if (ksym.sym == SDLK_LEFT) {
          if (newcurpos == 0 && doc.fpos - (win.amount-1) >= 0) {
            newcurpos = win.amount - 1;
            doc.fpos -= win.amount;
          } else {
            if (doc.fpos != 0 || newcurpos != 0)
              newcurpos--;
          }
        }
        if (ksym.sym == SDLK_RIGHT) {
          if (newcurpos + 1> win.amount - 1) {
            if (doc.fpos + win.amount < doc.fsize) {
              newcurpos = 0;
              doc.fpos += win.amount;
            }
          } else if ((doc.fpos + newcurpos + 1) < doc.fsize) {
            newcurpos++;
          }
        }
        if (ksym.sym == SDLK_DOWN || ksym.sym == SDLK_PAGEDOWN) {
          if (
              (win.curpos + win.colsize) < win.amount &&
              (win.curpos + win.colsize + doc.fpos) < doc.fsize &&
              ksym.sym == SDLK_DOWN) {
            newcurpos += win.colsize;
          } else {
            if (doc.fpos + win.amount < doc.fsize) {
              if (ksym.sym == SDLK_DOWN)
                newcurpos = win.colsize - (win.amount - newcurpos) ;
              doc.fpos += win.amount;

              if ((newcurpos + doc.fpos) > doc.fsize) { // avoid overflow
                newcurpos = doc.fsize - doc.fpos - 1;
              }
            }
          }
        }
        if (ksym.sym == SDLK_UP || ksym.sym == SDLK_PAGEUP) {
          if ((win.curpos - win.colsize) > -1 && ksym.sym == SDLK_UP) {
            newcurpos -= win.colsize;
          } else {
            if (doc.fpos-win.amount >= 0) {
              if (ksym.sym == SDLK_UP)
                newcurpos = win.amount - (win.colsize - newcurpos);
              doc.fpos -= win.amount;
            }
          }
        }
        if (ksym.sym == SDLK_HOME) {
          doc.fpos  = 0;
          newcurpos = 0;
        }
        if (ksym.sym == SDLK_END) {
          doc.fpos  = doc.fsize - (doc.fsize%win.amount);
          newcurpos = doc.fsize - doc.fpos - 1;
        }
        win.curpos = newcurpos;
      }

      // key commands
      if (e.type == SDL_KEYUP) {
//         if (ksym.mod != KMOD_NONE)
//           break;

        switch(ksym.sym){
          case SDLK_0: case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4:
          case SDLK_5: case SDLK_6: case SDLK_7: case SDLK_8: case SDLK_9:
          case SDLK_a: case SDLK_b: case SDLK_c: case SDLK_d: case SDLK_e:
          case SDLK_f:
              if (!doc.ro && !currcmd)
                currcmd = CMD_INPUT;
              grab_input(ksym.sym);
            break;
          case SDLK_g: currcmd = CMD_GO; break;
          case SDLK_x:
            // if 'x' copy 1 byte and 'X' four bytes
            copy_bytes((mod == KMOD_LSHIFT || mod == KMOD_RSHIFT  ||
              mod == KMOD_CAPS)?4:1);
            break;
          case SDLK_ESCAPE:
          case SDLK_q: quit(0, NULL); break;
          case SDLK_RETURN: case SDLK_RETURN2:
            if (currcmd == CMD_GO) go(input_to_long(input));
          default: memset(&input, 0, 4); currcmd = CMD_NONE;
        }
      }
    }
  }
}

int main(int argc, char **argv)
{
  SDL_RWops *RWfont;

  argc--; argv++;
  if (argc == 0)
    exit(1);
  doc.filepath = *argv;

  assert( SDL_Init(SDL_INIT_VIDEO) == 0 );
  SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

  if (TTF_Init() < 0 ) quit(1, NULL);
  assert( (RWfont = SDL_RWFromConstMem(ttf, ttf_len)) != NULL );
  font = TTF_OpenFontRW(RWfont, 1, theme.font_size);
  assert( font != NULL );
  get_font_width();

  win.colsize = 4*win.cols;
  win.amount  = win.colsize*win.rows;
  win.height  = win.font_height * 3 + ((win.font_height*2)*win.rows) + 2 ;
  win.offsetcol = (SDL_Rect){
    win.font_width, win.font_height, win.font_width * 6, win.height
  };
  win.content = (SDL_Rect){
    win.offsetcol.w, win.font_height,
    /* 4 hex bytes * colums + spaces */
    ((win.font_width * 8) * win.cols) + (win.font_width * (win.cols)),
    win.height - win.font_height
  };
  win.asciicol = (SDL_Rect){
    win.offsetcol.w + win.content.w, win.font_height,
    (win.font_width*(win.colsize)) + win.font_width, /* chars + spaces */
    win.height
  };
  win.width   = win.offsetcol.w + win.content.w + win.asciicol.w;
  win.infobar = (SDL_Rect){
    win.font_width, win.content.h - win.font_height,
    win.width - win.font_width*2, win.font_height
  };
  window = SDL_CreateWindow(
    "hexing",
    SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
    win.width, win.height,
    SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
      SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS
  );
  assert( window != NULL );
  SDL_SetWindowResizable(window, SDL_FALSE);
  screen = SDL_GetWindowSurface(window);

  renderer = SDL_GetRenderer(window);
  if (renderer == NULL )
    renderer = SDL_CreateRenderer(window, -1,
      SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  assert(renderer != NULL);

  spaces  = win.cols - 1;
  currcmd = CMD_NONE;

  init_content();
  running();
  quit(0, NULL);
}

