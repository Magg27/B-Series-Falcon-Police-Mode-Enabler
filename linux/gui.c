#define _POSIX_C_SOURCE 200809L
#include "pme.h"
#include "serial.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <errno.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    Display *display;
    Window window;
    GC graphics;
    unsigned long background;
    unsigned long foreground;
    unsigned long border;
    unsigned long button;
    unsigned long accent;
    char ports[64][256];
    int port_count;
    int selected_port;
    int selected_baud;
    int untested;
    int pending;
    _Atomic int busy;
    char status[512];
    pthread_mutex_t lock;
} Interface;

typedef struct {
    Interface *interface;
    char device[256];
    int baud;
    int operation;
    int untested;
} Task;

static const int baud_rates[] = {9600, 19200, 38400, 57600, 115200};

static unsigned long color(Display *display, const char *name, unsigned long fallback)
{
    XColor pixel, exact;
    if (XAllocNamedColor(display, DefaultColormap(display, DefaultScreen(display)),
                         name, &pixel, &exact)) return pixel.pixel;
    return fallback;
}

static void text(Interface *ui, int x, int y, const char *value)
{
    XSetForeground(ui->display, ui->graphics, ui->foreground);
    XDrawString(ui->display, ui->window, ui->graphics, x, y, value, (int)strlen(value));
}

static void rectangle(Interface *ui, int x, int y, unsigned int width,
                      unsigned int height, unsigned long fill)
{
    XSetForeground(ui->display, ui->graphics, fill);
    XFillRectangle(ui->display, ui->window, ui->graphics, x, y, width, height);
    XSetForeground(ui->display, ui->graphics, ui->border);
    XDrawRectangle(ui->display, ui->window, ui->graphics, x, y, width, height);
}

static void button(Interface *ui, int x, int y, int width, const char *label, int active)
{
    rectangle(ui, x, y, (unsigned int)width, 31, active ? ui->accent : ui->button);
    text(ui, x + 13, y + 20, label);
}

static void line_wrapped(Interface *ui, int x, int y, const char *message, size_t limit)
{
    char buffer[100];
    const char *position = message;
    for (int count = 0; count < 4 && *position; count++) {
        size_t remaining = strlen(position);
        size_t length = remaining > limit ? limit : remaining;
        if (remaining > limit) {
            size_t split = length;
            while (split > 15 && position[split] != ' ') split--;
            if (split > 15) length = split;
        }
        if (length >= sizeof(buffer)) length = sizeof(buffer) - 1;
        memcpy(buffer, position, length);
        buffer[length] = 0;
        text(ui, x, y + count * 19, buffer);
        position += length;
        while (*position == ' ') position++;
    }
}

static void draw(Interface *ui)
{
    XSetForeground(ui->display, ui->graphics, ui->background);
    XFillRectangle(ui->display, ui->window, ui->graphics, 0, 0, 510, 470);
    text(ui, 24, 30, "Falcon Police Mode Enabler");
    text(ui, 24, 51, "BF Falcon 5R29 tested | BA Falcon, SX/SY Territory untested");
    text(ui, 24, 90, "Port");
    rectangle(ui, 93, 69, 320, 31, ui->button);
    const char *device = ui->port_count ? ui->ports[ui->selected_port] : "No serial port detected";
    char clipped[50];
    snprintf(clipped, sizeof(clipped), "%.46s", device);
    text(ui, 103, 90, clipped);
    button(ui, 420, 69, 64, "Scan", 0);
    text(ui, 24, 140, "Speed");
    button(ui, 93, 118, 180, "", 0);
    char rate[32];
    snprintf(rate, sizeof(rate), "%d baud  v", baud_rates[ui->selected_baud]);
    text(ui, 106, 139, rate);
    text(ui, 24, 172, "Select port/speed, then read cluster status.");
    button(ui, 24, 188, 145, "Read Status", 0);
    button(ui, 177, 188, 145, "Enable", ui->pending == PME_ENABLE);
    button(ui, 330, 188, 154, "Disable", ui->pending == PME_DISABLE);
    rectangle(ui, 24, 244, 17, 17, ui->button);
    if (ui->untested) text(ui, 28, 257, "X");
    text(ui, 52, 257, "Allow changes on unverified cluster versions");
    if (ui->pending && !ui->busy) {
        text(ui, 24, 292, "Confirm the configuration change:");
        button(ui, 310, 272, 174, "Confirm Write", 1);
    } else {
        text(ui, 24, 292, "Changes require an explicit confirmation.");
    }
    XSetForeground(ui->display, ui->graphics, ui->border);
    XDrawLine(ui->display, ui->window, ui->graphics, 24, 311, 484, 311);
    text(ui, 24, 336, ui->busy ? "Working..." : "Status");
    char message[sizeof(ui->status)];
    pthread_mutex_lock(&ui->lock);
    snprintf(message, sizeof(message), "%s", ui->status);
    pthread_mutex_unlock(&ui->lock);
    line_wrapped(ui, 24, 360, message, 74);
    XFlush(ui->display);
}

static void *worker(void *argument)
{
    Task *task = argument;
    char output[512];
    int fd = serial_open(task->device, task->baud);
    if (fd < 0) {
        snprintf(output, sizeof(output), "Could not open %.160s: %.150s", task->device, strerror(errno));
    } else {
        PmeTransport transport = serial_transport(&fd);
        PmeState state;
        pme_run(&transport, task->operation, task->untested, &state, output, sizeof(output));
        serial_close(fd);
    }
    pthread_mutex_lock(&task->interface->lock);
    snprintf(task->interface->status, sizeof(task->interface->status), "%s", output);
    task->interface->busy = 0;
    pthread_mutex_unlock(&task->interface->lock);
    free(task);
    return NULL;
}

static void start_task(Interface *ui, int operation)
{
    if (ui->busy || !ui->port_count) return;
    Task *task = calloc(1, sizeof(*task));
    if (!task) return;
    task->interface = ui;
    snprintf(task->device, sizeof(task->device), "%s", ui->ports[ui->selected_port]);
    task->baud = baud_rates[ui->selected_baud];
    task->operation = operation;
    task->untested = ui->untested;
    ui->busy = 1;
    ui->pending = 0;
    snprintf(ui->status, sizeof(ui->status), "Communicating with %s...", task->device);
    pthread_t thread;
    if (pthread_create(&thread, NULL, worker, task) != 0) {
        ui->busy = 0;
        free(task);
        snprintf(ui->status, sizeof(ui->status), "Could not start communication thread.");
    } else pthread_detach(thread);
}

static void scan(Interface *ui)
{
    char previous[256] = "";
    if (ui->port_count) snprintf(previous, sizeof(previous), "%s", ui->ports[ui->selected_port]);
    ui->port_count = serial_ports(ui->ports, 64);
    ui->selected_port = 0;
    for (int i = 0; i < ui->port_count; i++)
        if (strcmp(ui->ports[i], previous) == 0) ui->selected_port = i;
    ui->pending = 0;
    if (!ui->port_count) snprintf(ui->status, sizeof(ui->status), "No USB serial adapter detected.");
}

static void click(Interface *ui, int x, int y)
{
    if (ui->busy) return;
    if (y >= 69 && y <= 101) {
        if (x >= 420 && x <= 484) scan(ui);
        else if (x >= 93 && x <= 413 && ui->port_count) {
            ui->selected_port = (ui->selected_port + 1) % ui->port_count;
            ui->pending = 0;
        }
    } else if (y >= 118 && y <= 150 && x >= 93 && x <= 273) {
        ui->selected_baud = (ui->selected_baud + 1) % 5;
        ui->pending = 0;
    } else if (y >= 188 && y <= 220) {
        if (x >= 24 && x < 169) start_task(ui, PME_READ);
        if (x >= 177 && x < 322) ui->pending = PME_ENABLE;
        if (x >= 330 && x <= 484) ui->pending = PME_DISABLE;
    } else if (y >= 241 && y <= 265 && x >= 24 && x <= 484) {
        ui->untested = !ui->untested;
        ui->pending = 0;
    } else if (y >= 272 && y <= 303 && x >= 310 && x <= 484 && ui->pending) {
        start_task(ui, ui->pending);
    }
}

int main(int argc, char **argv)
{
    Interface ui = {0};
    pthread_mutex_init(&ui.lock, NULL);
    ui.display = XOpenDisplay(NULL);
    if (!ui.display) { fputs("An X11 display is required.\n", stderr); return 1; }
    int screen = DefaultScreen(ui.display);
    ui.background = color(ui.display, "#F6F6F6", WhitePixel(ui.display, screen));
    ui.foreground = color(ui.display, "#151515", BlackPixel(ui.display, screen));
    ui.border = color(ui.display, "#848484", BlackPixel(ui.display, screen));
    ui.button = color(ui.display, "#E8E8E8", WhitePixel(ui.display, screen));
    ui.accent = color(ui.display, "#C3D2E6", WhitePixel(ui.display, screen));
    ui.window = XCreateSimpleWindow(ui.display, RootWindow(ui.display, screen),
                                     120, 120, 510, 460, 1, ui.border, ui.background);
    XStoreName(ui.display, ui.window, "Falcon Police Mode Enabler");
    XSizeHints limits = {.flags = PMinSize | PMaxSize, .min_width = 510, .min_height = 460,
                         .max_width = 510, .max_height = 460};
    XSetWMNormalHints(ui.display, ui.window, &limits);
    XSelectInput(ui.display, ui.window, ExposureMask | ButtonPressMask | KeyPressMask);
    Atom close = XInternAtom(ui.display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(ui.display, ui.window, &close, 1);
    ui.graphics = XCreateGC(ui.display, ui.window, 0, NULL);
    ui.selected_baud = 2;
    snprintf(ui.status, sizeof(ui.status), "Connect an ELM327 USB adapter. Reading is safe; writes are confirmed separately.");
    scan(&ui);
    if (argc == 3 && strcmp(argv[1], "--port") == 0) {
        if (strlen(argv[2]) >= 256) { fputs("Port path too long.\n", stderr); return 2; }
        snprintf(ui.ports[0], sizeof(ui.ports[0]), "%s", argv[2]);
        ui.port_count = 1;
        ui.selected_port = 0;
        snprintf(ui.status, sizeof(ui.status), "Ready. Read cluster status before enabling or disabling Police Mode.");
    }
    XMapWindow(ui.display, ui.window);
    int running = 1;
    while (running) {
        while (XPending(ui.display)) {
            XEvent event;
            XNextEvent(ui.display, &event);
            if (event.type == ButtonPress) click(&ui, event.xbutton.x, event.xbutton.y);
            if (event.type == ClientMessage && (Atom)event.xclient.data.l[0] == close) running = 0;
            if (event.type == KeyPress) {
                char key[8];
                KeySym symbol;
                int count = XLookupString(&event.xkey, key, sizeof(key), &symbol, NULL);
                if (symbol == XK_Escape) ui.pending = 0;
                if (count && key[0] == 'q') running = 0;
            }
        }
        if (running) draw(&ui);
        struct timespec wait = {.tv_sec = 0, .tv_nsec = 70000000};
        nanosleep(&wait, NULL);
    }
    XDestroyWindow(ui.display, ui.window);
    XCloseDisplay(ui.display);
    return 0;
}
