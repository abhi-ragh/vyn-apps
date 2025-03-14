#include <vte/vte.h>
#include <gtk/gtk.h>
#include <glib.h>

static void
child_ready(VteTerminal *terminal, GPid pid, GError *error, gpointer user_data)
{
    // Suppress unused parameter warnings
    (void)terminal;
    (void)pid;
    (void)user_data;
    
    if (error) {
        g_printerr("Error launching shell: %s\n", error->message);
        g_error_free(error);
        gtk_main_quit();
    }
}

static gboolean
on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    // Suppress unused parameter warning
    (void)user_data;
    
    // Handle Ctrl+Shift+C and Ctrl+Shift+V for copy-paste
    if ((event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) {
        if (event->keyval == GDK_KEY_c) {
            vte_terminal_copy_clipboard_format(VTE_TERMINAL(widget), VTE_FORMAT_TEXT);
            return TRUE;
        } else if (event->keyval == GDK_KEY_v) {
            vte_terminal_paste_clipboard(VTE_TERMINAL(widget));
            return TRUE;
        }
    }
    return FALSE;
}

static void
on_terminal_title_changed(VteTerminal *terminal, gpointer user_data)
{
    GtkWindow *window = GTK_WINDOW(user_data);
    const char *title = vte_terminal_get_window_title(terminal);
    if (title) {
        char *full_title = g_strdup_printf("Vyn Terminal - %s", title);
        gtk_window_set_title(window, full_title);
        g_free(full_title);
    } else {
        gtk_window_set_title(window, "Vyn Terminal");
    }
}

int
main(int argc, char *argv[])
{
    GtkWidget *window, *terminal;
    GtkWidget *scrolled_window;
    GdkRGBA bg_color, fg_color;
    char **command_argv;
    char *shell;
    
    // Initialize GTK
    gtk_init(&argc, &argv);
    
    // Create window
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Vyn Terminal");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    
    // Create VTE terminal
    terminal = vte_terminal_new();
    
    // Set terminal colors
    gdk_rgba_parse(&bg_color, "#282828");
    gdk_rgba_parse(&fg_color, "#F8F8F2");
    vte_terminal_set_colors(VTE_TERMINAL(terminal), &fg_color, &bg_color, NULL, 0);
    
    // Set terminal preferences
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(terminal), 10000);
    vte_terminal_set_scroll_on_output(VTE_TERMINAL(terminal), FALSE);
    vte_terminal_set_scroll_on_keystroke(VTE_TERMINAL(terminal), TRUE);
    vte_terminal_set_cursor_blink_mode(VTE_TERMINAL(terminal), VTE_CURSOR_BLINK_ON);
    vte_terminal_set_cursor_shape(VTE_TERMINAL(terminal), VTE_CURSOR_SHAPE_BLOCK);
    vte_terminal_set_mouse_autohide(VTE_TERMINAL(terminal), TRUE);
    
    // Connect signals
    g_signal_connect(terminal, "key-press-event", G_CALLBACK(on_key_press), NULL);
    g_signal_connect(terminal, "window-title-changed", G_CALLBACK(on_terminal_title_changed), window);
    
    // Create scrolled window for terminal
    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled_window), terminal);
    gtk_container_add(GTK_CONTAINER(window), scrolled_window);
    
    // Show all widgets
    gtk_widget_show_all(window);
    
    // Start ZSH shell
    shell = g_strdup("/bin/zsh");
    if (!g_file_test(shell, G_FILE_TEST_EXISTS)) {
        g_free(shell);
        shell = g_strdup("/bin/bash");  // Fallback to bash if zsh is not available
    }
    
    command_argv = g_new0(char *, 2);
    command_argv[0] = g_strdup(shell);  // Must duplicate the string
    command_argv[1] = NULL;
    
    vte_terminal_spawn_async(
        VTE_TERMINAL(terminal),
        VTE_PTY_DEFAULT,
        NULL,       // working directory
        command_argv,
        NULL,       // environment
        G_SPAWN_SEARCH_PATH,
        NULL, NULL, // child setup
        NULL,       // child pid
        -1,         // timeout
        NULL,       // cancellable
        child_ready,
        NULL        // user data
    );
    
    g_free(shell);
    g_strfreev(command_argv);  // This properly frees the array and its contents
    
    // Start main loop
    gtk_main();
    
    return 0;
}
