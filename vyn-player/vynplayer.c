#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <gst/gst.h>
#include <gst/video/videooverlay.h>
#include <gdk/gdkx.h>  // For X11 window handle

// Define the application structure
typedef struct {
    GtkWidget *video_container; // Container to embed the sink's widget
    GtkWidget *play_button;
    GtkWidget *pause_button;
    GtkWidget *stop_button;
    GtkWidget *volume_button;
    GtkWidget *progress_bar;
    GtkWidget *status_bar;
    GtkWidget *window;
    GList *video_list;
    GList *current_video;
    gdouble volume_level;
    gboolean is_playing;
    gboolean is_fullscreen;
    
    // Our custom pipeline
    GstElement *pipeline;
    GstBus *bus;
    guint bus_watch_id;
    guint timeout_id;
} VynPlayerApp;

// Function prototypes
static void update_video(VynPlayerApp *app, const gchar *path);
static void update_status(VynPlayerApp *app);
static void open_video(GtkWidget *widget, gpointer data);
static void play_video(GtkWidget *widget, gpointer data);
static void pause_video(GtkWidget *widget, gpointer data);
static void stop_video(GtkWidget *widget, gpointer data);
static void toggle_fullscreen(GtkWidget *widget, gpointer data);
static void volume_changed(GtkWidget *widget, gdouble value, gpointer data);
static void navigate_video(GtkWidget *widget, gpointer data);
static gboolean key_press_event(GtkWidget *widget, GdkEventKey *event, gpointer data);
static gboolean update_progress(gpointer data);
static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data);
static gboolean embed_video_idle(gpointer data);
static void cleanup_gstreamer(VynPlayerApp *app);
static void seek_video(VynPlayerApp *app, gdouble position);
static void play_next_video(VynPlayerApp *app);
static void play_prev_video(VynPlayerApp *app);
static gboolean progress_bar_click(GtkWidget *widget, GdkEventButton *event, gpointer data);

// Update the current video by building a new pipeline from the given file path.
static void update_video(VynPlayerApp *app, const gchar *path) {
    if (!path)
        return;
    
    g_print("Trying to play: %s\n", path);
    
    // Stop and free any existing pipeline.
    if (app->pipeline) {
        gst_element_set_state(app->pipeline, GST_STATE_NULL);
        gst_object_unref(app->pipeline);
        app->pipeline = NULL;
    }
    
    // Build the custom pipeline string using gtksink.
    // (If gtksink still opens a separate window, try using "gtkglsink" here.)
    gchar *pipeline_str = g_strdup_printf(
        "filesrc location=\"%s\" ! qtdemux name=demux "
        "demux.video_0 ! queue ! h264parse ! avdec_h264 ! videoconvert ! gtksink name=videosink "
        "demux.audio_0 ! queue ! aacparse ! avdec_aac ! audioconvert ! audioresample ! autoaudiosink",
        path);
    
    g_print("Pipeline: %s\n", pipeline_str);
    app->pipeline = gst_parse_launch(pipeline_str, NULL);
    g_free(pipeline_str);
    
    if (!app->pipeline) {
        g_printerr("Failed to create pipeline!\n");
        return;
    }
    
    // Get the bus and add a watch.
    app->bus = gst_element_get_bus(app->pipeline);
    app->bus_watch_id = gst_bus_add_watch(app->bus, bus_call, app);
    
    // Start playing.
    GstStateChangeReturn ret = gst_element_set_state(app->pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Failed to start playback!\n");
    } else {
        app->is_playing = TRUE;
        gtk_widget_set_sensitive(app->play_button, FALSE);
        gtk_widget_set_sensitive(app->pause_button, TRUE);
        gtk_widget_set_sensitive(app->stop_button, TRUE);
        update_status(app);
        // Delay embedding the sink widget until the pipeline is running.
        g_idle_add(embed_video_idle, app);
    }
}

static gboolean embed_video_idle(gpointer data) {
    VynPlayerApp *app = (VynPlayerApp *)data;
    GstElement *videosink = gst_bin_get_by_name(GST_BIN(app->pipeline), "videosink");
    if (videosink) {
        GtkWidget *video_widget = NULL;
        g_object_get(videosink, "widget", &video_widget, NULL);
        if (video_widget) {
            // Remove from any previous parent.
            GtkWidget *current_parent = gtk_widget_get_parent(video_widget);
            if (current_parent) {
                gtk_container_remove(GTK_CONTAINER(current_parent), video_widget);
                g_print("Removed gtksink widget from its previous parent\n");
            }
            // Hide the widget before reparenting.
            gtk_widget_hide(video_widget);
            // Clear any children from our container.
            GList *children = gtk_container_get_children(GTK_CONTAINER(app->video_container));
            for (GList *l = children; l; l = l->next)
                gtk_container_remove(GTK_CONTAINER(app->video_container), GTK_WIDGET(l->data));
            g_list_free(children);
            // Add the widget into our container.
            gtk_container_add(GTK_CONTAINER(app->video_container), video_widget);
            gtk_widget_show_all(app->video_container);
            g_print("Embedded gtksink widget into video container successfully\n");
            // Get its toplevel window and hide it.
            GtkWidget *toplevel = gtk_widget_get_toplevel(video_widget);
            if (GTK_IS_WINDOW(toplevel) && toplevel != app->window) {
                gtk_widget_hide(toplevel);
                g_print("Hid extra toplevel window from gtksink\n");
            }
        } else {
            g_warning("Failed to retrieve gtksink widget\n");
        }
        gst_object_unref(videosink);
    } else {
        g_warning("Failed to get videosink element from pipeline\n");
    }
    return G_SOURCE_REMOVE; // Run once.
}



// Update the status bar with the current video's basename.
static void update_status(VynPlayerApp *app) {
    if (app->current_video && app->current_video->data) {
        gchar *basename = g_path_get_basename((gchar *)app->current_video->data);
        gchar *status = g_strdup_printf("Now playing: %s", basename);
        gtk_statusbar_remove_all(GTK_STATUSBAR(app->status_bar), 0);
        gtk_statusbar_push(GTK_STATUSBAR(app->status_bar), 0, status);
        g_free(basename);
        g_free(status);
    }
}

// Open a video file and build the video list from the file's directory.
static void open_video(GtkWidget *widget, gpointer data) {
    VynPlayerApp *app = (VynPlayerApp *)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Open Video",
                                                    GTK_WINDOW(app->window),
                                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Open", GTK_RESPONSE_ACCEPT,
                                                    NULL);
    
    // Add video file filters.
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Video Files");
    gtk_file_filter_add_mime_type(filter, "video/*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    
    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "All Files");
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    
    // Clean up any existing video list.
    if (app->video_list) {
        g_list_free_full(app->video_list, g_free);
        app->video_list = NULL;
        app->current_video = NULL;
    }
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (filename) {
            gchar *dir_path = g_path_get_dirname(filename);
            GDir *dir = g_dir_open(dir_path, 0, NULL);
            if (dir) {
                const gchar *entry;
                while ((entry = g_dir_read_name(dir))) {
                    gchar *full_path = g_build_filename(dir_path, entry, NULL);
                    if (g_str_has_suffix(full_path, ".mp4") || 
                        g_str_has_suffix(full_path, ".mkv") ||
                        g_str_has_suffix(full_path, ".avi") ||
                        g_str_has_suffix(full_path, ".mov") ||
                        g_str_has_suffix(full_path, ".webm") ||
                        g_str_has_suffix(full_path, ".ogv")) {
                        app->video_list = g_list_append(app->video_list, full_path);
                    } else {
                        g_free(full_path);
                    }
                }
                g_dir_close(dir);
                
                app->video_list = g_list_sort(app->video_list, (GCompareFunc)g_strcmp0);
                app->current_video = g_list_find_custom(app->video_list, filename, (GCompareFunc)g_strcmp0);
                if (!app->current_video && app->video_list)
                    app->current_video = app->video_list;
                
                if (app->current_video)
                    update_video(app, (gchar *)app->current_video->data);
            }
            g_free(dir_path);
            g_free(filename);
        }
    }
    gtk_widget_destroy(dialog);
}

// Play the video.
static void play_video(GtkWidget *widget, gpointer data) {
    VynPlayerApp *app = (VynPlayerApp *)data;
    if (app->pipeline && !app->is_playing) {
        gst_element_set_state(app->pipeline, GST_STATE_PLAYING);
        app->is_playing = TRUE;
        gtk_widget_set_sensitive(app->play_button, FALSE);
        gtk_widget_set_sensitive(app->pause_button, TRUE);
        gtk_widget_set_sensitive(app->stop_button, TRUE);
    }
}

// Pause the video.
static void pause_video(GtkWidget *widget, gpointer data) {
    VynPlayerApp *app = (VynPlayerApp *)data;
    if (app->pipeline && app->is_playing) {
        gst_element_set_state(app->pipeline, GST_STATE_PAUSED);
        app->is_playing = FALSE;
        gtk_widget_set_sensitive(app->play_button, TRUE);
        gtk_widget_set_sensitive(app->pause_button, FALSE);
        gtk_widget_set_sensitive(app->stop_button, TRUE);
    }
}

// Stop the video playback.
static void stop_video(GtkWidget *widget, gpointer data) {
    VynPlayerApp *app = (VynPlayerApp *)data;
    if (app->pipeline) {
        gst_element_set_state(app->pipeline, GST_STATE_NULL);
        app->is_playing = FALSE;
        gtk_widget_set_sensitive(app->play_button, TRUE);
        gtk_widget_set_sensitive(app->pause_button, FALSE);
        gtk_widget_set_sensitive(app->stop_button, FALSE);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_bar), 0.0);
    }
}

// Toggle fullscreen mode.
static void toggle_fullscreen(GtkWidget *widget, gpointer data) {
    VynPlayerApp *app = (VynPlayerApp *)data;
    if (app->is_fullscreen) {
        gtk_window_unfullscreen(GTK_WINDOW(app->window));
        app->is_fullscreen = FALSE;
    } else {
        gtk_window_fullscreen(GTK_WINDOW(app->window));
        app->is_fullscreen = TRUE;
    }
}

// Handle volume changes.
static void volume_changed(GtkWidget *widget, gdouble value, gpointer data) {
    VynPlayerApp *app = (VynPlayerApp *)data;
    app->volume_level = value;
    // Volume control for a custom pipeline would require setting properties on the audio sink.
}

// Navigate to next or previous video.
static void navigate_video(GtkWidget *widget, gpointer data) {
    VynPlayerApp *app = (VynPlayerApp *)data;
    gint direction = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "direction"));
    if (direction > 0)
        play_next_video(app);
    else
        play_prev_video(app);
}

// Play the next video in the list.
static void play_next_video(VynPlayerApp *app) {
    if (!app->video_list || !app->current_video)
        return;
    
    GList *next = g_list_next(app->current_video);
    app->current_video = next ? next : app->video_list;
    
    if (app->current_video && app->current_video->data)
        update_video(app, (gchar *)app->current_video->data);
}

// Play the previous video in the list.
static void play_prev_video(VynPlayerApp *app) {
    if (!app->video_list || !app->current_video)
        return;
    
    GList *prev = g_list_previous(app->current_video);
    app->current_video = prev ? prev : g_list_last(app->video_list);
    
    if (app->current_video && app->current_video->data)
        update_video(app, (gchar *)app->current_video->data);
}

// Seek to a specific position in the video.
static void seek_video(VynPlayerApp *app, gdouble position) {
    if (!app->pipeline)
        return;
    
    gint64 duration;
    if (gst_element_query_duration(app->pipeline, GST_FORMAT_TIME, &duration))
        gst_element_seek_simple(app->pipeline, GST_FORMAT_TIME,
                                GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
                                (gint64)(position * duration));
}

// Progress bar click handler for seeking.
static gboolean progress_bar_click(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    VynPlayerApp *app = (VynPlayerApp *)data;
    if (event->button == 1) { // Left click
        GtkAllocation allocation;
        gtk_widget_get_allocation(widget, &allocation);
        gdouble pos = event->x / allocation.width;
        pos = CLAMP(pos, 0.0, 1.0);
        seek_video(app, pos);
        return TRUE;
    }
    return FALSE;
}

// Handle key press events for playback controls.
static gboolean key_press_event(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    VynPlayerApp *app = (VynPlayerApp *)data;
    switch (event->keyval) {
        case GDK_KEY_space:
            if (app->is_playing)
                pause_video(NULL, app);
            else
                play_video(NULL, app);
            return TRUE;
        case GDK_KEY_Left:
        case GDK_KEY_KP_Left: {
            gint64 position;
            if (gst_element_query_position(app->pipeline, GST_FORMAT_TIME, &position)) {
                position = MAX(0, position - 10 * GST_SECOND);
                gst_element_seek_simple(app->pipeline, GST_FORMAT_TIME,
                                        GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
                                        position);
            }
            return TRUE;
        }
        case GDK_KEY_Right:
        case GDK_KEY_KP_Right: {
            gint64 position, duration;
            if (gst_element_query_position(app->pipeline, GST_FORMAT_TIME, &position) &&
                gst_element_query_duration(app->pipeline, GST_FORMAT_TIME, &duration)) {
                position = MIN(duration, position + 10 * GST_SECOND);
                gst_element_seek_simple(app->pipeline, GST_FORMAT_TIME,
                                        GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
                                        position);
            }
            return TRUE;
        }
        case GDK_KEY_Up:
        case GDK_KEY_KP_Up:
            app->volume_level = MIN(1.0, app->volume_level + 0.05);
            gtk_scale_button_set_value(GTK_SCALE_BUTTON(app->volume_button), app->volume_level);
            return TRUE;
        case GDK_KEY_Down:
        case GDK_KEY_KP_Down:
            app->volume_level = MAX(0.0, app->volume_level - 0.05);
            gtk_scale_button_set_value(GTK_SCALE_BUTTON(app->volume_button), app->volume_level);
            return TRUE;
        case GDK_KEY_f:
        case GDK_KEY_F:
            toggle_fullscreen(NULL, app);
            return TRUE;
        case GDK_KEY_q:
            if (event->state & GDK_CONTROL_MASK)
                gtk_main_quit();
            return TRUE;
        case GDK_KEY_n:
        case GDK_KEY_N:
            play_next_video(app);
            return TRUE;
        case GDK_KEY_p:
        case GDK_KEY_P:
            play_prev_video(app);
            return TRUE;
        case GDK_KEY_Escape:
            if (app->is_fullscreen)
                toggle_fullscreen(NULL, app);
            return TRUE;
        default:
            return FALSE;
    }
}

// Update progress bar and time display.
static gboolean update_progress(gpointer data) {
    VynPlayerApp *app = (VynPlayerApp *)data;
    if (app->pipeline && app->is_playing) {
        gint64 position, duration;
        if (gst_element_query_position(app->pipeline, GST_FORMAT_TIME, &position) &&
            gst_element_query_duration(app->pipeline, GST_FORMAT_TIME, &duration) &&
            duration > 0) {
            gdouble progress = (gdouble)position / (gdouble)duration;
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_bar), progress);
            
            gint pos_hours = position / (3600 * GST_SECOND);
            gint pos_minutes = (position / (60 * GST_SECOND)) % 60;
            gint pos_seconds = (position / GST_SECOND) % 60;
            gint dur_hours = duration / (3600 * GST_SECOND);
            gint dur_minutes = (duration / (60 * GST_SECOND)) % 60;
            gint dur_seconds = (duration / GST_SECOND) % 60;
            
            gchar *time_str = g_strdup_printf("%02d:%02d:%02d / %02d:%02d:%02d",
                                             pos_hours, pos_minutes, pos_seconds,
                                             dur_hours, dur_minutes, dur_seconds);
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app->progress_bar), time_str);
            g_free(time_str);
            
            if (position >= duration - GST_SECOND/2)
                play_next_video(app);
        }
    }
    return TRUE;
}

// GStreamer bus callback to handle EOS and errors.
static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
    VynPlayerApp *app = (VynPlayerApp *)data;
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            play_next_video(app);
            break;
        case GST_MESSAGE_ERROR: {
            gchar *debug;
            GError *error;
            gst_message_parse_error(msg, &error, &debug);
            g_free(debug);
            g_printerr("Error: %s\n", error->message);
            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                                       GTK_MESSAGE_ERROR,
                                                       GTK_BUTTONS_CLOSE,
                                                       "Failed to play video: %s",
                                                       error->message);
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            g_error_free(error);
            break;
        }
        default:
            break;
    }
    return TRUE;
}

// Clean up GStreamer resources.
static void cleanup_gstreamer(VynPlayerApp *app) {
    if (app->timeout_id > 0) {
        g_source_remove(app->timeout_id);
        app->timeout_id = 0;
    }
    if (app->bus_watch_id > 0) {
        g_source_remove(app->bus_watch_id);
        app->bus_watch_id = 0;
    }
    if (app->pipeline) {
        gst_element_set_state(app->pipeline, GST_STATE_NULL);
        gst_object_unref(app->pipeline);
        app->pipeline = NULL;
    }
    if (app->video_list) {
        g_list_free_full(app->video_list, g_free);
        app->video_list = NULL;
        app->current_video = NULL;
    }
}

// Main function.
int main(int argc, char *argv[]) {
    GtkWidget *toolbar;
    GtkWidget *vbox;
    VynPlayerApp app = {0};
    GtkToolItem *open_toolitem, *prev_toolitem, *next_toolitem, *play_toolitem,
                *pause_toolitem, *stop_toolitem, *fullscreen_toolitem, *separator;
    
    gtk_init(&argc, &argv);
    gst_init(&argc, &argv);
    
    g_print("Vyn Player starting...\n");
    
    // Create main window.
    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "Vyn Player");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 800, 600);
    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(app.window, "key-press-event", G_CALLBACK(key_press_event), &app);
    
    // Create main container.
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app.window), vbox);
    
    // Create toolbar.
    toolbar = gtk_toolbar_new();
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
    
    // Toolbar buttons.
    open_toolitem = gtk_tool_button_new(gtk_image_new_from_icon_name("document-open", GTK_ICON_SIZE_LARGE_TOOLBAR), "Open");
    g_signal_connect(open_toolitem, "clicked", G_CALLBACK(open_video), &app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(open_toolitem), -1);
    
    separator = gtk_separator_tool_item_new();
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(separator), -1);
    
    prev_toolitem = gtk_tool_button_new(gtk_image_new_from_icon_name("media-skip-backward", GTK_ICON_SIZE_LARGE_TOOLBAR), "Previous");
    g_object_set_data(G_OBJECT(prev_toolitem), "direction", GINT_TO_POINTER(-1));
    g_signal_connect(prev_toolitem, "clicked", G_CALLBACK(navigate_video), &app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(prev_toolitem), -1);
    
    play_toolitem = gtk_tool_button_new(gtk_image_new_from_icon_name("media-playback-start", GTK_ICON_SIZE_LARGE_TOOLBAR), "Play");
    g_signal_connect(play_toolitem, "clicked", G_CALLBACK(play_video), &app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(play_toolitem), -1);
    app.play_button = GTK_WIDGET(play_toolitem);
    
    pause_toolitem = gtk_tool_button_new(gtk_image_new_from_icon_name("media-playback-pause", GTK_ICON_SIZE_LARGE_TOOLBAR), "Pause");
    g_signal_connect(pause_toolitem, "clicked", G_CALLBACK(pause_video), &app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(pause_toolitem), -1);
    app.pause_button = GTK_WIDGET(pause_toolitem);
    
    stop_toolitem = gtk_tool_button_new(gtk_image_new_from_icon_name("media-playback-stop", GTK_ICON_SIZE_LARGE_TOOLBAR), "Stop");
    g_signal_connect(stop_toolitem, "clicked", G_CALLBACK(stop_video), &app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(stop_toolitem), -1);
    app.stop_button = GTK_WIDGET(stop_toolitem);
    
    next_toolitem = gtk_tool_button_new(gtk_image_new_from_icon_name("media-skip-forward", GTK_ICON_SIZE_LARGE_TOOLBAR), "Next");
    g_object_set_data(G_OBJECT(next_toolitem), "direction", GINT_TO_POINTER(1));
    g_signal_connect(next_toolitem, "clicked", G_CALLBACK(navigate_video), &app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(next_toolitem), -1);
    
    separator = gtk_separator_tool_item_new();
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(separator), -1);
    
    fullscreen_toolitem = gtk_tool_button_new(gtk_image_new_from_icon_name("view-fullscreen", GTK_ICON_SIZE_LARGE_TOOLBAR), "Fullscreen");
    g_signal_connect(fullscreen_toolitem, "clicked", G_CALLBACK(toggle_fullscreen), &app);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(fullscreen_toolitem), -1);
    
    // Create volume button.
    GtkToolItem *volume_toolitem = gtk_tool_item_new();
    app.volume_button = gtk_volume_button_new();
    app.volume_level = 0.5;
    gtk_scale_button_set_value(GTK_SCALE_BUTTON(app.volume_button), app.volume_level);
    g_signal_connect(app.volume_button, "value-changed", G_CALLBACK(volume_changed), &app);
    gtk_container_add(GTK_CONTAINER(volume_toolitem), app.volume_button);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), volume_toolitem, -1);
    
    // Create video container.
    app.video_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(app.video_container, TRUE);
    gtk_widget_set_vexpand(app.video_container, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), app.video_container, TRUE, TRUE, 0);
    
    // Create progress bar.
    app.progress_bar = gtk_progress_bar_new();
    g_signal_connect(app.progress_bar, "button-press-event", G_CALLBACK(progress_bar_click), &app);
    gtk_box_pack_start(GTK_BOX(vbox), app.progress_bar, FALSE, FALSE, 0);
    
    // Create status bar.
    app.status_bar = gtk_statusbar_new();
    gtk_box_pack_start(GTK_BOX(vbox), app.status_bar, FALSE, FALSE, 0);
    
    // Set initial states.
    gtk_widget_set_sensitive(app.pause_button, FALSE);
    gtk_widget_set_sensitive(app.stop_button, FALSE);
    
    // Set up periodic progress updates.
    app.timeout_id = g_timeout_add(250, update_progress, &app);
    
    gtk_widget_show_all(app.window);
    gtk_main();
    
    cleanup_gstreamer(&app);
    
    return 0;
}

