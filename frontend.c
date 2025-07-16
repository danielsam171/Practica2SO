#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>


#define MAX_LINE_LEN 4096
#define SERVER_IP "127.0.0.1" // IP del servidor (localhost)
#define PORT 3550

GtkWidget *entry_id;        // Aquí se ingresará el ID a buscar.
GtkWidget *entry_year;      // Aquí se ingresará el año (opcional).
GtkWidget *entry_month;     // Aquí se ingresará el mes (opcional, 1-12).
GtkWidget *text_view;       // Aquí se mostrará el texto de los resultados.
GtkTextBuffer *text_buffer; // Este es el "almacén" donde vive el texto que se muestra en text_view.

void search_id(GtkWidget *widget, gpointer data)
{
    const char *id_to_find = gtk_entry_get_text(GTK_ENTRY(entry_id));
    const char *year_str = gtk_entry_get_text(GTK_ENTRY(entry_year));
    const char *month_str = gtk_entry_get_text(GTK_ENTRY(entry_month));

    if (strlen(id_to_find) == 0) {
        gtk_text_buffer_set_text(text_buffer, "Error: El campo ID no puede estar vacío.", -1);
        return;
    }
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

    gtk_text_buffer_set_text(text_buffer, "Buscando, por favor espere...", -1);
    
    // GTK necesita procesar eventos pendientes para redibujar la pantalla.
    // Esta línea fuerza a GTK a actualizar la interfaz y mostrar el mensaje "Buscando..."
    // antes de que nos bloqueemos en la red. Es un truco muy útil.
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }

    // Configuro la conexión al servidor y envío la solicitud. Tengo que configurar la estructura de tipo sockaddr_in

    int socket_fd;
    struct sockaddr_in server;
    char request[MAX_LINE_LEN];

    // Crear el socket
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        gtk_text_buffer_set_text(text_buffer, "Error: No se pudo crear el socket.", -1);
        return;
    }

    //--------------Configurar la estructura del servidor -------------------
    
    server.sin_family = AF_INET; //indica las familias de direcciones (IPv4)
    server.sin_port = htons(PORT);//asigna el puerto al que se va a conectar pero lo pasa a formato de red
    //client.sin_addr.s_addr = INET_ADDRSTRLEN; 
    inet_pton(AF_INET, SERVER_IP, &server.sin_addr);//asigna la direccion IP del servidor al que se va a conectar(local)
    //Direccion local  127.0.0.1
    //25.32.50.226 de la red de juan de hamachi
    //Esa es la IP del servidor al que se va a conectar a travez de hamachi
    //La puedo cambiar por la IP del servidor al que quiero conectarme como por ejemplo la IP local 
    //memset(client.sin_zero, 0, sizeof(client.sin_zero));
    bzero(server.sin_zero, sizeof(server.sin_zero));

    //------------------Conectar al servidor-------------------
    if (connect(socket_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Error de conexión: No se pudo conectar a %s:%d. El servidor no se esta ejecutando", SERVER_IP, PORT);
        gtk_text_buffer_set_text(text_buffer, error_msg, -1);
        close(socket_fd);
        return;
    }

    //--------------Enviar solicitud al servidor-------------------
    snprintf(request, sizeof(request), "%s|%s|%s", id_to_find, year_str, month_str);
    int r = send(socket_fd, request, strlen(request), 0);
    if (r < 0) {
        perror("Error al enviar datos al servidor");
        close(socket_fd);
        return;
    }

     // C.5: Recibir la respuesta (usando lógica de búfer dinámico)

     size_t buffer_size = 4096;
     char *response_buffer = malloc(buffer_size);
     size_t total_bytes_read = 0;
     ssize_t bytes_in_chunk;
 
     while ((bytes_in_chunk = recv(socket_fd, response_buffer + total_bytes_read, buffer_size - total_bytes_read - 1, 0)) > 0) {
         total_bytes_read += bytes_in_chunk;
         if (total_bytes_read >= buffer_size - 1) {
             buffer_size *= 2;
             response_buffer = realloc(response_buffer, buffer_size);
         }
     }
 
     // C.6: Cerrar la conexión
     close(socket_fd);
     
     // C.7: Mostrar el resultado
     response_buffer[total_bytes_read] = '\0';
     gtk_text_buffer_set_text(text_buffer, response_buffer, -1);
 
     // C.8: Liberar memoria
     free(response_buffer);
}

static void activate(GtkApplication *app, gpointer user_data)
{
    GtkWidget *window;
    GtkWidget *grid;
    GtkWidget *label_prompt_id, *label_prompt_year, *label_prompt_month;
    GtkWidget *button_search;

    //Configuracion general de la ventana
    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Sistema de Búsqueda de Préstamos de la Biblioteca Pública de Seattle");
    gtk_window_set_default_size(GTK_WINDOW(window), 700, 500);

    //Configuracion de la rejilla (es como se organiza el contenido en la ventana)
    grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(window), grid);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);

    //Configuracion de los widgets para el ID
    label_prompt_id = gtk_label_new("Ingrese el ID a buscar:");
    gtk_grid_attach(GTK_GRID(grid), label_prompt_id, 0, 0, 1, 1);
    gtk_widget_set_halign(label_prompt_id, GTK_ALIGN_END);

    entry_id = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), entry_id, 1, 0, 2, 1);
    gtk_widget_set_hexpand(entry_id, TRUE);

    //Configuracion de los widgets para el Año
    label_prompt_year = gtk_label_new("Año (opcional):");
    gtk_grid_attach(GTK_GRID(grid), label_prompt_year, 0, 1, 1, 1);
    gtk_widget_set_halign(label_prompt_year, GTK_ALIGN_END);

    entry_year = gtk_entry_new();
    gtk_entry_set_input_purpose(GTK_ENTRY(entry_year), GTK_INPUT_PURPOSE_DIGITS);
    gtk_entry_set_max_length(GTK_ENTRY(entry_year), 4);
    gtk_grid_attach(GTK_GRID(grid), entry_year, 1, 1, 2, 1);
    gtk_widget_set_hexpand(entry_year, TRUE);

    //Configuracion de los widgets para el mes
    label_prompt_month = gtk_label_new("Mes (1-12, opcional):");
    gtk_grid_attach(GTK_GRID(grid), label_prompt_month, 0, 2, 1, 1);
    gtk_widget_set_halign(label_prompt_month, GTK_ALIGN_END);

    entry_month = gtk_entry_new();
    gtk_entry_set_input_purpose(GTK_ENTRY(entry_month), GTK_INPUT_PURPOSE_DIGITS);
    gtk_entry_set_max_length(GTK_ENTRY(entry_month), 2);
    gtk_grid_attach(GTK_GRID(grid), entry_month, 1, 2, 2, 1);
    gtk_widget_set_hexpand(entry_month, TRUE);

    // Botón para buscar y filtrar
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

    // 2. Crear el área de texto 
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

    // 5.Añadimos el área de texto DENTRO de la ventana con scroll.
    gtk_container_add(GTK_CONTAINER(scrolled_window), text_view);

    gtk_widget_show_all(window);
}


int main(int argc, char **argv)
{
    GtkApplication *app;
    int status;
    app = gtk_application_new("org.gtk.example", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}