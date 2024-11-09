#include <gtk/gtk.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

typedef struct {
    char process_name[256];
    int pid;
    double memory_gb;
} ProcessInfo;

typedef struct {
    GtkTextView *box_strings;
    GtkWindow *result_window;
    GtkTextView *result_text_view;
} SearchWidgets;

int compare_processes(const void *a, const void *b) {
    double memory_a = ((ProcessInfo *)a)->memory_gb;
    double memory_b = ((ProcessInfo *)b)->memory_gb;
    return (memory_b > memory_a) - (memory_b < memory_a);
}

double get_process_memory(int pid) {
    char path[512];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);

    FILE *file = fopen(path, "r");
    if (!file) return 0.0;

    double memory_kb = 0.0;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line, "VmRSS: %lf", &memory_kb);
            break;
        }
    }
    fclose(file);

    return memory_kb / (1024.0 * 1024.0);
}

void populate_processes(GtkComboBoxText *combo_box, const gchar *filter) {
    DIR *dir = opendir("/proc");
    if (!dir) {
        perror("Error...#104");
        return;
    }
    struct dirent *entry;

    gtk_combo_box_text_remove_all(combo_box);

    ProcessInfo processes[1024];
    int count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR && strtol(entry->d_name, NULL, 10) > 0) {
            char path[512];
            snprintf(path, sizeof(path), "/proc/%s/comm", entry->d_name);

            FILE *file = fopen(path, "r");
            if (!file) continue;

            char process_name[256];
            if (fgets(process_name, sizeof(process_name), file) != NULL) {
                process_name[strcspn(process_name, "\n")] = 0;

                if (filter && !strstr(process_name, filter) && !strstr(entry->d_name, filter)) {
                    fclose(file);
                    continue;
                }

                int pid = strtol(entry->d_name, NULL, 10);
                double memory_gb = get_process_memory(pid);

                processes[count].pid = pid;
                processes[count].memory_gb = memory_gb;
                strncpy(processes[count].process_name, process_name, sizeof(processes[count].process_name));
                count++;
            }
            fclose(file);
        }
    }
    closedir(dir);

    qsort(processes, count, sizeof(ProcessInfo), compare_processes);

    for (int i = 0; i < count; i++) {
        char item[1024];
        snprintf(item, sizeof(item), "%s --- %d (%.2f GB)", 
                 processes[i].process_name, processes[i].pid, processes[i].memory_gb);
        gtk_combo_box_text_append_text(combo_box, item);
    }
}

char* search_in_memory(int pid, const char *keyword) {
    char maps_path[64], mem_path[64];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
    snprintf(mem_path, sizeof(mem_path), "/proc/%d/mem", pid);

    FILE *maps_file = fopen(maps_path, "r");
    if (!maps_file) {
        return NULL;
    }
    int mem_fd = open(mem_path, O_RDONLY);
    if (mem_fd == -1) {
        fclose(maps_file);
        return NULL;
    }

    unsigned long start, end;
    char perms[5], line[256];
    const size_t block_size = 4096;
    char *buffer = malloc(block_size);
    if (!buffer) {
        g_print("Memory error.\n");
        fclose(maps_file);
        close(mem_fd);
        return NULL;
    }

    char *found_string = NULL;
    while (fgets(line, sizeof(line), maps_file)) {
        if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) == 3) {
            if (perms[0] != 'r') continue;

            size_t region_size = end - start;
            for (size_t offset = 0; offset < region_size; offset += block_size) {
                ssize_t bytes_to_read = (region_size - offset < block_size) ? (region_size - offset) : block_size;
                ssize_t bytes_read = pread(mem_fd, buffer, bytes_to_read, start + offset);

                if (bytes_read == -1) {
                    break;
                }

                void *match_ptr = memmem(buffer, bytes_read, keyword, strlen(keyword));
                if (match_ptr) {
                    size_t match_offset = (char*)match_ptr - buffer;

                    size_t line_start = match_offset;
                    while (line_start > 0 && buffer[line_start - 1] != '\n') {
                        line_start--;
                    }

                    size_t line_end = match_offset;
                    while (line_end < bytes_read && buffer[line_end] != '\n') {
                        line_end++;
                    }

                    size_t line_length = line_end - line_start;
                    found_string = strndup(buffer + line_start, line_length);
                    break;
                }
            }

            if (found_string) break;
        }
    }

    free(buffer);
    fclose(maps_file);
    close(mem_fd);
    return found_string;
}

void on_find_button_clicked(GtkButton *button, gpointer user_data) {
    GtkComboBoxText *box_process = GTK_COMBO_BOX_TEXT(user_data);
    SearchWidgets *widgets = g_object_get_data(G_OBJECT(button), "search_widgets");
    GtkTextView *box_strings = widgets->box_strings;
    GtkWidget *result_window = GTK_WIDGET(widgets->result_window);
    GtkTextView *result_text_view = widgets->result_text_view;

    gchar *selected_process = gtk_combo_box_text_get_active_text(box_process);
    if (!selected_process) {
        g_print("Choose process.\n");
        return;
    }

    GtkTextBuffer *text_buffer = gtk_text_view_get_buffer(box_strings);
    if (!GTK_IS_TEXT_BUFFER(text_buffer)) {
        g_print("Error: Invalid text buffer\n");
        g_free(selected_process);
        return;
    }

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(text_buffer, &start, &end);
    gchar *text_in_box = gtk_text_buffer_get_text(text_buffer, &start, &end, FALSE);

    GtkTextBuffer *result_buffer = gtk_text_view_get_buffer(result_text_view);
    if (!GTK_IS_TEXT_BUFFER(result_buffer)) {
        g_print("Error: Invalid result buffer\n");
        g_free(selected_process);
        g_free(text_in_box);
        return;
    }

    gtk_text_buffer_set_text(result_buffer, "", -1);

    int pid;
    sscanf(selected_process, "%*s --- %d", &pid);

    if (text_in_box && *text_in_box != '\0') {
        gchar **lines = g_strsplit(text_in_box, "\n", -1);
        gboolean found_any = FALSE;

        for (int i = 0; lines[i] != NULL; i++) {
            gchar *line = lines[i];
            gchar **parts = g_strsplit(line, "---", 2);

            if (parts[0] && parts[1]) {
                const char *keyword = g_strstrip(parts[0]);
                const char *search_string = g_strstrip(parts[1]);

                char *full_string = search_in_memory(pid, search_string);
                if (full_string) {
                    found_any = TRUE;
                    gchar *result = g_strdup_printf("%s  |  %s  |  %s\n", 
                                                    keyword, search_string, full_string);
                    gtk_text_buffer_insert_at_cursor(result_buffer, result, -1);
                    g_free(result);
                    free(full_string);
                }
            }
            g_strfreev(parts);
        }

        if (!found_any) {
            gtk_widget_show_all(result_window);
            gtk_text_buffer_set_text(result_buffer, "Nothing found.", -1);
        } else {
            if (!gtk_widget_is_visible(result_window)) {
                gtk_widget_show_all(result_window);
            }
        }

        g_strfreev(lines);
    } else {
        g_print("Enter strings.\n");
    }

    g_free(selected_process);
    g_free(text_in_box);
}


void on_combo_box_changed(GtkComboBoxText *combo_box, gpointer user_data) {
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextBuffer *text_buffer = gtk_text_view_get_buffer(text_view);
    
    gtk_text_buffer_set_text(text_buffer, "", -1);
}

static void activate(GtkApplication *app, gpointer user_data) {
    GtkWidget *window;
    GtkWidget *fixed;
    GtkWidget *header;
    GtkWidget *icon;
    GtkCssProvider *css_provider;

    window = gtk_application_window_new(app);
    gtk_widget_set_name(window, "window");
    gtk_window_set_title(GTK_WINDOW(window), "Error Strings");
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 600);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

    GtkWidget *result_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(result_window), "Error Result");
    gtk_window_set_default_size(GTK_WINDOW(result_window), 400, 400);
    gtk_widget_set_name(GTK_WIDGET(result_window), "result_window");
    gtk_window_set_position(GTK_WINDOW(result_window), GTK_WIN_POS_CENTER);
    gtk_window_set_resizable(GTK_WINDOW(result_window), FALSE);
    GtkWidget *result_fixed = gtk_fixed_new();
    gtk_container_add(GTK_CONTAINER(result_window), result_fixed);
    GtkWidget *result_window_header = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(result_window), result_window_header);
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(result_window_header), TRUE);
    GtkWidget *result_title_label = gtk_label_new("Result");
    gtk_header_bar_set_custom_title(GTK_HEADER_BAR(result_window_header), result_title_label);
    GtkWidget *result_label = gtk_label_new("Result:");
    gtk_widget_set_name(GTK_WIDGET(result_label), "result_label");
    gtk_fixed_put(GTK_FIXED(result_fixed), result_label, 20, 20);
    GtkWidget *result_scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(result_scrolled_window, 360, 300);
    gtk_fixed_put(GTK_FIXED(result_fixed), result_scrolled_window, 20, 50);
    gtk_widget_set_name(GTK_WIDGET(result_scrolled_window), "result_scrolled_window");
    GtkWidget *result_text_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(result_text_view), GTK_WRAP_WORD);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(result_text_view), FALSE);
    gtk_container_add(GTK_CONTAINER(result_scrolled_window), result_text_view);
    gtk_widget_set_name(GTK_WIDGET(result_text_view), "result_text_view");

    fixed = gtk_fixed_new();
    gtk_container_add(GTK_CONTAINER(window), fixed);

    header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(window), header);

    icon = gtk_image_new_from_resource("/df/error/imgs/logo.svg");
    gtk_widget_set_size_request(GTK_WIDGET(icon), 50, 50);
    gtk_fixed_put(GTK_FIXED(fixed), icon, 150, 7);
    gtk_widget_set_name(GTK_WIDGET(icon), "icon");

    GtkWidget *text = gtk_label_new("Error Strings");
    gtk_widget_set_size_request(GTK_WIDGET(text), 50, 50);
    gtk_fixed_put(GTK_FIXED(fixed), text, 215, 17);
    gtk_widget_set_name(GTK_WIDGET(text), "text");

    GtkWidget *box_process_label = gtk_label_new("Process: ");
    gtk_widget_set_size_request(GTK_WIDGET(box_process_label), 100, 30);
    gtk_fixed_put(GTK_FIXED(fixed), box_process_label, 30, 100);
    gtk_widget_set_name(GTK_WIDGET(box_process_label), "box_process_label");

    GtkWidget *box_process = gtk_combo_box_text_new();
    populate_processes(GTK_COMBO_BOX_TEXT(box_process), NULL);
    gtk_widget_set_size_request(GTK_WIDGET(box_process), 400, 20);
    gtk_fixed_put(GTK_FIXED(fixed), box_process, 50, 125);
    gtk_widget_set_name(GTK_WIDGET(box_process), "box_process");
    
    GtkWidget *box_strings_label = gtk_label_new("Strings: ");
    gtk_widget_set_size_request(GTK_WIDGET(box_strings_label), 100, 30);
    gtk_fixed_put(GTK_FIXED(fixed), box_strings_label, 30, 180);
    gtk_widget_set_name(GTK_WIDGET(box_strings_label), "box_strings_label");

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scrolled_window, 400, 250);
    gtk_fixed_put(GTK_FIXED(fixed), scrolled_window, 50, 205);
    gtk_widget_set_name(GTK_WIDGET(scrolled_window), "scrolled_window");
    GtkWidget *box_strings = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(box_strings), GTK_WRAP_WORD);
    gtk_container_add(GTK_CONTAINER(scrolled_window), box_strings);
    gtk_widget_set_name(GTK_WIDGET(box_strings), "box_strings");
    g_signal_connect(box_process, "changed", G_CALLBACK(on_combo_box_changed), box_strings);

    SearchWidgets *widgets = g_new(SearchWidgets, 1);
    widgets->box_strings = GTK_TEXT_VIEW(box_strings);
    widgets->result_window = GTK_WINDOW(result_window);
    widgets->result_text_view = GTK_TEXT_VIEW(result_text_view);
    GtkWidget *find_b = gtk_button_new_with_label("Find");
    gtk_widget_set_size_request(GTK_WIDGET(find_b), 70, 30);
    gtk_widget_set_name(GTK_WIDGET(find_b), "find_b");
    gtk_fixed_put(GTK_FIXED(fixed), find_b, 215, 470);
    g_object_set_data_full(G_OBJECT(find_b), "search_widgets", widgets, g_free);
    g_signal_connect(find_b, "clicked", G_CALLBACK(on_find_button_clicked), box_process);
    g_object_set_data(G_OBJECT(find_b), "box_strings", box_strings);
    g_object_set_data(G_OBJECT(find_b), "result_window", result_window);
    g_object_set_data(G_OBJECT(find_b), "result_text_view", result_text_view);
    g_signal_connect(result_window, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);

    css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(css_provider, "/df/error/styles/style.css");
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    gtk_widget_show_all(window);
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("com.example.ErrorStrings", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
