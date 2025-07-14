#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define INPUT_PIPE "/tmp/frontend_input"
#define OUTPUT_PIPE "/tmp/frontend_output"
#define MAX_LINE_LEN 4096

GtkWidget *entry_id;
GtkWidget *entry_year;
GtkWidget *entry_month;

GtkWidget *text_view;       // Aquí se mostrará el texto de los resultados.
GtkTextBuffer *text_buffer; // Este es el "almacén" donde vive el texto que se muestra en text_view.

void send_request_to_backend(const char *id, const char *year, const char *month)
{
    int fd;
    char request[MAX_LINE_LEN];

    // Create the request string
    snprintf(request, sizeof(request), "%s|%s|%s", id, year, month);

    // Open the input pipe (frontend writes to this)
    mkfifo(INPUT_PIPE, 0666);
    fd = open(INPUT_PIPE, O_WRONLY);
    write(fd, request, strlen(request) + 1);
    close(fd);
}

void read_response_from_backend() {
    int fd;
    
    // 1. Abrimos la tubería para leer
    fd = open(OUTPUT_PIPE, O_RDONLY);
    if (fd == -1) {
        perror("Frontend: Error abriendo OUTPUT_PIPE para lectura");
        gtk_text_buffer_set_text(text_buffer, "Error: No se pudo conectar con el backend.", -1);
        return;
    }

    // 2. Preparamos un búfer de resultados dinámico, igual que en el backend
    size_t buffer_size = 4096; // Tamaño inicial
    size_t total_bytes_read = 0;
    char *response_buffer = malloc(buffer_size);
    if (response_buffer == NULL) {
        perror("Frontend: Fallo de malloc inicial");
        close(fd);
        return;
    }

    // 3. Leemos de la tubería en un bucle hasta que se vacíe
    while (1) {
        // Comprobamos si necesitamos más espacio ANTES de leer
        if (total_bytes_read + 2048 > buffer_size) { // Dejamos un margen de 2KB
            buffer_size *= 2;
            char *new_buffer = realloc(response_buffer, buffer_size);
            if (new_buffer == NULL) {
                perror("Frontend: Fallo de realloc");
                free(response_buffer);
                close(fd);
                return;
            }
            response_buffer = new_buffer;
        }

        // Intentamos leer un trozo (chunk) de la tubería
        // Leemos en la posición correcta del búfer
        ssize_t bytes_in_chunk = read(fd, response_buffer + total_bytes_read, buffer_size - total_bytes_read - 1);

        if (bytes_in_chunk > 0) {
            // Si leímos algo, actualizamos nuestro contador total
            total_bytes_read += bytes_in_chunk;
        } else if (bytes_in_chunk == 0) {
            // Si read devuelve 0, significa que no hay más datos. Hemos terminado.
            break;
        } else {
            // Si read devuelve -1, hubo un error.
            perror("Frontend: Error leyendo de la tubería");
            break;
        }
    }

    // 4. Cerramos la tubería
    close(fd);

    // 5. Nos aseguramos de que la cadena esté terminada en nulo
    response_buffer[total_bytes_read] = '\0';
    
    // 6. Actualizamos la GUI con la respuesta COMPLETA
    gtk_text_buffer_set_text(text_buffer, response_buffer, -1);

    // 7. Liberamos la memoria que asignamos.
    free(response_buffer);
}

void search_id(GtkWidget *widget, gpointer data)
{
    const char *id_to_find = gtk_entry_get_text(GTK_ENTRY(entry_id));
    const char *year_str = gtk_entry_get_text(GTK_ENTRY(entry_year));
    const char *month_str = gtk_entry_get_text(GTK_ENTRY(entry_month));

    // Validate month input
    if (strlen(month_str) > 0)
    {
        int month = atoi(month_str);
        if (month < 1 || month > 12)
        {
            // Si el mes no es valido, mostramos un mensaje de error en el área de texto.
            gtk_text_buffer_set_text(text_buffer, "Error: El mes debe ser un número entre 1 y 12.", -1);
            return;
        }
    }

    send_request_to_backend(id_to_find, year_str, month_str);
    read_response_from_backend();
}

static void activate(GtkApplication *app, gpointer user_data)
{
    GtkWidget *window;
    GtkWidget *grid;
    GtkWidget *label_prompt_id, *label_prompt_year, *label_prompt_month;
    GtkWidget *button_search;

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Sistema de Búsqueda de Préstamos de la Biblioteca Pública de Seattle");
    gtk_window_set_default_size(GTK_WINDOW(window), 700, 500);

    grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(window), grid);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);

    label_prompt_id = gtk_label_new("Ingrese el ID a buscar:");
    gtk_grid_attach(GTK_GRID(grid), label_prompt_id, 0, 0, 1, 1);
    gtk_widget_set_halign(label_prompt_id, GTK_ALIGN_END);

    entry_id = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), entry_id, 1, 0, 2, 1);
    gtk_widget_set_hexpand(entry_id, TRUE);

    label_prompt_year = gtk_label_new("Año (opcional):");
    gtk_grid_attach(GTK_GRID(grid), label_prompt_year, 0, 1, 1, 1);
    gtk_widget_set_halign(label_prompt_year, GTK_ALIGN_END);

    entry_year = gtk_entry_new();
    gtk_entry_set_input_purpose(GTK_ENTRY(entry_year), GTK_INPUT_PURPOSE_DIGITS);
    gtk_entry_set_max_length(GTK_ENTRY(entry_year), 4);
    gtk_grid_attach(GTK_GRID(grid), entry_year, 1, 1, 2, 1);
    gtk_widget_set_hexpand(entry_year, TRUE);

    label_prompt_month = gtk_label_new("Mes (1-12, opcional):");
    gtk_grid_attach(GTK_GRID(grid), label_prompt_month, 0, 2, 1, 1);
    gtk_widget_set_halign(label_prompt_month, GTK_ALIGN_END);

    entry_month = gtk_entry_new();
    gtk_entry_set_input_purpose(GTK_ENTRY(entry_month), GTK_INPUT_PURPOSE_DIGITS);
    gtk_entry_set_max_length(GTK_ENTRY(entry_month), 2);
    gtk_grid_attach(GTK_GRID(grid), entry_month, 1, 2, 2, 1);
    gtk_widget_set_hexpand(entry_month, TRUE);

    button_search = gtk_button_new_with_label("Buscar y Filtrar");
    gtk_grid_attach(GTK_GRID(grid), button_search, 1, 3, 1, 1);
    g_signal_connect(button_search, "clicked", G_CALLBACK(search_id), NULL);

    // 1. Crear una ventana con barras de desplazamiento.
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    // Le decimos que muestre las barras de desplazamiento solo cuando sea necesario.
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    // Le decimos al grid que esta ventana debe expandirse para llenar el espacio.
    gtk_widget_set_hexpand(scrolled_window, TRUE);
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    // La añadimos al grid en la misma posición donde estaba la etiqueta.
    gtk_grid_attach(GTK_GRID(grid), scrolled_window, 0, 4, 3, 1);

    // 2. Crear el área de texto (nuestro reemplazo para GtkLabel).
    text_view = gtk_text_view_new();

    // 3. Obtener el "buffer" del texto. El buffer es donde se guarda el texto.
    // Siempre modificamos el buffer, no la vista directamente.
    text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    gtk_text_buffer_set_text(text_buffer, "Resultados aparecerán aquí.", -1);

    // 4. Configurar el área de texto para que sea de solo lectura.
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    // Opcional: Oculta el cursor parpadeante de texto.
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
    // Opcional: Hace que el texto se ajuste a la ventana (word wrap).
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);

    // 5. ¡El paso CLAVE! Añadimos el área de texto DENTRO de la ventana con scroll.
    gtk_container_add(GTK_CONTAINER(scrolled_window), text_view);

    gtk_widget_show_all(window);
}

int main(int argc, char **argv)
{
    GtkApplication *app;
    int status;

    // Creo las tuberias si no existen. Pero ps el frontend no las crea, las crea el backend.
    mkfifo(INPUT_PIPE, 0666);
    mkfifo(OUTPUT_PIPE, 0666);

    app = gtk_application_new("org.gtk.example", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    // Clean up pipes when done
    unlink(INPUT_PIPE);
    unlink(OUTPUT_PIPE);

    return status;
}