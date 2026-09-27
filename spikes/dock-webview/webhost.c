/* dockspike-webhost: a WebKitGTK view inside a GtkPlug.
 *
 * argv[1] = X window id to embed into. Prints "XID <plug window id>" on stdout
 * so the extension can position it. Exits when stdin closes (REAPER gone). */

#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <stdio.h>
#include <stdlib.h>
#include <webkit2/webkit2.h>

static const char* kPage =
    "<!doctype html><html><head><meta charset=utf-8><style>"
    "html,body{margin:0;height:100%;font:14px/1.45 sans-serif;background:#1d2533;color:#e6e9ef}"
    "body{display:flex;flex-direction:column}"
    "header{padding:8px 12px;background:#2b3a55;display:flex;justify-content:space-between}"
    "#log{flex:1;overflow:auto;padding:12px}"
    ".m{max-width:80%;padding:8px 10px;border-radius:8px;margin:0 0 10px}"
    ".u{background:#3b5b8c;margin-left:auto}.a{background:#2a3242}"
    "code,pre{font-family:monospace;background:#0f141d;border-radius:4px}"
    "pre{padding:8px;white-space:pre-wrap}"
    "footer{display:flex;gap:6px;padding:8px;background:#2b3a55}"
    "input{flex:1;padding:6px;border:0;border-radius:4px}"
    "</style></head><body>"
    "<header><b>ReaClaw Chat &middot; dock test</b><span id=info></span></header>"
    "<div id=log>"
    "<div class='m u'>make the kick louder</div>"
    "<div class='m a'>Raised <b>Kick</b> by <code>+3 dB</code> (now -4.2 dB). Undo?"
    "<pre>POST /state/tracks/0 {\"volume_db\": -4.2}</pre></div>"
    "</div>"
    "<footer><input id=inp placeholder='Type a message'><button>Send</button></footer>"
    "<script>"
    "function tick(){document.getElementById('info').textContent="
    "innerWidth+'\\u00d7'+innerHeight+' px \\u00b7 '+new Date().toLocaleTimeString()}"
    "tick();setInterval(tick,500);addEventListener('resize',tick);"
    "document.querySelector('button').onclick=function(){var i=document.getElementById('inp');"
    "if(!i.value)return;var d=document.createElement('div');d.className='m u';"
    "d.textContent=i.value;document.getElementById('log').appendChild(d);i.value=''};"
    "</script></body></html>";

static gboolean on_stdin(GIOChannel* ch, GIOCondition cond, gpointer data) {
    (void)data;
    gchar buf[256];
    gsize n = 0;
    if (cond & (G_IO_HUP | G_IO_ERR) ||
        g_io_channel_read_chars(ch, buf, sizeof(buf), &n, NULL) != G_IO_STATUS_NORMAL || n == 0) {
        gtk_main_quit();
        return FALSE;
    }
    return TRUE;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <parent-xid>\n", argv[0]);
        return 2;
    }
    Window parent = (Window)strtoul(argv[1], NULL, 10);
    gdk_set_allowed_backends("x11");
    gtk_init(&argc, &argv);

    GtkWidget* plug = gtk_plug_new(parent);
    GtkWidget* view = webkit_web_view_new();
    gtk_container_add(GTK_CONTAINER(plug), view);
    webkit_web_view_load_html(WEBKIT_WEB_VIEW(view), kPage, NULL);
    gtk_widget_show_all(plug);

    printf("XID %lu\n", (unsigned long)gtk_plug_get_id(GTK_PLUG(plug)));
    fflush(stdout);

    GIOChannel* in = g_io_channel_unix_new(0);
    g_io_add_watch(in, G_IO_IN | G_IO_HUP | G_IO_ERR, on_stdin, NULL);
    g_signal_connect(plug, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_main();
    return 0;
}
