/*
 * MS Windows driver for QEmacs
 * Copyright (c) 2002 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "qe.h"

#include <windows.h>

extern int main1(int argc, char **argv);
LRESULT CALLBACK qe_wnd_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern QEDisplay win32_dpy;

static HINSTANCE _hPrev, _hInstance;
static int font_xsize;

/* state of a single window */
typedef struct WinWindow {
    HWND w;
    HDC hdc;
    HFONT font;
} WinWindow;

typedef struct QEEventQ {
    QEEvent ev;
    struct QEEventQ *next;
} QEEventQ;
 
QEEventQ *first_event, *last_event;
WinWindow win_ctx;

#define PROG_NAME "qemacs"

/* the main is there. We simulate a unix command line by parsing the
   windows command line */
int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInst, 
                   LPSTR lpszCmdLine, int nCmdShow)
{
   char **argv;
   int argc, count;
   char *command_line, *p;

   command_line = malloc(strlen(lpszCmdLine) + sizeof(PROG_NAME) + 1);
   if (!command_line)
       return 0;
   strcpy(command_line, PROG_NAME " ");
   strcat(command_line, lpszCmdLine);
   _hPrev = hPrevInst;
   _hInstance = hInstance;

   p = command_line;
   count = 0;
   for(;;) {
       skip_spaces((const char **)&p);
       if (*p == '\0')
           break;
       while (*p != '\0' && !css_is_space(*p))
           p++;
      count++;
   }

   argv = (char **)malloc( (count + 1) * sizeof(char *) );
   if (!argv)
       return 0;
   
   argc = 0;
   p = command_line;
   for(;;) {
       skip_spaces((const char **)&p);
       if (*p == '\0')
           break;
       argv[argc++] = p;
       while (*p != '\0' && !css_is_space(*p))
           p++;
       *p = '\0';
       p++;
   }

   argv[argc] = NULL;

#if 0
   {
       int i;
       for(i=0;i<argc;i++) {
           printf("%d: '%s'\n", i, argv[i]);
       }
   }
#endif

   return main1(argc, argv);
}

static int win_probe(void)
{
    return 1;
}

static void init_application(void)
{
    WNDCLASS wc;
    
    wc.style = 0;
    wc.lpfnWndProc = qe_wnd_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = _hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_IBEAM);
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "qemacs";
    
    RegisterClass(&wc);
}

static int win_init(QEditScreen *s, int w, int h)
{
    int xsize, ysize, font_ysize;
    TEXTMETRIC tm;
    HDC hdc;
    HWND desktop_hwnd;
    
    if (!_hPrev) 
        init_application();

    memcpy(&s->dpy, &win32_dpy, sizeof(QEDisplay));

    s->private = NULL;
    s->media = CSS_MEDIA_SCREEN;
    
    win_ctx.font = CreateFont(-12, 0, 0, 0, 0 , 0, 0, 0, 0, 0, 0, 0, 
                              FIXED_PITCH, "fixed");

    /* get font metric for window size */
    desktop_hwnd = GetDesktopWindow();
    hdc = GetDC(desktop_hwnd);
    SelectObject(hdc, win_ctx.font);
    GetTextMetrics(hdc, &tm);
    ReleaseDC(desktop_hwnd, hdc);

    font_xsize = tm.tmAveCharWidth;
    font_ysize = tm.tmHeight;

    xsize = 80 * font_xsize;
    ysize = 25 * font_ysize;

    s->width = xsize;
    s->height = ysize;
    s->charset = &charset_utf8;

    s->clip_x1 = 0;
    s->clip_y1 = 0;
    s->clip_x2 = s->width;
    s->clip_y2 = s->height;

    win_ctx.w = CreateWindow("qemacs", "qemacs", WS_OVERLAPPEDWINDOW, 
                             0, 0, xsize, ysize, NULL, NULL, _hInstance, NULL);

    win_ctx.hdc = GetDC(win_ctx.w);
    SelectObject(win_ctx.hdc, win_ctx.font);

    //    SetWindowPos (win_ctx.w, NULL, 0, 0, xsize, ysize, SWP_NOMOVE);

    ShowWindow(win_ctx.w, SW_SHOW);
    UpdateWindow(win_ctx.w);
    
    return 0;
}

static void win_close(QEditScreen *s)
{
    ReleaseDC(win_ctx.w, win_ctx.hdc);
    DestroyWindow(win_ctx.w);
    DeleteObject(win_ctx.font);
}

static void win_flush(QEditScreen *s)
{
}

static int win_is_user_input_pending(QEditScreen *s)
{
    /* XXX: do it */
    return 0;
}

static void push_event(QEEvent *ev)
{
    QEEventQ *e;
    
    e = malloc(sizeof(QEEventQ));
    if (!e)
        return;
    e->ev = *ev;
    e->next = NULL;
    if (!last_event)
        first_event = e;
    else
        last_event->next = e;
    last_event = e;
}

static void push_key(int key)
{
    QEEvent ev;
    ev.type = QE_KEY_EVENT;
    ev.key_event.key = key;
    push_event(&ev);
}

static int ignore_wchar_msg = 0;

LRESULT CALLBACK qe_wnd_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    //    printf("msg=%d\n", msg);
    switch (msg) {
    case WM_CREATE:
        /* NOTE: must store them here to avoid problems in main */
        win_ctx.w = hWnd;
        return 0;
        
        /* key handling */
    case WM_CHAR:
        if (!ignore_wchar_msg) {
            push_key(wParam);
        } else {
            return DefWindowProc(hWnd, msg, wParam, lParam);
        }
        break;
    case WM_SYSCHAR:
        if (!ignore_wchar_msg) {
            int key;
            key = wParam;
            if (key >= ' ' && key <= '~') {
                key = KEY_META(' ') + key - ' ';
                push_key(key);
                break;
            }
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
        {
            unsigned int scan;
            int ctrl, shift, alt, key;
            
            ctrl = (GetKeyState(VK_CONTROL) & 0x8000);
            shift = (GetKeyState(VK_SHIFT) & 0x8000);
            alt = (GetKeyState(VK_MENU) & 0x8000);
            
            ignore_wchar_msg = 0;
            
            scan = (unsigned int) ((lParam >> 16) & 0x1FF);
            switch(scan) {
            case 0x00E:
                ignore_wchar_msg = 1;
                push_key(KEY_BACKSPACE);
                break;
            case 0x039: /* space */
                ignore_wchar_msg = 1;
                if (!ctrl) 
                    push_key(KEY_SPC);
                else
                    push_key(KEY_CTRL('@'));
                break;
            case 0x147: 		       /* home */
                push_key(KEY_HOME);
                break;
            case 0x148:		       /* UP */
                push_key(KEY_UP);
                break;
            case 0x149:		       /* PGUP */
                push_key(KEY_PAGEUP);
                break;
            case 0x14B:		       /* LEFT */
                push_key(KEY_LEFT);
                break;
            case 0x14D:		       /* RIGHT */
                push_key(KEY_RIGHT);
                break;
            case 0x14F:		       /* END */
                push_key(KEY_END);
                break;
            case 0x150:		       /* DOWN */
                push_key(KEY_DOWN);
                break;
            case 0x151:		       /* PGDN */
                push_key(KEY_PAGEDOWN);
                break;
            case 0x153:		       /* DEL */
                push_key(KEY_DELETE);
                break;
            case 0x152:		       /* INSERT */
                push_key(KEY_INSERT);
                break;
            case 0x3b:		       /* F1 */
            case 0x3c:
            case 0x3d:
            case 0x3e:
            case 0x3f:
            case 0x40:
            case 0x41:
            case 0x42:
            case 0x43:
            case 0x44:
            case 0x57:
            case 0x58:		       /* F12 */
                key = scan - 0x3b;
                if (key > 9)
                    key -= 0x12;
                key += KEY_F1;
                /* we leave Alt-F4 to close the window */
                if (alt && key == KEY_F4)
                    return DefWindowProc(hWnd, msg, wParam, lParam);
                push_key(key);
                break;
                  
            default: 
                return DefWindowProc(hWnd, msg, wParam, lParam);
            }
        }
        break;

    case WM_KEYUP:
        ignore_wchar_msg = 0;
        break;
          
    case WM_SYSKEYUP:
        ignore_wchar_msg = 0;
        return DefWindowProc(hWnd, msg, wParam, lParam);

    case WM_SIZE:
	if (wParam != SIZE_MINIMIZED) {
            QEmacsState *qs = &qe_state;
            QEEvent ev;

            qs->screen->width = LOWORD(lParam);
            qs->screen->height = HIWORD(lParam);
            ev.expose_event.type = QE_EXPOSE_EVENT;
            push_event(&ev);
        }
	break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC saved_hdc;
            BeginPaint(win_ctx.w, &ps);
            saved_hdc = win_ctx.hdc;
            win_ctx.hdc = ps.hdc;
            SelectObject(win_ctx.hdc, win_ctx.font);
            do_refresh(NULL);
            
            EndPaint(win_ctx.w, &ps);
            win_ctx.hdc = saved_hdc;
        }
	break;

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        break;

    default:
	return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

static int get_unicode_key(QEditScreen *s, QEPollData *pd, QEEvent *ev)
{
    MSG msg;
    QEEventQ *e;

    for(;;) {
        /* check if events queued */
        if (first_event != NULL) {
            e = first_event;
            *ev = e->ev;
            first_event = e->next;
            if (!first_event)
                last_event = NULL;
            free(e);
            break;
        }

        /* check if message queued */
        if (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    return 1;
}

static void win_fill_rectangle(QEditScreen *s,
                               int x1, int y1, int w, int h, QEColor color)
{
   RECT rc;
   HBRUSH hbr;
   COLORREF col;

   /* XXX: suppress XOR mode */
   if (color == QECOLOR_XOR)
       color = QERGB(0xff, 0xff, 0xff);

   SetRect(&rc, x1, y1, x1 + w, y1 + h);
   col = RGB((color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff);
   hbr = CreateSolidBrush(col);
   FillRect(win_ctx.hdc, &rc, hbr);
   DeleteObject(hbr);
}

static QEFont *win_open_font(QEditScreen *s, int style, int size)
{
    QEFont *font;
    TEXTMETRIC tm;

    font = malloc(sizeof(QEFont));
    if (!font)
        return NULL;
    GetTextMetrics(win_ctx.hdc, &tm);
    font->ascent = tm.tmAscent;
    font->descent = tm.tmDescent;
    font->private = NULL;
    return font;
}

static void win_close_font(QEditScreen *s, QEFont *font)
{
    free(font);
}

static void win_text_metrics(QEditScreen *s, QEFont *font, 
                             QECharMetrics *metrics,
                             const unsigned int *str, int len)
{
    int i, x;
    metrics->font_ascent = font->ascent;
    metrics->font_descent = font->descent;
    x = 0;
    for(i=0;i<len;i++)
        x += font_xsize;
    metrics->width = x;
}

static void win_draw_text(QEditScreen *s, QEFont *font,
                          int x1, int y, const unsigned int *str, int len,
                          QEColor color)
{
    int i;
    WORD buf[len];
    COLORREF col;

    for(i=0;i<len;i++)
        buf[i] = str[i];
    col = RGB((color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff);
    SetTextColor(win_ctx.hdc, col);
    SetBkMode(win_ctx.hdc, TRANSPARENT);
    TextOutW(win_ctx.hdc, x1, y - font->ascent, buf, len);
}

static void win_set_clip(QEditScreen *s,
                         int x, int y, int w, int h)
{
    /* nothing to do */
}

static QEDisplay win32_dpy = {
    "win32",
    win_probe,
    win_init,
    win_close,
    NULL,
    win_flush,
    win_is_user_input_pending,
    win_fill_rectangle,
    win_open_font,
    win_close_font,
    win_text_metrics,
    win_draw_text,
    win_set_clip,
    NULL, /* no selection handling */
    NULL, /* no selection handling */
};

int win32_init(void)
{
    return qe_register_display(&win32_dpy);
}

qe_module_init(win32_init);
