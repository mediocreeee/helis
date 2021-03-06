/* Includes */
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* Defines */
#define CTRL_KEY(k) ((k)&0x1f)
#define HELIS_VERSION "0.0.0.0.1"
#define HELIS_TAB_STOP 4
#define HELIS_QUIT_TIMES 1

// Keys bindings
enum editorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN,
};

enum editorSequnces {
  GG_SEQ,
};

enum editorHighlight {
  HL_NORMAL = 0,
  HL_COMMENT,
  HL_MLCOMMENT,
  HL_KEYWORD1,
  HL_KEYWORD2,
  HL_STRING,
  HL_NUMBER,
  HL_MATCH
};

// Highlight flags
#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1 << 1)

/* Data */

// Syntax
struct editorSyntax {
  char *filetype;
  char **filematch;
  char **keywords;
  char *singleline_comment_start;
  char *multiline_comment_start;
  char *multiline_comment_end;
  int flags;
};

// Editor modes
enum editorMode { Normal, Visual, Insert, Cmd };

char *editorModes[] = {"Normal", "Visual", "Insert", "Cmd"};

// Row of text
typedef struct erow {
  int idx;
  int size;          // Chars size
  int rsize;         // Render size
  char *chars;       // Chars in a row(actual)
  char *render;      // Chars in a row(to render)
  unsigned char *hl; // Highlight
  int hl_open_comment;
} erow;

// Editor config
struct editorConfig {
  int cx, cy;                  // Cursor coords
  int rx;                      // Render coord
  int rowoff;                  // Row offset
  int coloff;                  // Column offset
  int screenrows;              // Screen row count
  int screencols;              // Screen columns count
  int numrows;                 // File rows count
  erow *row;                   // Row sctruct
  int dirty;                   // Predicate of dirtiness
  char *filename;              // File Name
  char statusmsg[80];          // Status message
  time_t statusmsg_time;       // Timestamp for status message
  struct editorSyntax *syntax; // Syntax
  struct termios orig_termios; // Terminal attributes
  enum editorMode mode;        // Editor mode
};

struct editorConfig E;

/* Filetypes */

// Language extensions for highlight
char *C_HL_extensions[] = {".c", ".h", ".cpp", NULL};
char *C_HL_keywords[] = {"switch",    "if",      "while",   "for",    "break",
                         "continue",  "return",  "else",    "struct", "union",
                         "typedef",   "static",  "enum",    "class",  "case",

                         "int|",      "long|",   "double|", "float|", "char|",
                         "unsigned|", "signed|", "void|",   NULL};

// Higlight database
struct editorSyntax HLDB[] = {
    {"c", C_HL_extensions, C_HL_keywords, "//", "/*", "*/",
     HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
};

// Length of database
#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/* Prototypes */
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));

/* Terminal */

// Fail handling
void die(const char *s) {
  // TODO: Refactor to editorRefreshScreen later
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[1;1H", 6);
  perror(s);
  exit(1);
}

// Disabling raw mode at exit to prevent issues
void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

// Getting the atributes and setting flags to make terminal raw
void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
    die("tcgetattr");
  }

  atexit(disableRawMode);

  // Flags to make terminal 'true' raw
  struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

  // Min num of bytes needed before read() can return
  raw.c_cc[VMIN] = 0;
  // Max amount of time to wati before read() return
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

// Reading the key from stdin
int editorReadKey() {
  int nread;
  char seq[6];

  while ((nread = read(STDIN_FILENO, &seq[0], 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }

  if (seq[0] == '\x1b') {

    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[2], 1) != 1)
      return '\x1b';

    if (seq[1] == '[') {
      if (seq[2] >= '0' && seq[2] <= '9') {
        if (read(STDIN_FILENO, &seq[3], 1) != 1)
          return '\x1b';
        if (seq[3] == '~') {
          switch (seq[2]) {
          case '1':
            return HOME_KEY;
          case '3':
            return DEL_KEY;
          case '4':
            return END_KEY;
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          case '7':
            return HOME_KEY;
          case '8':
            return END_KEY;
          }
        }
      } else {
        switch (seq[2]) {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
        case 'P':
          return DEL_KEY;
        }
      }
    } else if (seq[1] == 'O') {
      switch (seq[2]) {
      case 'H':
        return HOME_KEY;
      case 'F':
        return END_KEY;
      }
    }
    return '\x1b';
  } else if (seq[0] == 'g') {
    if (read(STDIN_FILENO, &seq[1], 1) != 1) {
      return 'g';
    } else if (seq[1] == 'g') {
      return GG_SEQ;
    } else {
      return seq[0];
    }

  } else {
    return seq[0];
  }
}

// Getting the cursor position
int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i] = '\0';
  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;
  return 0;
}

// Getting the window size
int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/* Syntax Highlight */

int is_separator(int c) {
  return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

// Update Highlight
void editorUpdateSyntax(erow *row) {
  row->hl = realloc(row->hl, row->rsize);
  memset(row->hl, HL_NORMAL, row->rsize);

  // If no syntax return
  if (E.syntax == NULL)
    return;

  char **keywords = E.syntax->keywords;

  char *scs = E.syntax->singleline_comment_start;
  char *mcs = E.syntax->multiline_comment_start;
  char *mce = E.syntax->multiline_comment_end;

  int scs_len = scs ? strlen(scs) : 0;
  int mcs_len = mcs ? strlen(mcs) : 0;
  int mce_len = mce ? strlen(mce) : 0;

  int prev_sep = 1;
  int in_string = 0;
  int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

  int i = 0;
  while (i < row->rsize) {
    char c = row->render[i];
    unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

    if (scs_len && !in_string && !in_comment) {
      if (!strncmp(&row->render[i], scs, scs_len)) {
        memset(&row->hl[i], HL_COMMENT, row->rsize - i);
        break;
      }
    }

    if (mcs_len && mce_len && !in_string) {
      if (in_comment) {
        row->hl[i] = HL_MLCOMMENT;
        if (!strncmp(&row->render[i], mce, mce_len)) {
          memset(&row->hl[i], HL_MLCOMMENT, mce_len);
          i += mce_len;
          in_comment = 0;
          prev_sep = 1;
          continue;
        } else {
          i++;
          continue;
        }
      } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
        memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
        i += mcs_len;
        in_comment = 1;
        continue;
      }
    }

    if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
      if (in_string) {
        row->hl[i] = HL_STRING;
        if (c == '\\' && i + 1 < row->rsize) {
          row->hl[i + 1] = HL_STRING;
          i += 2;
          continue;
        }
        if (c == in_string)
          in_string = 0;
        i++;
        prev_sep = 1;
        continue;
      } else {
        if (c == '"' || c == '\'') {
          in_string = c;
          row->hl[i] = HL_STRING;
          i++;
          continue;
        }
      }
    }

    if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
      if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
          (c == '.' && prev_hl == HL_NUMBER)) {
        row->hl[i] = HL_NUMBER;
        i++;
        prev_sep = 0;
        continue;
      }
    }

    if (prev_sep) {
      int j;
      for (j = 0; keywords[j]; j++) {
        int klen = strlen(keywords[j]);
        int kw2 = keywords[j][klen - 1] == '|';
        if (kw2)
          klen--;

        if (!strncmp(&row->render[i], keywords[j], klen) &&
            is_separator(row->render[i + klen])) {
          memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
          i += klen;
          break;
        }
      }
      if (keywords[j] != NULL) {
        prev_sep = 0;
        continue;
      }
    }

    prev_sep = is_separator(c);
    i++;
  }

  int changed = (row->hl_open_comment != in_comment);
  row->hl_open_comment = in_comment;
  if (changed && row->idx + 1 < E.numrows)
    editorUpdateSyntax(&E.row[row->idx + 1]);
}

// Syntax to Color
int editorSyntaxToColor(int hl) {
  switch (hl) {
  case HL_COMMENT:
  case HL_MLCOMMENT:
    return 36;
  case HL_KEYWORD1:
    return 33;
  case HL_KEYWORD2:
    return 32;
  case HL_STRING:
    return 35;
  case HL_NUMBER:
    return 31;
  case HL_MATCH:
    return 34;
  default:
    return 37;
  }
}

void editorSelectSyntaxHighlight() {
  E.syntax = NULL;
  if (E.filename == NULL)
    return;

  for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
    struct editorSyntax *s = &HLDB[j];
    unsigned int i = 0;
    while (s->filematch[i]) {
      char *p = strstr(E.filename, s->filematch[i]);
      if (p != NULL) {
        int patlen = strlen(s->filematch[i]);
        if (s->filematch[i][0] != '.' || p[patlen] == '\0') {
          E.syntax = s;
          int filerow;
          for (filerow = 0; filerow < E.numrows; filerow++) {
            editorUpdateSyntax(&E.row[filerow]);
          }
          return;
        }
      }
      i++;
    }
  }
}

/* Row Functions */

// Convert chars x to render x
int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t') {
      rx += (HELIS_TAB_STOP - 1) - (rx % HELIS_TAB_STOP);
    }
    rx++;
  }
  return rx;
}

// Convert rendex x to chars x
int editorRowRxToCx(erow *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (HELIS_TAB_STOP - 1) - (cur_rx % HELIS_TAB_STOP);
    cur_rx++;

    if (cur_rx > rx)
      return cx;
  }
  return cx;
}

// Update Row
void editorUpdateRow(erow *row) {

  // Handling tabs
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t')
      tabs++;
  }

  free(row->render);
  row->render = malloc(row->size + tabs * (HELIS_TAB_STOP - 1) + 1);

  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while ((idx % HELIS_TAB_STOP) != 0)
        row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }

  row->render[idx] = '\0';
  row->rsize = idx;
  editorUpdateSyntax(row);
}

// Insert row
void editorInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > E.numrows)
    return;

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
  for (int j = at + 1; j <= E.numrows; j++)
    E.row[j].idx++;

  E.row[at].idx = at;

  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  E.row[at].hl = NULL;
  E.row[at].hl_open_comment = 0;
  editorUpdateRow(&E.row[at]);

  E.numrows++;
  // Update dirtiness
  E.dirty++;
}

// Free memory of erow
void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
  free(row->hl);
}

// Delete row
void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows)
    return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  for (int j = at; j < E.numrows - 1; j++)
    E.row[j].idx--;
  E.numrows--;
  E.dirty++;
}

// Insert character
void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size)
    at = row->size;
  // Allocate memory for new char
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  // Update row
  editorUpdateRow(row);
  // Update dirtiness
  E.dirty++;
}

// Delete character
void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size)
    return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  // Set dirty to true
  E.dirty++;
}

/* Editor Functions */

// Insert character
void editorInsertChar(int c) {
  if (E.cy == E.numrows) {
    editorInsertRow(E.numrows, "", 0);
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

// Handle Enter key
void editorInsertNewline() {
  if (E.cx == 0) {
    editorInsertRow(E.cy, "", 0);
  } else {
    erow *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = 0;
}

// Append row
void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

// Delete character
void editorDelChar() {
  if (E.cy == E.numrows)
    return;
  if (E.cx == 0 && E.cy == 0)
    return;

  erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    editorRowDelChar(row, E.cx - 1);
    E.cx--;
  } else {
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    editorDelRow(E.cy);
    E.cy--;
  }
}

/* File I/O */

// Convert array of rows to string
char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++) {
    // Add byte for \n
    totlen += E.row[j].size + 1;
  }
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;
  // Copy each row into buffer
  for (j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    // Append newline
    *p = '\n';
    p++;
  }

  return buf;
}

// Open file in the editor
void editorOpen(char *filename) {
  free(E.filename);
  // Set File Name
  E.filename = strdup(filename);

  // Set highlight
  editorSelectSyntaxHighlight();

  // Open file stream
  FILE *fp = fopen(filename, "r");
  if (!fp)
    die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  // Copy line form file into erow struct
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;
    editorInsertRow(E.numrows, line, linelen);
  }
  free(line);
  // Closing file stream
  fclose(fp);
  // Set dirtiness to false
  E.dirty = 0;
}

// Save file changes to disk
void editorSave() {
  if (E.filename == NULL) {
    E.filename = editorPrompt("Save as: %s", NULL);
    if (E.filename == NULL) {
      editorSetStatusMessage("Save aborted");
      return;
    }
    editorSelectSyntaxHighlight();
  }

  int len;
  char *buf = editorRowsToString(&len);

  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        E.dirty = 0;
        editorSetStatusMessage("%d bytes writen to disk", len);
        return;
      }
    }
    close(fd);
  }
  free(buf);
  editorSetStatusMessage("Filed ot save: I/o error: %s", strerror(errno));
}

/* Find */

// Callback for find
void editorFindCallback(char *query, int key) {
  // Search forward and backward
  static int last_match = -1;
  static int direction = 1;

  // Highlight of search
  static int saved_hl_line;
  static char *saved_hl = NULL;

  if (saved_hl) {
    memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
    free(saved_hl);
    saved_hl = NULL;
  }

  if (key == '\r' || key == '\x1b') {
    last_match = -1;
    direction = 1;
    return;
  } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
    direction = 1;
  } else if (key == ARROW_LEFT || key == ARROW_UP) {
    direction = -1;
  } else {
    last_match = -1;
    direction = 1;
  }

  if (last_match == -1)
    direction = 1;
  int current = last_match;

  int i;
  for (i = 0; i < E.numrows; i++) {
    current += direction;
    if (current == -1)
      current = E.numrows - 1;
    else if (current == E.numrows)
      current = 0;

    erow *row = &E.row[current];
    char *match = strstr(row->render, query);
    if (match) {
      last_match = current;
      E.cy = current;
      E.cx = editorRowRxToCx(row, match - row->render);
      E.rowoff = E.numrows;

      saved_hl_line = current;
      saved_hl = malloc(row->rsize);
      memcpy(saved_hl, row->hl, row->rsize);
      memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
      break;
    }
  }
}

// Find in text
void editorFind() {
  // Save cursor position
  int saved_cx = E.cx;
  int saved_cy = E.cy;
  int saved_coloff = E.coloff;
  int saved_rowoff = E.rowoff;

  char *query =
      editorPrompt("Search: %s (Use ESC/Arrow/Enter)", editorFindCallback);
  if (query) {
    free(query);
  } else {
    // Restore cursor position
    E.cx = saved_cx;
    E.cy = saved_cy;
    E.coloff = saved_coloff;
    E.rowoff = saved_rowoff;
  }
}

/* Append Buffer */

// Dynamic string
struct abuf {
  char *b;
  int len;
};

// Empty abuf
#define ABUF_INIT                                                              \
  { NULL, 0 }

// Append to dynamic string
void abAppend(struct abuf *ab, const char *s, int len) {
  // Allocate memory for new string
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL)
    return;
  // Append string into buffer
  memcpy(&new[ab->len], s, len);
  // Update buffer
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) { free(ab->b); }

/* Output */

// Scrolling
void editorScroll() {
  E.rx = 0;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }
}
// Drawing rows
void editorDrawRows(struct abuf *ab) {
  int r;
  for (r = 0; r < E.screenrows; r++) {
    // Rows in the file
    int filerow = r + E.rowoff;
    if (filerow >= E.numrows) {
      if (E.numrows == 0 && r == E.screenrows / 3) {
        // Printing editor version
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
                                  "Helis editor -- version %s", HELIS_VERSION);
        // If welcome string is longer then terminal columns number, truncate it
        if (welcomelen > E.screencols)
          welcomelen = E.screencols;
        // Centering the welcome message
        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, ">", 1);
          padding--;
        }
        while (padding--)
          abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      } else {
        abAppend(ab, ">", 1);
      }
    } else {
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0)
        len = 0;
      if (len > E.screencols)
        len = E.screencols;

      char *c = &E.row[filerow].render[E.coloff];
      unsigned char *hl = &E.row[filerow].hl[E.coloff];
      int current_color = -1;
      int j;
      for (j = 0; j < len; j++) {
        if (iscntrl(c[j])) {
          char sym = (c[j] <= 26) ? '@' + c[j] : '?';
          abAppend(ab, "\x1b[7m", 4);
          abAppend(ab, &sym, 1);
          abAppend(ab, "\x1b[m", 3);
          if (current_color != -1) {
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
            abAppend(ab, buf, clen);
          }
        } else if (hl[j] == HL_NORMAL) {
          if (current_color != -1) {
            abAppend(ab, "\x1b[39m", 5);
            current_color = -1;
          }
          abAppend(ab, &c[j], 1);
        } else {
          int color = editorSyntaxToColor(hl[j]);
          if (color != current_color) {
            current_color = color;
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
            abAppend(ab, buf, clen);
          }
          abAppend(ab, &c[j], 1);
        }
      }
      abAppend(ab, "\x1b[39m", 5);
    }
    // Clear the line when redrawing
    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

// Draw status bar
void editorDrawStatusBar(struct abuf *ab) {
  // Invert colors and make text bold for status bar
  abAppend(ab, "\x1b[1;7m", 6);
  char status[80], rstatus[80];
  // File Name, Lines Count and Dirtiness on the left side
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                     E.filename ? E.filename : "[ No Name ]", E.numrows,
                     E.dirty ? "(modified)" : "");
  // Mode and Line:LinesCount on the right side
  int rlen = snprintf(
      rstatus, sizeof(rstatus), "[%s] | %s | %d:%d", editorModes[E.mode],
      E.syntax ? E.syntax->filetype : "no ft", E.cy + 1, E.numrows);
  if (len > E.screencols)
    len = E.screencols;
  abAppend(ab, status, len);
  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  // Switch back to normal colors
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

// Draw message bar
void editorDrawMessageBar(struct abuf *ab) {
  // Clear the message bar
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols)
    msglen = E.screencols;
  // Display the message if message is less than 5 seconds old
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}

// Refreshing Screen
void editorRefreshScreen() {
  editorScroll();

  struct abuf ab = ABUF_INIT;

  // Hiding the cursor
  abAppend(&ab, "\x1b[?25l", 6);
  // Move cursor to the top-left corner
  abAppend(&ab, "\x1b[1;1H", 6);

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  // Move cursor to E.rx and E.cy
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
           (E.rx - E.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));

  // Showing cursor
  abAppend(&ab, "\x1b[?25h", 6);

  // Write the buffer's contents
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

// Set Status Message
void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

/* Editor Modes */

// Enter the normal mode
void editorEnableNormalMode() {
  E.mode = Normal;
  write(STDOUT_FILENO, "\033[1 q", 5);
}

// Enter insert mode
void editorEnableInsertMode() {
  E.mode = Insert;
  write(STDOUT_FILENO, "\033[5 q", 5);
}

/* Input */

// Prompt for user
char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);

  size_t buflen = 0;
  buf[0] = '\0';

  while (1) {
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();

    int c = editorReadKey();
    if (c == DEL_KEY || c == CTRL_KEY('h') ||
        c == BACKSPACE) { // Let user use del, ctrl-h and backspace in prompt
      if (buflen != 0)
        buf[--buflen] = '\0';
    } else if (c == '\x1b') { // Cancel if user pressed Escape
      editorSetStatusMessage("");
      if (callback)
        callback(buf, c);
      free(buf);
      return NULL;
    } else if (c == '\r') { // Return if user pressed Enter
      if (buflen != 0) {
        editorSetStatusMessage("");
        if (callback)
          callback(buf, c);
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        // Double the size of buffer if user input is big enough
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }

    if (callback)
      callback(buf, c);
  }
}

// Moving the cursor
void editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  switch (key) {
  case ARROW_UP:
  case 'k':
    if (E.cy != 0) {
      E.cy--;
    }
    break;
  case ARROW_DOWN:
  case 'j':
    if (E.cy < E.numrows) {
      E.cy++;
    }
    break;
  case ARROW_LEFT:
  case 'h':
    if (E.cx != 0) {
      E.cx--;
    } else if (E.cy > 0) {
      E.cy--;
      E.cx = E.row[E.cy].size;
    }
    break;
  case ARROW_RIGHT:
  case 'l':
    if (row && E.cx < row->size) {
      E.cx++;
    } else if (row && E.cx == row->size) {
      E.cy++;
      E.cx = 0;
    }
    break;
  }

  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}

/* Exit */

void clearAndExit() {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[1;1H", 6);
  exit(0);
}

/* Cmd mode */

void editorCmdPrompt() {
  E.mode = Cmd;
  char *query = editorPrompt("Cmd: %s", NULL);
  // If ESC was pressed query return NULL
  if (!query) {
    editorEnableNormalMode();
    return;
  }

  // Checking for variants
  if (strcmp(query, "quit") == 0 || strcmp(query, "q") == 0) {

    // FIXME
    // How many times :q is needed to be pressed to quit
    /* static int quit_times = HELIS_QUIT_TIMES; */

    // If file is modifeid, warn user at quit
    /* if (E.dirty && quit_times > 0) { */
    /*   editorSetStatusMessage("WARNING!!! File has unsaved changes. " */
    /*                          "Press :q %d more times to quit.", */
    /*                          quit_times); */
    /*   quit_times--; */
    /* } */
    /* quit_times = HELIS_QUIT_TIMES; */

    clearAndExit();
  } else if (strcmp(query, "write") == 0 || strcmp(query, "w") == 0) {
    editorSave();
    editorEnableNormalMode();
  } else {
    editorEnableNormalMode();
  }
}
// Handle keypress in normal mode
void editorProcessNormalKeypress(int c) {

  // Handle Enter
  switch (c) {
  case '\r':
    editorMoveCursor(ARROW_DOWN);
    break;

    // / to "Find"
  case '/':
    editorFind();
    break;

    // Basic insert keys
  case 'i':
    editorEnableInsertMode();
    break;
  case 'I':
    E.cx = 0;
    editorEnableInsertMode();
    break;
  case 'a':
    editorMoveCursor(ARROW_RIGHT);
    editorEnableInsertMode();
    break;
  case 'A':
    if (E.cy < E.numrows)
      E.cx = E.row[E.cy].size;
    editorEnableInsertMode();
    break;
  case 'o':
    if (E.cy < E.numrows)
      E.cx = E.row[E.cy].size;
    editorEnableInsertMode();
    editorInsertNewline();
    break;
  case 'O':
    editorMoveCursor(ARROW_UP);
    if (E.cy < E.numrows)
      E.cx = E.row[E.cy].size;
    editorEnableInsertMode();
    editorInsertNewline();
    break;

    // Basic movement keys
  case '0':
  case HOME_KEY:
    E.cx = 0;
    break;
  case '$':
  case END_KEY:
    if (E.cy < E.numrows)
      E.cx = E.row[E.cy].size;
    break;
  case ' ':
    editorMoveCursor(ARROW_LEFT);
    break;
  case BACKSPACE:
  case CTRL_KEY('h'):
    editorMoveCursor(ARROW_LEFT);
    break;
  case DEL_KEY:
    editorMoveCursor(ARROW_RIGHT);
    break;

  case 'G':
    E.cy = E.numrows - 1;
    break;

  case PAGE_UP:
  case PAGE_DOWN: {
    if (c == PAGE_UP) {
      E.cy = E.rowoff;
    } else {
      E.cy = E.rowoff + E.screenrows - 1;
      if (E.cy > E.numrows)
        E.cy = E.numrows;
    }
    int times = E.screenrows;
    while (times--)
      editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  } break;

  // Handle x to delete char
  case 'x':
    editorMoveCursor(ARROW_RIGHT);
    editorDelChar();
    break;

    // Handle : to goto cmd mode
  case ':': {
    editorCmdPrompt();
    break;
  }

  // Handle hjkl and Arrows
  case ARROW_LEFT:
  case ARROW_DOWN:
  case ARROW_UP:
  case ARROW_RIGHT:
  case 'h':
  case 'j':
  case 'k':
  case 'l':
    editorMoveCursor(c);
    break;

  // Goto Normal mode on 'ESC'
  case CTRL_KEY('l'):
  case '\x1b':
    editorEnableNormalMode();
    break;

  case GG_SEQ:
    E.cy = 0;
  default:
    break;
  }
}
// Handle keypress in insert mode
void editorProcessInsertKeypress(int c) {
  switch (c) {
    // Insert newline on 'Enter'
  case '\r':
    editorInsertNewline();
    break;

    // Handle keys for deletion
  case BACKSPACE:
  case CTRL_KEY('h'):
  case DEL_KEY:
    if (c == DEL_KEY)
      editorMoveCursor(ARROW_RIGHT);
    editorDelChar();
    break;

  // Handle PageUP and PageDown
  case PAGE_UP:
  case PAGE_DOWN: {
    if (c == PAGE_UP) {
      E.cy = E.rowoff;
    } else {
      E.cy = E.rowoff + E.screenrows - 1;
      if (E.cy > E.numrows)
        E.cy = E.numrows;
    }
    int times = E.screenrows;
    while (times--)
      editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  } break;

    // Handle Home and End
  case HOME_KEY:
    E.cx = 0;
    break;
  case END_KEY:
    if (E.cy < E.numrows)
      E.cx = E.row[E.cy].size;
    break;

    // Goto Normal mode on 'ESC'
  case CTRL_KEY('l'):
  case '\x1b':
    editorEnableNormalMode();
    break;

  case ARROW_LEFT:
  case ARROW_DOWN:
  case ARROW_UP:
  case ARROW_RIGHT:
    editorMoveCursor(c);
    break;

  default:
    editorInsertChar(c);
  }
}
// TODO:
// Handle keypress in visual mode
// Handle keypress in cmd mode
// Make own func:
/* if (E.cy < E.numrows) */
/*   E.cx = E.row[E.cy].size; */

// Handling keypress
void editorProcessKeypress() {
  int c = editorReadKey();

  switch (E.mode) {
  case Normal:
    editorProcessNormalKeypress(c);
    break;
  case Insert:
    editorProcessInsertKeypress(c);
    break;
    /* // TODO: */
    /* case Visual: */
    /*   editorProcessVisualKeypress(); */
    /*   break; */
    /* case Cmd: */
    /*   editorProcessCmdKeypress(); */
    /*   break; */
  }
}

/* Init */

// Initialize the editor
void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0;
  E.numrows = 0;
  E.coloff = 0;
  E.row = NULL;
  E.dirty = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.mode = Normal;
  E.syntax = NULL;

  editorEnableNormalMode();

  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
  E.screenrows -= 2;
}

// Main
int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }

  editorSetStatusMessage(
      "HELP: w/write(cmd) = save | '/'(normal) = find | q/quit(cmd) = quit");

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
