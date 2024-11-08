#include <gtk/gtk.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    char process_name[256];
    int pid;
    double memory_gb;
} ProcessInfo;

int compare_processes(const void *a, const void *b) {
    double memory_a = ((ProcessInfo *)a)->memory_gb;
    double memory_b = ((ProcessInfo *)b)->memory_gb;
    if (memory_b > memory_a) return 1;
    if (memory_b < memory_a) return -1;
    return 0;
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
        perror("Cannot open /proc");
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

void on_combo_box_changed(GtkComboBoxText *combo_box, gpointer user_data) {
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextBuffer *text_buffer = gtk_text_view_get_buffer(text_view);
    
    gtk_text_buffer_set_text(text_buffer, "", -1);
}

void on_find_button_clicked(GtkButton *button, gpointer user_data) {
    GtkComboBoxText *box_process = GTK_COMBO_BOX_TEXT(user_data);
    GtkTextView *box_strings = GTK_TEXT_VIEW(g_object_get_data(G_OBJECT(button), "box_strings"));
    GtkWidget *result_window = GTK_WIDGET(g_object_get_data(G_OBJECT(button), "result_window"));

    gchar *selected_process = gtk_combo_box_text_get_active_text(box_process);
    if (!selected_process) {
        g_print("Please select a process.\n");
        return;
    }

    GtkTextBuffer *text_buffer = gtk_text_view_get_buffer(box_strings);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(text_buffer, &start, &end);
    gchar *text_in_box = gtk_text_buffer_get_text(text_buffer, &start, &end, FALSE);

    if (text_in_box && *text_in_box != '\0') {
        gchar **lines = g_strsplit(text_in_box, "\n", -1);
        gboolean valid = TRUE;

        for (int i = 0; lines[i] != NULL; i++) {
            gchar *line = lines[i];
            gchar **parts = g_strsplit(line, "---", 2);

            if (parts[0] && parts[1]) {
                g_print("Name: %s\n", parts[0]);
                g_print("String: %s\n", parts[1]);
            } else {
                g_print("Invalid entry format\n");
                valid = FALSE;
                break;
            }
            g_strfreev(parts);
        }
        g_strfreev(lines);
        if (valid) {
            GtkWidget *result_window_header = gtk_header_bar_new();
            gtk_window_set_titlebar(GTK_WINDOW(result_window), result_window_header);
            gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(result_window_header), TRUE);
            
            GtkWidget *result_title_label = gtk_label_new(selected_process);
            gtk_header_bar_set_custom_title(GTK_HEADER_BAR(result_window_header), result_title_label);

            gtk_widget_show_all(result_window);
        }
        g_signal_connect(result_window, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);        
    } else {
        g_print("Please enter text in the Strings box.\n");
    }

    g_free(selected_process);
    g_free(text_in_box);
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

    GtkWidget *find_b = gtk_button_new_with_label("Find");
    gtk_widget_set_size_request(GTK_WIDGET(find_b), 70, 30);
    gtk_widget_set_name(GTK_WIDGET(find_b), "find_b");
    gtk_fixed_put(GTK_FIXED(fixed), find_b, 215, 470);
    g_signal_connect(find_b, "clicked", G_CALLBACK(on_find_button_clicked), box_process);
    g_object_set_data(G_OBJECT(find_b), "box_strings", box_strings);
    g_object_set_data(G_OBJECT(find_b), "result_window", result_window);

    css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css_provider,
        "#window { background-color: #292929; }"
        "#text { font-size: 20px; font-weight: 600; color: #B7B7B7; }"
        "#box_process_label, #box_strings_label { font-size: 15px; font-weight: 600; color: #B7B7B7; }"
        "#box_strings, #scrolled_window { border: #181818 solid 1px; border-radius: 7px; background-color: #373737 }"
        "#find_b { font-size: 15px; color: #b7b7b7; }"
        "#box_process { font-size: 13px; }"
        "#result_label { font-size: 15px; font-weight: 600; color: #B7B7B7; }"
        ,
        -1,
        NULL);

    GtkStyleContext *context = gtk_widget_get_style_context(window);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    context = gtk_widget_get_style_context(text);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    context = gtk_widget_get_style_context(box_process_label);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    context = gtk_widget_get_style_context(box_strings_label);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    context = gtk_widget_get_style_context(box_strings);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    context = gtk_widget_get_style_context(scrolled_window);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    context = gtk_widget_get_style_context(find_b);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    context = gtk_widget_get_style_context(box_process);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    context = gtk_widget_get_style_context(result_label);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    gtk_widget_show_all(window);
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("com.example.ErrorStrings", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}



/*
Сделай так что бы код после успешного открытия result_window сканировал строки памяти в выбранном процессе в box_process на наличие строк из box_strings(искать он будет все значения String, но самое главное что Name---String и у каждого String есть своё Name):
1. В окне result_window будет textview с результатами где будет написано: 
 name  |  string
name - значение Name из box_strings
string - Значение String из box_strings

2. У каждого значения String есть свой Name по этому если в box_string написано:
DD client---^string!!!1
Nemezida Client---@s!tring2

И в процессе нашлось какае-то String из это, либо сразу все, то код в result_window так и записывает:
Found:
DD client  |  ^string!!!1  | 
Nemezida Client  |  @s!tring2

Если ничего из String не нашло в строках процесса, то появляется result_label с надписью "Not Found", но во время поиска в result_window будет Надпись "Loading." и эти точки будут добавляться и убираться по типу:
Loading
Loading.
Loading..
Loading...
Loading..
Loading.
Loading
И так пока не найдет что то либо не закончит поиск
ТОЛЬКО НАПИШИ КОД ПОЛНОСТЬЮ И НИЧЕГО НЕ УБИРАЙ И ВЕСЬ ФУНКЦИОНАЛ НАПИШИ САМ
*/

































/*
У меня есть код НО он не исполняет то что должен, а именно он должен делать следующие:
Пользователь выбирает процесс(в GtkWidget *box_process) который ему нужен для сканирования строк -> Потом в поле для заполения строк(box_strings) должен написать строки в виде: Name---String, name - это наименование для строки, string - это то, что должно искать в строках и после успешного заполнения открывается окно result_window и в нем в text_view вписывается результат того что нашлёл в строках выбранного процесса, если не находит то текст в  GtkWidget *result_label меняется на Not Found, если находит какую-то строку то пишет в text_view следующие:
name(название string)  | string(то что нашлось)
если name---string много/ много нашлось то пишет тоже
У КАЖДОГО STRING есть свой NAME

Код уже выполняет половину функций, проверяет правильность написания в box_strings, выбрали ли процесс в box_process НО после успешного заполнения код не находит выбранный процесс и не начинает поиск в нем строк и не ищет того что я написал НО, так же надо что бы все результаты которые получает код он вписывает в text_view а надо в консоль, это тоже надо

Вот мой код, В функции active и main НИЧЕГО НЕ МЕНЯТЬ И НЕ УДАЛЯТЬ:
#include <gtk/gtk.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    char process_name[256];
    int pid;
    double memory_gb;
} ProcessInfo;

// Function to compare processes based on memory usage
int compare_processes(const void *a, const void *b) {
    double memory_a = ((ProcessInfo *)a)->memory_gb;
    double memory_b = ((ProcessInfo *)b)->memory_gb;
    if (memory_b > memory_a) return 1;
    if (memory_b < memory_a) return -1;
    return 0;
}

// Function to get memory usage of a process
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

    return memory_kb / (1024.0 * 1024.0);  // Convert KB to GB
}

// Function to populate process list into combo box
void populate_processes(GtkComboBoxText *combo_box, const gchar *filter) {
    DIR *dir = opendir("/proc");
    if (!dir) {
        perror("Cannot open /proc");
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

                if (filter && !(strstr(process_name, filter) || strstr(entry->d_name, filter))) {
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

// Function to extract PID for a process based on its name
gchar *get_pid_for_process(const gchar *process_name) {
    DIR *dir = opendir("/proc");
    if (!dir) {
        g_print("Unable to open /proc directory.\n");
        return NULL;
    }

    struct dirent *entry;
    gchar *pid_string = NULL;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            // Check if directory name is a number (it corresponds to a PID)
            int pid = strtol(entry->d_name, NULL, 10);
            if (pid > 0) {
                char comm_path[512];
                snprintf(comm_path, sizeof(comm_path), "/proc/%d/comm", pid);
                FILE *file = fopen(comm_path, "r");

                if (file) {
                    char current_process_name[256];
                    if (fgets(current_process_name, sizeof(current_process_name), file)) {
                        current_process_name[strcspn(current_process_name, "\n")] = 0;  // Remove the newline
                        
                        // Debug output to see the process being checked
                        g_print("Checking process: PID %d, Name: %s\n", pid, current_process_name);
                        
                        // Check if process name matches the one we're looking for
                        if (g_strcmp0(current_process_name, process_name) == 0) {
                            g_print("Found matching process: %s (PID %d)\n", current_process_name, pid);
                            pid_string = g_strdup(entry->d_name);  // Found the matching PID
                            fclose(file);
                            break;  // Exit loop once we find the first match
                        }
                    }
                    fclose(file);
                }
            }
        }
    }

    closedir(dir);

    return pid_string;
}



// Callback function for "Find" button
int extract_pid_from_string(const gchar *process_string) {
    // Строка в формате "имя процесса --- PID (память)"
    gchar **parts = g_strsplit(process_string, "---", 2);
    if (parts[1]) {
        gchar *pid_str = g_strstrip(parts[1]);  // Получаем строку с PID
        int pid = atoi(pid_str);  // Преобразуем строку в целое число (PID)
        g_strfreev(parts);  // Освобождаем память
        return pid;
    }
    return -1;  // Если строка не в правильном формате
}

// Функция для поиска процесса по PID
ProcessInfo *find_process_by_pid(int pid) {
    DIR *dir = opendir("/proc");
    if (!dir) {
        perror("Cannot open /proc");
        return NULL;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            int current_pid = strtol(entry->d_name, NULL, 10);
            if (current_pid == pid) {
                char comm_path[512];
                snprintf(comm_path, sizeof(comm_path), "/proc/%d/comm", pid);
                FILE *file = fopen(comm_path, "r");

                if (file) {
                    char process_name[256];
                    if (fgets(process_name, sizeof(process_name), file)) {
                        process_name[strcspn(process_name, "\n")] = 0;  // Убираем символ новой строки
                        fclose(file);

                        // Возвращаем информацию о процессе
                        ProcessInfo *process_info = malloc(sizeof(ProcessInfo));
                        strncpy(process_info->process_name, process_name, sizeof(process_info->process_name));
                        process_info->pid = pid;
                        process_info->memory_gb = get_process_memory(pid);
                        closedir(dir);
                        return process_info;
                    }
                }
            }
        }
    }
    closedir(dir);
    return NULL;  // Если процесс не найден
}

// Callback-функция для кнопки "Find"
void on_find_button_clicked(GtkButton *button, gpointer user_data) {
    GtkComboBoxText *box_process = GTK_COMBO_BOX_TEXT(user_data);
    GtkTextView *box_strings = GTK_TEXT_VIEW(g_object_get_data(G_OBJECT(button), "box_strings"));
    GtkWidget *result_window = GTK_WIDGET(g_object_get_data(G_OBJECT(button), "result_window"));

    // Получаем выбранную строку процесса
    gchar *selected_process_string = gtk_combo_box_text_get_active_text(box_process);
    if (!selected_process_string) {
        g_print("Please select a process.\n");
        return;
    }

    // Извлекаем PID из строки
    int pid = extract_pid_from_string(selected_process_string);
    if (pid == -1) {
        g_print("Invalid process format.\n");
        g_free(selected_process_string);
        return;
    }

    // Находим процесс по PID
    ProcessInfo *process_info = find_process_by_pid(pid);
    if (!process_info) {
        g_print("Process with PID %d not found.\n", pid);
        g_free(selected_process_string);
        return;
    }

    // Очищаем предыдущий результат
    GtkTextBuffer *result_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_object_get_data(G_OBJECT(button), "result_text_view")));
    gtk_text_buffer_set_text(result_buffer, "", -1);

    // Извлекаем текст из текстового поля "Strings"
    GtkTextBuffer *text_buffer = gtk_text_view_get_buffer(box_strings);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(text_buffer, &start, &end);
    gchar *text_in_box = gtk_text_buffer_get_text(text_buffer, &start, &end, FALSE);

    if (text_in_box && *text_in_box != '\0') {
        gchar **lines = g_strsplit(text_in_box, "\n", -1);
        gboolean found = FALSE;
        gchar *result_text = g_strdup("");  // Начинаем с пустой строки

        // Добавляем заголовок с именем процесса и PID
        gchar *header = g_strdup_printf("Process: %s (PID: %d)\n\n", process_info->process_name, pid);
        result_text = g_strconcat(result_text, header, NULL);
        g_free(header);

        // Поиск совпадений по строкам
        for (int i = 0; lines[i] != NULL; i++) {
            gchar *line = lines[i];
            gchar **parts = g_strsplit(line, "---", 2);

            if (parts[0] && parts[1]) {
                gchar *name = g_strstrip(parts[0]);
                gchar *string = g_strstrip(parts[1]);

                // Проверяем, принадлежит ли строка выбранному процессу
                if (strstr(name, process_info->process_name) != NULL) {
                    g_print("Found match: %s --- %s\n", name, string);

                    // Добавляем в результат
                    gchar *new_result = g_strdup_printf("Name: %s\nString: %s\n", name, string);
                    result_text = g_strconcat(result_text, new_result, NULL);
                    found = TRUE;
                    g_free(new_result);
                }
            }
            g_strfreev(parts);
        }
        g_strfreev(lines);

        // Показать результаты
        if (found) {
            gtk_text_buffer_set_text(result_buffer, result_text, -1);
            gtk_widget_show_all(result_window);
        } else {
            g_print("No matches found for the selected process.\n");
        }

        g_free(result_text);  // Освобождаем память
    } else {
        g_print("Please enter text in the Strings box.\n");
    }

    // Освобождаем память
    g_free(selected_process_string);
    g_free(text_in_box);
    free(process_info);  // Освобождаем память, выделенную для ProcessInfo
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
    GtkWidget *result_window_header = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(result_window), result_window_header);
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(result_window_header), TRUE);
    GtkWidget *result_title_label = gtk_label_new("Result");
    gtk_header_bar_set_custom_title(GTK_HEADER_BAR(result_window_header), result_title_label);
    GtkWidget *fixed_result = gtk_fixed_new();
    gtk_container_add(GTK_CONTAINER(result_window), fixed_result);
    GtkWidget *result_label = gtk_label_new("Result:");
    gtk_fixed_put(GTK_FIXED(fixed_result), result_label, 175, 20);
    gtk_widget_set_name(GTK_WIDGET(result_label), "result_label");
    GtkWidget *scrolled_window_result = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scrolled_window_result, 350, 200);
    gtk_fixed_put(GTK_FIXED(fixed_result), scrolled_window_result, 30, 50);
    gtk_widget_set_name(GTK_WIDGET(scrolled_window_result), "scrolled_window_result");
    GtkWidget *result_text_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(result_text_view), GTK_WRAP_WORD);
    gtk_container_add(GTK_CONTAINER(scrolled_window_result), result_text_view);
    gtk_widget_set_name(GTK_WIDGET(result_text_view), "result_text_view");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(result_text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(result_text_view), FALSE);


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

    GtkWidget *find_b = gtk_button_new_with_label("Find");
    gtk_widget_set_size_request(GTK_WIDGET(find_b), 70, 30);
    gtk_widget_set_name(GTK_WIDGET(find_b), "find_b");
    gtk_fixed_put(GTK_FIXED(fixed), find_b, 215, 470);
    g_signal_connect(find_b, "clicked", G_CALLBACK(on_find_button_clicked), box_process);
    g_object_set_data(G_OBJECT(find_b), "box_strings", box_strings);
    g_object_set_data(G_OBJECT(find_b), "result_window", result_window);
    g_object_set_data(G_OBJECT(find_b), "result_label", result_label);
    g_object_set_data(G_OBJECT(find_b), "result_text_view", result_text_view);


    css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css_provider,
        "#window { background-color: #292929; }"
        "#text { font-size: 20px; font-weight: 600; color: #B7B7B7; }"
        "#box_process_label, #box_strings_label { font-size: 15px; font-weight: 600; color: #B7B7B7; }"
        "#box_strings, #scrolled_window { border: #181818 solid 1px; border-radius: 7px; background-color: #373737 }"
        "#find_b { font-size: 15px; color: #b7b7b7; }"
        "#box_process { font-size: 13px; }"
        "#result_label { font-size: 15px; font-weight: 600; color: #B7B7B7; }"
        "#scrolled_window_result { border: #181818 solid 1px; border-radius: 7px; background-color: #373737 }"
        "#result_text_view { font-size: 13px; color: #B7B7B7; }"
        ,
        -1,
        NULL);

    GtkStyleContext *context = gtk_widget_get_style_context(window);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    context = gtk_widget_get_style_context(text);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    context = gtk_widget_get_style_context(box_process_label);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    context = gtk_widget_get_style_context(box_strings_label);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    context = gtk_widget_get_style_context(box_strings);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    context = gtk_widget_get_style_context(scrolled_window);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    context = gtk_widget_get_style_context(find_b);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    context = gtk_widget_get_style_context(box_process);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    context = gtk_widget_get_style_context(result_label);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    context = gtk_widget_get_style_context(scrolled_window_result);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    context = gtk_widget_get_style_context(result_text_view);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

    gtk_widget_show_all(window);
}


int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("com.example.ErrorStrings", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
*/