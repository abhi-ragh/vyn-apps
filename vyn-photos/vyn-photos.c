#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <string.h>

// Define the application structure
typedef struct {
    GtkWidget *image;
    GtkWidget *status_bar;
    GtkWidget *window;
    GList *image_list;
    GList *current_image;
    gdouble zoom_level;
    gboolean fit_to_window;
    GdkPixbuf *original_pixbuf; // Store original pixbuf for zoom operations
} VynPhotosApp;

// Function prototypes
static void update_image(VynPhotosApp *app, const gchar *path);
static void update_status(VynPhotosApp *app);
static void open_image(GtkWidget *widget, gpointer data);
static void zoom_in(GtkWidget *widget, gpointer data);
static void zoom_out(GtkWidget *widget, gpointer data);
static void fit_to_window(GtkWidget *widget, gpointer data);
static void navigate_image(GtkWidget *widget, gpointer data);
static gboolean key_press_event(GtkWidget *widget, GdkEventKey *event, gpointer data);

// Function to update the displayed image
static void update_image(VynPhotosApp *app, const gchar *path) {
    if (!path) return;
    
    GError *error = NULL;
    
    // Free the previous original pixbuf if it exists
    if (app->original_pixbuf) {
        g_object_unref(app->original_pixbuf);
        app->original_pixbuf = NULL;
    }
    
    // Load the new image
    app->original_pixbuf = gdk_pixbuf_new_from_file(path, &error);
    
    if (error) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_ERROR,
                                                   GTK_BUTTONS_CLOSE,
                                                   "Failed to load image: %s",
                                                   error->message);
        g_error_free(error);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }
    
    if (!app->original_pixbuf) return;
    
    GdkPixbuf *display_pixbuf = NULL;
    
    if (app->fit_to_window) {
        GtkAllocation allocation;
        gtk_widget_get_allocation(GTK_WIDGET(app->window), &allocation);
        
        // Get original dimensions
        int orig_width = gdk_pixbuf_get_width(app->original_pixbuf);
        int orig_height = gdk_pixbuf_get_height(app->original_pixbuf);
        
        // Calculate scale to fit window (accounting for some padding)
        int window_width = allocation.width - 20;
        int window_height = allocation.height - 100; // Account for toolbar and statusbar
        
        double scale_x = (double)window_width / orig_width;
        double scale_y = (double)window_height / orig_height;
        double scale = MIN(scale_x, scale_y);
        
        int new_width = (int)(orig_width * scale);
        int new_height = (int)(orig_height * scale);
        
        if (new_width > 0 && new_height > 0) {
            display_pixbuf = gdk_pixbuf_scale_simple(app->original_pixbuf,
                                                   new_width,
                                                   new_height,
                                                   GDK_INTERP_BILINEAR);
        } else {
            display_pixbuf = g_object_ref(app->original_pixbuf);
        }
    } else {
        // Apply zoom level
        int orig_width = gdk_pixbuf_get_width(app->original_pixbuf);
        int orig_height = gdk_pixbuf_get_height(app->original_pixbuf);
        
        int new_width = (int)(orig_width * app->zoom_level);
        int new_height = (int)(orig_height * app->zoom_level);
        
        if (new_width > 0 && new_height > 0) {
            display_pixbuf = gdk_pixbuf_scale_simple(app->original_pixbuf,
                                                   new_width,
                                                   new_height,
                                                   GDK_INTERP_BILINEAR);
        } else {
            display_pixbuf = g_object_ref(app->original_pixbuf);
        }
    }
    
    if (display_pixbuf) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(app->image), display_pixbuf);
        g_object_unref(display_pixbuf);
    }
    
    update_status(app);
}

// Function to update the status bar
static void update_status(VynPhotosApp *app) {
    if (app->current_image && app->current_image->data) {
        gchar *basename = g_path_get_basename((gchar *)app->current_image->data);
        gchar *status = g_strdup_printf("%s - Zoom: %.0f%%", 
                                        basename,
                                        app->zoom_level * 100);
        gtk_statusbar_pop(GTK_STATUSBAR(app->status_bar), 0);
        gtk_statusbar_push(GTK_STATUSBAR(app->status_bar), 0, status);
        g_free(basename);
        g_free(status);
    }
}

// Function to open an image file
static void open_image(GtkWidget *widget, gpointer data) {
    VynPhotosApp *app = (VynPhotosApp *)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Open Image",
                                                    GTK_WINDOW(app->window),
                                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                                    "_Cancel",
                                                    GTK_RESPONSE_CANCEL,
                                                    "_Open",
                                                    GTK_RESPONSE_ACCEPT,
                                                    NULL);
    
    // Clean up existing image list
    if (app->image_list) {
        g_list_free_full(app->image_list, g_free);
        app->image_list = NULL;
        app->current_image = NULL;
    }
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            gchar *dir_path = g_path_get_dirname(filename);
            GDir *dir = g_dir_open(dir_path, 0, NULL);
            if (dir) {
                const gchar *entry;
                
                // Build image list
                while ((entry = g_dir_read_name(dir))) {
                    gchar *full_path = g_build_filename(dir_path, entry, NULL);
                    if (gdk_pixbuf_get_file_info(full_path, NULL, NULL)) {
                        app->image_list = g_list_append(app->image_list, full_path);
                    } else {
                        g_free(full_path);
                    }
                }
                g_dir_close(dir);
                
                // Sort the image list
                app->image_list = g_list_sort(app->image_list, (GCompareFunc)g_strcmp0);
                
                // Find current image in list
                app->current_image = g_list_find_custom(app->image_list, filename, (GCompareFunc)g_strcmp0);
                if (!app->current_image && app->image_list) {
                    app->current_image = app->image_list;
                }
                
                if (app->current_image) {
                    app->zoom_level = 1.0; // Reset zoom level
                    update_image(app, (gchar *)app->current_image->data);
                }
            }
            g_free(dir_path);
            g_free(filename);
        }
    }
    gtk_widget_destroy(dialog);
}

// Function to zoom in
static void zoom_in(GtkWidget *widget, gpointer data) {
    VynPhotosApp *app = (VynPhotosApp *)data;
    app->zoom_level *= 1.1;
    if (app->zoom_level > 3.0) { // Limit zoom level to 300%
        app->zoom_level = 3.0;
    }
    app->fit_to_window = FALSE;
    
    if (app->current_image && app->current_image->data) {
        update_image(app, (gchar *)app->current_image->data);
    }
}

// Function to zoom out
static void zoom_out(GtkWidget *widget, gpointer data) {
    VynPhotosApp *app = (VynPhotosApp *)data;
    app->zoom_level *= 0.9;
    if (app->zoom_level < 0.1) { // Limit zoom level to 10%
        app->zoom_level = 0.1;
    }
    app->fit_to_window = FALSE;
    
    if (app->current_image && app->current_image->data) {
        update_image(app, (gchar *)app->current_image->data);
    }
}

// Function to fit image to window
static void fit_to_window(GtkWidget *widget, gpointer data) {
    VynPhotosApp *app = (VynPhotosApp *)data;
    app->fit_to_window = TRUE;
    
    if (app->current_image && app->current_image->data) {
        update_image(app, (gchar *)app->current_image->data);
    }
}

// Function to navigate through images
static void navigate_image(GtkWidget *widget, gpointer data) {
    VynPhotosApp *app = data;
    gint direction = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "direction"));
    
    if (!app->image_list || !app->current_image) return;
    
    if (direction > 0) {
        GList *next = g_list_next(app->current_image);
        if (next) {
            app->current_image = next;
        } else {
            app->current_image = app->image_list; // Wrap around to first image
        }
    } else {
        GList *prev = g_list_previous(app->current_image);
        if (prev) {
            app->current_image = prev;
        } else {
            app->current_image = g_list_last(app->image_list); // Wrap around to last image
        }
    }
    
    if (app->current_image && app->current_image->data) {
        update_image(app, (gchar *)app->current_image->data);
    }
}

// Function to handle key press events
static gboolean key_press_event(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    VynPhotosApp *app = (VynPhotosApp *)data;
    
    switch (event->keyval) {
        case GDK_KEY_Left:
        case GDK_KEY_KP_Left:
            if (app->image_list && app->current_image) {
                GList *prev = g_list_previous(app->current_image);
                if (prev) {
                    app->current_image = prev;
                } else {
                    app->current_image = g_list_last(app->image_list);
                }
                update_image(app, (gchar *)app->current_image->data);
            }
            return TRUE;
        case GDK_KEY_Right:
        case GDK_KEY_KP_Right:
            if (app->image_list && app->current_image) {
                GList *next = g_list_next(app->current_image);
                if (next) {
                    app->current_image = next;
                } else {
                    app->current_image = app->image_list;
                }
                update_image(app, (gchar *)app->current_image->data);
            }
            return TRUE;
        case GDK_KEY_q:
            if (event->state & GDK_CONTROL_MASK) {
                gtk_main_quit();
            }
            return TRUE;
        case GDK_KEY_plus:
        case GDK_KEY_KP_Add:
            zoom_in(NULL, app);
            return TRUE;
        case GDK_KEY_minus:
        case GDK_KEY_KP_Subtract:
            zoom_out(NULL, app);
            return TRUE;
        case GDK_KEY_0:
        case GDK_KEY_KP_0:
            fit_to_window(NULL, app);
            return TRUE;
        default:
            return FALSE;
    }
}

// Main function
int main(int argc, char *argv[]) {
    GtkWidget *toolbar;
    GtkWidget *scrolled_window, *box;
    VynPhotosApp app = {0};
    GtkToolItem *open_toolitem, *prev_toolitem, *next_toolitem, *zoom_in_toolitem, *zoom_out_toolitem, *fit_toolitem;

    gtk_init(&argc, &argv);

    // Create main window
    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "Vyn Photos");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 800, 600);
    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Create main container
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app.window), box);

    // Create toolbar
    toolbar = gtk_toolbar_new();
    gtk_box_pack_start(GTK_BOX(box), toolbar, FALSE, FALSE, 0);

    // Toolbar buttons
    open_toolitem = gtk_tool_button_new(gtk_image_new_from_icon_name("document-open", GTK_ICON_SIZE_LARGE_TOOLBAR), "Open");
    g_signal_connect(open_toolitem, "clicked", G_CALLBACK(open_image), &app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(open_toolitem), -1);

    prev_toolitem = gtk_tool_button_new(gtk_image_new_from_icon_name("go-previous", GTK_ICON_SIZE_LARGE_TOOLBAR), "Previous");
    g_object_set_data(G_OBJECT(prev_toolitem), "direction", GINT_TO_POINTER(-1));
    g_signal_connect(prev_toolitem, "clicked", G_CALLBACK(navigate_image), &app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(prev_toolitem), -1);

    next_toolitem = gtk_tool_button_new(gtk_image_new_from_icon_name("go-next", GTK_ICON_SIZE_LARGE_TOOLBAR), "Next");
    g_object_set_data(G_OBJECT(next_toolitem), "direction", GINT_TO_POINTER(1));
    g_signal_connect(next_toolitem, "clicked", G_CALLBACK(navigate_image), &app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(next_toolitem), -1);

    zoom_in_toolitem = gtk_tool_button_new(gtk_image_new_from_icon_name("zoom-in", GTK_ICON_SIZE_LARGE_TOOLBAR), "Zoom In");
    g_signal_connect(zoom_in_toolitem, "clicked", G_CALLBACK(zoom_in), &app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(zoom_in_toolitem), -1);

    zoom_out_toolitem = gtk_tool_button_new(gtk_image_new_from_icon_name("zoom-out", GTK_ICON_SIZE_LARGE_TOOLBAR), "Zoom Out");
    g_signal_connect(zoom_out_toolitem, "clicked", G_CALLBACK(zoom_out), &app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(zoom_out_toolitem), -1);

    fit_toolitem = gtk_tool_button_new(gtk_image_new_from_icon_name("zoom-fit-best", GTK_ICON_SIZE_LARGE_TOOLBAR), "Fit to Window");
    g_signal_connect(fit_toolitem, "clicked", G_CALLBACK(fit_to_window), &app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(fit_toolitem), -1);

    // Create scrolled window for image
    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), 
                                  GTK_POLICY_AUTOMATIC, 
                                  GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(box), scrolled_window, TRUE, TRUE, 0);

    // Create image widget
    app.image = gtk_image_new();
    gtk_container_add(GTK_CONTAINER(scrolled_window), app.image);

    // Create status bar
    app.status_bar = gtk_statusbar_new();
    gtk_box_pack_start(GTK_BOX(box), app.status_bar, FALSE, FALSE, 0);

    // Initialize app state
    app.zoom_level = 1.0;
    app.fit_to_window = TRUE;
    app.original_pixbuf = NULL;

    // Connect keyboard shortcuts
    g_signal_connect(app.window, "key-press-event", G_CALLBACK(key_press_event), &app);

    gtk_widget_show_all(app.window);
    gtk_main();

    // Clean up
    if (app.image_list) {
        g_list_free_full(app.image_list, g_free);
    }
    if (app.original_pixbuf) {
        g_object_unref(app.original_pixbuf);
    }
    return 0;
}
