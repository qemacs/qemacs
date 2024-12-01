/*
 * Haiku driver for QEmacs
 *
 * Copyright (c) 2013 Francois Revol.
 * Copyright (c) 2015-2024 Charlie Gordon.
 * Copyright (c) 2002 Fabrice Bellard.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

extern "C" {
#include "qe.h"
}

#include <Application.h>
#include <Bitmap.h>
#include <FindDirectory.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <Path.h>
#include <Region.h>
#include <String.h>
#include <View.h>
#include <Window.h>

static int font_xsize;

class QEWindow;
class QEView;

/* state of a single window */
typedef struct WindowState {
    BWindow *w;
    BView *v;
    BFont font;
    int events_rd;
    int events_wr;
} WindowState;

WindowState haiku_ctx;
static thread_id bapp_thid;
static int bapp_ref_count = 0;
static int events_wr;

/* count of pending repaints */
static int32 repaints = 0;
//TODO:use double-buffering with a BBitmap

static void haiku_handle_event(void *opaque);

static status_t bmessage_input(QEWindow *win, QEView *view, BMessage *message)
{
    if (!message)
        return EINVAL;
    /* push the message into the pipe */
    if (write(events_wr, &message, sizeof(BMessage *)) < 0)
        return errno;
    return B_OK;
}

class QEWindow : public BWindow {
    public:
        QEWindow(BRect frame, const char *name, QEView *view)
                : BWindow(frame, name, B_TITLED_WINDOW, 0)
                , fView(view)
        {}

        virtual ~QEWindow() {}

        //virtual void Show();
        //virtual void Hide();
        //virtual void Minimize(bool minimize);
        virtual bool QuitRequested();
        virtual void DispatchMessage(BMessage *message, BHandler *handler);

    private:
        QEView *fView;
};

class QEView : public BView {
    public:
        QEView(BRect frame, const char *name);
        virtual ~QEView();

        virtual void MouseDown(BPoint where);
        virtual void MouseUp(BPoint where);
        virtual void MouseMoved(BPoint where, uint32 code, const BMessage *a_message);
        virtual void KeyDown(const char *bytes, int32 numBytes);
        virtual void KeyUp(const char *bytes, int32 numBytes);
        virtual void Draw(BRect updateRect);
        virtual void FrameResized(float new_width, float new_height);
#if 0
        virtual void WindowActivated(bool state);
        virtual void MessageReceived(BMessage *message);
#endif
};


bool QEWindow::QuitRequested()
{
    BMessage *message = new BMessage(B_QUIT_REQUESTED);
    bmessage_input(this, NULL, message);
    return false;
}

void QEWindow::DispatchMessage(BMessage *message, BHandler *handler)
{
    uint32 mods;

    switch (message->what) {
    case B_MOUSE_WHEEL_CHANGED:
        {
            BMessage *message = DetachCurrentMessage();
            bmessage_input(this, NULL, message);
        }
        break;
    case B_KEY_DOWN:
        if ((message->FindInt32("modifiers", (int32 *)&mods) == B_OK) &&
              (mods & B_COMMAND_KEY)) {
            /* BWindow swallows KEY_DOWN when ALT is down...
             * so this hack is needed.
             */
            fView->KeyDown(NULL, 0);
            return;
        }
        break;
    case B_UNMAPPED_KEY_DOWN:
    case B_UNMAPPED_KEY_UP:
    case B_KEY_UP:
        //message->PrintToStream();
        break;
    }
    BWindow::DispatchMessage(message, handler);
}

QEView::QEView(BRect frame, const char *name)
        : BView(frame, name, B_FOLLOW_ALL_SIDES, B_WILL_DRAW|B_FRAME_EVENTS)
{
    SetViewColor(ui_color(B_DOCUMENT_BACKGROUND_COLOR));
    //SetViewColor(0, 255, 0);
}

QEView::~QEView()
{
}

void QEView::MouseDown(BPoint where)
{
    BMessage *message = Window()->DetachCurrentMessage();
    bmessage_input(NULL, this, message);
}

void QEView::MouseUp(BPoint where)
{
    BMessage *message = Window()->DetachCurrentMessage();
    bmessage_input(NULL, this, message);
}

void QEView::MouseMoved(BPoint where, uint32 code, const BMessage *a_message)
{
    BMessage *message = Window()->DetachCurrentMessage();
    bmessage_input(NULL, this, message);
}

void QEView::KeyDown(const char *bytes, int32 numBytes)
{
    BMessage *message = Window()->DetachCurrentMessage();
    //message->PrintToStream();
    bmessage_input(NULL, this, message);
}

void QEView::KeyUp(const char *bytes, int32 numBytes)
{
    BMessage *message = Window()->DetachCurrentMessage();
    bmessage_input(NULL, this, message);
}

void QEView::Draw(BRect updateRect)
{
    BMessage *message = new BMessage(_UPDATE_);
    message->AddRect("update_rect", updateRect);
    atomic_add(&repaints, 1);
    bmessage_input(NULL, this, message);
}

void QEView::FrameResized(float new_width, float new_height)
{
    BMessage *message = Window()->DetachCurrentMessage();
    atomic_set(&repaints, 0);
    bmessage_input(NULL, this, message);
    BView::FrameResized(new_width, new_height);
}

static int haiku_probe(void)
{
    if (force_tty)
        return 0;
    return 2;
}

static int32 bapp_thread(void *arg)
{
    be_app->Lock();
    be_app->Run();
    return 0;
}

static void init_application(void)
{
    bapp_ref_count++;

    if (be_app)
        return; /* done already! */

    new BApplication("application/x-vnd.Bellard-QEmacs");
    //new XEmacsApp;
    bapp_thid = spawn_thread(bapp_thread, "BApplication(QEmacs)",
                             B_NORMAL_PRIORITY, (void *)find_thread(NULL));
    if (bapp_thid < B_OK)
        return; /* #### handle errors */
    if (resume_thread(bapp_thid) < B_OK)
        return;
    // This way we ensure we don't create BWindows before be_app is created
    // (creating it in the thread doesn't ensure this)
    be_app->Unlock();
}

static void uninit_application(void)
{
    status_t err;
    if (--bapp_ref_count)
        return;
    be_app->Lock();
    be_app->Quit();
    //XXX:HACK
    be_app->Unlock();
    //be_app_messenger.SendMessage(B_QUIT_REQUESTED);
    wait_for_thread(bapp_thid, &err);
    //FIXME:leak or crash
    //delete be_app;
    be_app = NULL;
}

static int haiku_init(QEditScreen *s, int w, int h)
{
    int xsize, ysize, font_ysize;
    WindowState *ctx;

    init_application();

    ctx = qe_mallocz(WindowState);
    if (ctx == NULL)
        return -1;
    s->priv_data = ctx;
    s->media = CSS_MEDIA_SCREEN;

    s->bitmap_format = QEBITMAP_FORMAT_RGBA32;
    /* BBitmap supports overlay, but not planar data */
    //s->video_format = QEBITMAP_FORMAT_RGBA32;

    int event_pipe[2];
    if (pipe(event_pipe) < 0)
        return -1;

    fcntl(event_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(event_pipe[1], F_SETFD, FD_CLOEXEC);

    ctx->events_rd = event_pipe[0];
    ctx->events_wr = events_wr = event_pipe[1];

    set_read_handler(event_pipe[0], haiku_handle_event, s);

    ctx->font = BFont(be_fixed_font);

    font_height height;
    ctx->font.GetHeight(&height);

    font_xsize = (int)ctx->font.StringWidth("n");
    font_ysize = (int)(height.ascent + height.descent + height.leading + 1);

    if (w == 0)
        w = 80;
    if (h == 0)
        h = 25;
    xsize = w * font_xsize;
    ysize = h * font_ysize;

    s->width = xsize;
    s->height = ysize;
    s->charset = &charset_utf8;

    s->clip_x1 = 0;
    s->clip_y1 = 0;
    s->clip_x2 = s->width;
    s->clip_y2 = s->height;

    BRect frame(0, 0, s->width - 1, s->height - 1);

    QEView *v = new QEView(frame, "qemacs");
    ctx->v = v;

    frame.OffsetTo(200, 200);

    ctx->w = new QEWindow(frame, "qemacs", v);
    ctx->w->AddChild(ctx->v);
    ctx->v->MakeFocus();
    //ctx->v->SetViewColor(B_TRANSPARENT_COLOR);
    ctx->v->SetDrawingMode(B_OP_OVER);

    ctx->w->Show();

    return 0;
}

static void haiku_close(QEditScreen *s)
{
    WindowState *ctx = (WindowState *)s->priv_data;
    ctx->w->Lock();
    ctx->w->Quit();
    qe_free((WindowState **)&s->priv_data);
    uninit_application();
}

static void haiku_flush(QEditScreen *s)
{
    WindowState *ctx = (WindowState *)s->priv_data;
    //fprintf(stderr, "%s()\n", __FUNCTION__);

    ctx->v->LockLooper();

    // doesn't really help
    ctx->v->Sync();

    ctx->v->UnlockLooper();
}

static int haiku_is_user_input_pending(QEditScreen *s)
{
    /* XXX: do it */
    return 0;
}

extern int32 atomic_get_and_set(int32 *p, int32 v);

/* called when an BMessage is forwarded. dispatch events to qe_handle_event() */
static void haiku_handle_event(void *opaque)
{
    QEditScreen *s = (QEditScreen *)opaque;
    QEmacsState *qs = s->qs;
    WindowState *ctx = (WindowState *)s->priv_data;
    bigtime_t timestamp_ms;
    BMessage *event;
    //fprintf(stderr, "%s()\n", __FUNCTION__);
    int key_state, key = -1;
    QEEvent ev1, *ev = qe_event_clear(&ev1);

    if (read(ctx->events_rd, &event, sizeof(event)) < (signed)sizeof(event))
        return;

    switch(event->what) {
    case B_QUIT_REQUESTED:
        // cancel pending operation
        ev->key_event.type = QE_KEY_EVENT;
        ev->key_event.key = KEY_QUIT;       // C-g
        qe_handle_event(qs, ev);

        // exit qemacs
        ev->key_event.type = QE_KEY_EVENT;
        ev->key_event.key = KEY_EXIT;       // C-x C-c
        qe_handle_event(qs, ev);
        break;

    case _UPDATE_:
        // flush queued repaints
        if (atomic_get_and_set(&repaints, 0)) {
            ev->expose_event.type = QE_EXPOSE_EVENT;
            qe_handle_event(qs, ev);
        }
        break;

    case B_VIEW_RESIZED:
        {
            int32 width, height;

            ctx->v->LockLooper();

            width = ctx->v->Bounds().IntegerWidth() + 1;
            height = ctx->v->Bounds().IntegerHeight() + 1;

            if (width != s->width || height != s->height)
                ctx->v->Invalidate(ctx->v->Bounds());

            s->width = width;
            s->height = height;

            ctx->v->UnlockLooper();

            //ev->expose_event.type = QE_EXPOSE_EVENT;
            //qe_handle_event(qs, ev);
        }
        break;

    case B_MOUSE_MOVED:
        {
            BPoint pt;

            ev->button_event.type = QE_MOTION_EVENT;
            // TODO: set shift state
            ev->button_event.x = (int)pt.x;
            ev->button_event.y = (int)pt.y;
            qe_handle_event(qs, ev);
        }
        break;

    case B_MOUSE_DOWN:
    case B_MOUSE_UP:
        {
            BPoint pt;
            uint32 buttons;

            if (event->what == B_MOUSE_DOWN)
                ev->button_event.type = QE_BUTTON_PRESS_EVENT;
            else
                ev->button_event.type = QE_BUTTON_RELEASE_EVENT;

            if (event->FindPoint("where", &pt) < B_OK)
                pt = BPoint(0,0);
            // TODO: set shift state
            ev->button_event.x = (int)pt.x;
            ev->button_event.y = (int)pt.y;

            if (event->FindInt32("buttons", (int32 *)&buttons) < B_OK)
                buttons = (event->what == B_MOUSE_UP)?0:B_PRIMARY_MOUSE_BUTTON;


            if (buttons & B_PRIMARY_MOUSE_BUTTON)
                ev->button_event.button = QE_BUTTON_LEFT;
            else if (buttons & B_SECONDARY_MOUSE_BUTTON)
                ev->button_event.button = QE_BUTTON_MIDDLE;
            else if (buttons & B_TERTIARY_MOUSE_BUTTON)
                ev->button_event.button = QE_BUTTON_RIGHT;

            qe_handle_event(qs, ev);
        }
        break;

    case B_MOUSE_WHEEL_CHANGED:
        {
            float delta;

            ev->button_event.type = QE_BUTTON_PRESS_EVENT;
            // TODO: set shift state
            ev->button_event.x = 0;
            ev->button_event.y = 0;

            if (event->FindFloat("be:wheel_delta_y", &delta) < B_OK)
                delta = 0.0;

            if (delta > 0)
                ev->button_event.button = QE_WHEEL_DOWN;
            else if (delta < 0)
                ev->button_event.button = QE_WHEEL_UP;
            else
                break;

            qe_handle_event(qs, ev);
        }
        break;

    case B_KEY_UP:
        break;

    case B_KEY_DOWN:
        {
            uint32 state;
            //event->PrintToStream();
            uint32 scancode;
            uint32 raw_char;
            const char *bytes;
            char buff[6];
            int numbytes = 0;
            int i;

            if (event->FindInt64("when", &timestamp_ms) < B_OK)
                timestamp_ms = 0LL;
            if (event->FindInt32("modifiers", (int32 *)&state) < B_OK)
                state = modifiers();
            if (event->FindInt32("key", (int32 *)&scancode) < B_OK)
                scancode = 0;
            if (event->FindInt32("raw_char", (int32 *)&raw_char) < B_OK)
                raw_char = 0;

            /* check for byte[] first,
             * because C-space gives bytes="" (and byte[0] = '\0')
             */
            for (i = 0; i < 5; i++) {
                buff[i] = '\0';
                if (event->FindInt8("byte", i, (int8 *)&buff[i]) < B_OK)
                    break;
            }

            if (i) {
                bytes = buff;
                numbytes = i;
            } else
            if (event->FindString("bytes", &bytes) < B_OK) {
                bytes = "";
            }

            if (!numbytes)
                numbytes = strlen(bytes);

            key_state = 0;
            if (state & B_SHIFT_KEY)
                key_state = KEY_STATE_SHIFT;
            if (state & B_CONTROL_KEY)
                key_state = KEY_STATE_CONTROL;
            if (state & B_LEFT_OPTION_KEY)
                key_state = KEY_STATE_META;
            if (state & B_COMMAND_KEY)
                key_state = KEY_STATE_COMMAND;

            //fprintf(stderr, "state=%d numbytes %d \n", state, numbytes);

            char byte = 0;
            if (numbytes == 1) {
                byte = bytes[0];
                //if (state & B_CONTROL_KEY)
                //    byte = (char)raw_char;
                switch (byte) {
                case B_BACKSPACE:
                    key = KEY_DEL;
                    break;
                case B_TAB:
                    key = KEY_TAB;
                    break;
                case B_ENTER:
                    key = KEY_RET;
                    break;
                case B_ESCAPE:
                    key = KEY_ESC;
                    break;
                case B_SPACE:
                    key = KEY_SPC;
                    break;
                case B_DELETE:
                    key = KEY_DELETE;
                    break;
                case B_INSERT:
                    key = KEY_INSERT;
                    break;
                case B_HOME:
                    key = KEY_HOME;
                    break;
                case B_END:
                    key = KEY_END;
                    break;
                case B_PAGE_UP:
                    key = KEY_PAGEUP;
                    break;
                case B_PAGE_DOWN:
                    key = KEY_PAGEDOWN;
                    break;
                case B_LEFT_ARROW:
                    key = KEY_LEFT;
                    break;
                case B_RIGHT_ARROW:
                    key = KEY_RIGHT;
                    break;
                case B_UP_ARROW:
                    key = KEY_UP;
                    break;
                case B_DOWN_ARROW:
                    key = KEY_DOWN;
                    break;
                case B_FUNCTION_KEY:
                    switch (scancode) {
                    case B_F1_KEY:
                    case B_F2_KEY:
                    case B_F3_KEY:
                    case B_F4_KEY:
                    case B_F5_KEY:
                    case B_F6_KEY:
                    case B_F7_KEY:
                    case B_F8_KEY:
                    case B_F9_KEY:
                    case B_F10_KEY:
                    case B_F11_KEY:
                    case B_F12_KEY:
                        key = KEY_F1 + (scancode - B_F1_KEY);
                        break;
                    case B_PRINT_KEY:
                    case B_SCROLL_KEY:
                    case B_PAUSE_KEY:
                    default:
                        break;
                    }
                    break;
                case 0:
                    break;
                default:
                    if (byte >= ' ' && byte <= '~')
                        key = byte;
                    break;
                }
                if (key < 0)
                    break;
                key = get_modified_key(key, key_state);
            } else {
                const char *p = bytes;
                key = utf8_decode(&p);
            }

            ev->key_event.type = QE_KEY_EVENT;
            ev->key_event.shift = key_state;
            ev->key_event.key = key;
            qe_handle_event(qs, ev);
        }
        break;
    }

    delete event;
}

static void haiku_fill_rectangle(QEditScreen *s,
                                 int x1, int y1, int w, int h, QEColor color)
{
    WindowState *ctx = (WindowState *)s->priv_data;

    BRect r(x1, y1, x1 + w - 1, y1 + h - 1);
    rgb_color c = { (color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff, 0xff };

    ctx->v->LockLooper();
    ctx->v->SetHighColor(c);
    ctx->v->FillRect(r);
    ctx->v->UnlockLooper();
}

static void haiku_xor_rectangle(QEditScreen *s,
                                int x1, int y1, int w, int h, QEColor color)
{
    WindowState *ctx = (WindowState *)s->priv_data;
    drawing_mode oldMode;

    BRect r(x1, y1, x1 + w - 1, y1 + h - 1);

    ctx->v->LockLooper();
    oldMode = ctx->v->DrawingMode();
    ctx->v->SetDrawingMode(B_OP_INVERT);
    ctx->v->FillRect(r);
    ctx->v->SetDrawingMode(oldMode);
    ctx->v->UnlockLooper();
}

static QEFont *haiku_open_font(QEditScreen *s, int style, int size)
{
    //WindowState *ctx = (WindowState *)s->priv_data;
    //fprintf(stderr, "%s()\n", __FUNCTION__);
    QEFont *font;

    font = qe_mallocz(QEFont);
    if (!font)
        return NULL;

    BFont *f;
    uint16 face = 0;
    switch (style & QE_FONT_FAMILY_MASK) {
    default:
    case QE_FONT_FAMILY_FIXED:
        f = new BFont(be_fixed_font);
        break;
    case QE_FONT_FAMILY_SANS:
    case QE_FONT_FAMILY_SERIF:
        /* There isn't a separate default sans and serif font */
        /* for now just only use fixed font */
        //f = new BFont(be_plain_font);
        f = new BFont(be_fixed_font);
        break;
    }
    if (style & QE_FONT_STYLE_NORM)
        face |= B_REGULAR_FACE;
    if (style & QE_FONT_STYLE_BOLD)
        face |= B_BOLD_FACE;
    if (style & QE_FONT_STYLE_ITALIC)
        face |= B_ITALIC_FACE;
    if (style & QE_FONT_STYLE_UNDERLINE)
        face |= B_UNDERSCORE_FACE; // not really supported IIRC
    if (style & QE_FONT_STYLE_LINE_THROUGH)
        face |= B_STRIKEOUT_FACE; // not really supported IIRC
    if (face)
        f->SetFace(face);

    font_height height;
    f->GetHeight(&height);
    font->ascent = (int)height.ascent;
    font->descent = (int)(height.descent + height.leading + 1);
    font->priv_data = f;
    return font;
}

static void haiku_close_font(QEditScreen *s, QEFont **fontp)
{
    QEFont *font = *fontp;

    if (font) {
        BFont *f = (BFont *)font->priv_data;
        delete f;
        /* Clear structure to force crash if font is still used after
         * close_font.
         */
        memset(font, 0, sizeof(*font));
        qe_free(fontp);
    }
}

static void haiku_text_metrics(QEditScreen *s, QEFont *font,
                               QECharMetrics *metrics,
                               const char32_t *str, int len)
{
    //TODO: use BFont::GetEscapements() or StringWidth()
    int i, x;

    metrics->font_ascent = font->ascent;
    metrics->font_descent = font->descent;
    x = 0;
    for (i = 0; i < len; i++)
        x += font_xsize;
    metrics->width = x;
}

static void haiku_draw_text(QEditScreen *s, QEFont *font,
                            int x1, int y, const char32_t *str, int len,
                            QEColor color)
{
    WindowState *ctx = (WindowState *)s->priv_data;
    BFont *f = (BFont *)(font->priv_data);
    int i;
    //fprintf(stderr, "%s()\n", __FUNCTION__);

    rgb_color c = { (color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff, 0xff };

    ctx->v->LockLooper();

    ctx->v->SetHighColor(c);
    ctx->v->SetLowColor(B_TRANSPARENT_COLOR);
    ctx->v->SetFont(f);
    ctx->v->MovePenTo(x1, y - 1);

    char buf[10];
    char32_t cc;

    BString text;
    for (i = 0; i < len; i++) {
        cc = str[i];
        buf[utf8_encode(buf, cc)] = '\0';
        text << buf;
    }
    ctx->v->DrawString(text.String());

    /* underline synthesis */
    if (font->style & (QE_FONT_STYLE_UNDERLINE | QE_FONT_STYLE_LINE_THROUGH)) {
        int dy, h, w;
        BFont *f = (BFont *)font->priv_data;
        h = (font->descent + 2) / 4 - 1;
        if (h < 0)
            h = 0;
        w = (int)f->StringWidth(text.String()) - 1;
        if (font->style & QE_FONT_STYLE_UNDERLINE) {
            dy = (font->descent + 1) / 3;
            ctx->v->FillRect(BRect(x1, y + dy, x1 + w, y + dy + h));
        }
        if (font->style & QE_FONT_STYLE_LINE_THROUGH) {
            dy = -(font->ascent / 2 - 1);
            ctx->v->FillRect(BRect(x1, y + dy, x1 + w, y + dy + h));
        }
    }

    ctx->v->UnlockLooper();

    //TextOutW(haiku_ctx.hdc, x1, y - font->ascent, buf, len);
}

static void haiku_set_clip(QEditScreen *s,
                           int x, int y, int w, int h)
{
    WindowState *ctx = (WindowState *)s->priv_data;
    //fprintf(stderr, "%s(,%d, %d, %d, %d)\n", __FUNCTION__, x, y, w, h);

    BRegion clip(BRect(x, y, x + w - 1, y + h - 1));

    ctx->v->LockLooper();

    ctx->v->ConstrainClippingRegion(&clip);

    ctx->v->UnlockLooper();
}

static int haiku_bmp_alloc(QEditScreen *s, QEBitmap *b)
{
    BBitmap *bitmap;
    color_space space = B_RGBA32;
    uint32 flags = 0;

    b->format = s->bitmap_format;
    if (b->flags & QEBITMAP_FLAG_VIDEO) {
        b->format = s->video_format;
    }
    //fprintf(stderr, "%s(, [w %d, h %d])\n", __FUNCTION__, b->width, b->height);
    switch (b->format) {
    case QEBITMAP_FORMAT_RGB565:
        space = B_RGB16;
        break;
    case QEBITMAP_FORMAT_RGB555:
        space = B_RGB15;
        break;
    case QEBITMAP_FORMAT_RGB24:
        space = B_RGB24;
        break;
    case QEBITMAP_FORMAT_RGBA32:
        space = B_RGBA32;
        break;
    case QEBITMAP_FORMAT_YUV420P:
        // we don't support planar overlays
    default:
        return -1;
    }

    BRect bounds(0, 0, b->width - 1, b->height - 1);
    bitmap = new BBitmap(bounds, flags, space);
    if (bitmap->InitCheck() != B_OK) {
        delete bitmap;
        return -1;
    }
    b->priv_data = bitmap;

    return 0;
}

static void haiku_bmp_free(qe__unused__ QEditScreen *s, QEBitmap *b)
{
    BBitmap *bitmap = (BBitmap *)b->priv_data;
    delete bitmap;
    b->priv_data = NULL;
}

static void haiku_full_screen(QEditScreen *s, int full_screen)
{
    WindowState *ctx = (WindowState *)s->priv_data;

    BMessenger msgr(ctx->v->Looper());
    msgr.SendMessage(B_ZOOM);
}

static QEDisplay haiku_dpy = {
    "haiku", 1, 1,
    haiku_probe,
    haiku_init,
    haiku_close,
    haiku_flush,
    haiku_is_user_input_pending,
    haiku_fill_rectangle,
    haiku_xor_rectangle,
    haiku_open_font,
    haiku_close_font,
    haiku_text_metrics,
    haiku_draw_text,
    haiku_set_clip,
    NULL, /* dpy_selection_activate */
    NULL, /* dpy_selection_request */
    NULL, /* dpy_invalidate */
    NULL, /* dpy_cursor_at */
    haiku_bmp_alloc, /* dpy_bmp_alloc */
    haiku_bmp_free, /* dpy_bmp_free */
    NULL, /* dpy_bmp_draw */
    NULL, /* dpy_bmp_lock */
    NULL, /* dpy_bmp_unlock */
    NULL, /* dpy_draw_picture */
    haiku_full_screen,
    NULL, /* dpy_describe */
    NULL, /* dpy_sound_bell */
    NULL, /* dpy_suspend */
    qe_dpy_error, /* dpy_error */
    NULL, /* next */
};

static int haiku_module_init(QEmacsState *qs)
{
    /* override default res path, to find config file at native location */
    BPath path;
    BString old(":");
    old << qs->res_path;
    qs->res_path[0] = '\0';
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
        path.Append("qemacs");
        pstrcat(qs->res_path, sizeof(qs->res_path), path.Path());
    }
    pstrcat(qs->res_path, sizeof(qs->res_path), old.String());

    if (force_tty)
        return 0;

    return qe_register_display(qs, &haiku_dpy);
}

qe_module_init(haiku_module_init);
