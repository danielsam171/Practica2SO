#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "indexer.h"

#define INPUT_PIPE "/tmp/frontend_input"
#define OUTPUT_PIPE "/tmp/frontend_output"
#define MAX_LINE_LEN 4096
#define INITIAL_BUFFER_SIZE 4096 // Empezamos con 4KB, un tamaño inicial razonable

// Lee una línea entera de un archivo, sin importar su longitud.
char *read_full_line(FILE *stream)
{
    char *line = NULL;
    size_t buffer_size = 0;
    size_t line_len = 0;
    int c;

    buffer_size = 256; // Empezamos con un tamaño inicial
    line = malloc(buffer_size);
    if (line == NULL)
    {
        return NULL;
    }

    while ((c = fgetc(stream)) != EOF && c != '\n')
    {
        if (line_len + 1 >= buffer_size)
        {
            buffer_size *= 2; // Duplicar tamaño si se llena
            char *new_line = realloc(line, buffer_size);
            if (new_line == NULL)
            {
                free(line);
                return NULL;
            }
            line = new_line;
        }
        line[line_len++] = c;
    }
    line[line_len] = '\0';

    if (line_len == 0 && c == EOF)
    {
        free(line);
        return NULL;
    }

    return line;
}

// Esta función se encarga de añadir una cadena a nuestro búfer dinámico,
// y lo redimensiona si es necesario.
char *append_to_buffer(char *buffer, size_t *buffer_pos, size_t *buffer_size, const char *str_to_add)
{
    size_t len_to_add = strlen(str_to_add);

    // Comprobamos si necesitamos más espacio. (+1 para el carácter nulo '\0')
    if (*buffer_pos + len_to_add + 1 > *buffer_size)
    {
        size_t new_size = *buffer_size * 2; // Duplicamos el tamaño
        // Nos aseguramos de que el nuevo tamaño sea suficiente
        if (new_size < *buffer_pos + len_to_add + 1)
        {
            new_size = *buffer_pos + len_to_add + 1;
        }

        char *new_buffer = realloc(buffer, new_size);
        if (new_buffer == NULL)
        {
            perror("Error: Fallo al redimensionar el búfer");
            free(buffer); // Liberamos la memoria antigua
            return NULL;  // Indicamos el error
        }

        // Si realloc tuvo éxito, actualizamos nuestros datos
        buffer = new_buffer;
        *buffer_size = new_size;
    }

    // Copiamos la nueva cadena en la posición correcta
    strcpy(buffer + *buffer_pos, str_to_add);
    *buffer_pos += len_to_add; // Actualizamos la posición de escritura

    return buffer; // Devolvemos el puntero (pudo haber cambiado por realloc)
}

void perform_search(const char *id_to_find, int filter_year, int filter_month, char *archivo_csv)
{
    // los 3 archivos necesarios
    const char *csv_filepath = archivo_csv; // El archivo CSV que contiene los registros
    const char *header_filepath = "header.dat";
    const char *index_filepath = "index.dat";

    // Abrir el archivo de cabecera para leer la tabla hash
    FILE *header_file = fopen(header_filepath, "rb");
    if (!header_file)
    {
        return;
    }

    // Leer la tabla hash desde el archivo de cabecera
    long header_table[HASH_TABLE_SIZE];
    fread(header_table, sizeof(long), HASH_TABLE_SIZE, header_file);
    fclose(header_file);

    // Buscar el ID (que ya se paso por parametro a la funcion) en la tabla hash
    unsigned int hash_index = hash_function(id_to_find) % HASH_TABLE_SIZE;

    // Obtener la posición del primer nodo en la lista enlazada
    // Si no hay nodos, significa que no hay registros con ese ID y eso pasa si el offset es -1(definicion en la inicializacion de la tabla hash)
    long current_node_offset = header_table[hash_index];

    if (current_node_offset == -1)
    {
        // No hay registros con ese ID en la tabla hash
        // Enviar mensaje de error al frontend
        char not_found_msg[256];
        snprintf(not_found_msg, sizeof(not_found_msg),
                 "ID '%s' no encontrado.", id_to_find);
        int fd = open(OUTPUT_PIPE, O_WRONLY);
        write(fd, not_found_msg, strlen(not_found_msg) + 1);
        close(fd);
        return;
    }

    // Abrir el archivo de índice y el archivo CSV
    FILE *index_file = fopen(index_filepath, "rb");
    if (!index_file)
    {
        fclose(header_file);
        perror("Error abriendo archivo de índice");
        return;
    }

    FILE *csv_file = fopen(csv_filepath, "r");
    if (!csv_file)
    {
        fclose(csv_file);
        fclose(index_file);
        fclose(header_file);
        perror("Error abriendo archivo CSV");
        return;
    }

    // Preparar el buffer para almacenar los resultados
    int found_count = 0;

    // Buffer para leer líneas del CSV y almacenar resultados

    // char line_buffer[MAX_LINE_LEN];
    //  char result_buffer[MAX_LINE_LEN * 10]; // Buffer for results
    size_t buffer_size = INITIAL_BUFFER_SIZE;
    size_t buffer_pos = 0; // Posición actual de escritura (longitud de la cadena)
    char *result_buffer = malloc(buffer_size);

    // Es VITAL comprobar si malloc funcionó
    if (result_buffer == NULL)
    {
        perror("Error: Fallo al asignar memoria inicial");
        // Cerramos los archivos que ya estaban abiertos
        fclose(index_file);
        fclose(csv_file);
        return; // Salimos de la función si no hay memoria
    }
    // la inicializacion del buffer de resultados con una cadena vacía
    result_buffer[0] = '\0';

    const char *csv_headers = "BibNumber,ItemBarcode,ItemType,Collection,CallNumber,CheckoutDateTime\n";

    // Leer el archivo CSV y buscar el ID
    // Recorremos la lista enlazada de nodos en el índice
    // Cada nodo contiene un offset al registro en el CSV( el offset es como la dirección de memoria del registro en el CSV)
    while (current_node_offset != -1)
    {

        fseek(index_file, current_node_offset, SEEK_SET); // Mover al nodo actual en el archivo de índice, para eso es fseek(mover puntero de lectura/escritura)

        // Leemos el nodo completo en la variable 'current_node'.
        IndexNode current_node;
        if (fread(&current_node, sizeof(IndexNode), 1, index_file) != 1)
        {
            // Error al leer el nodo, salimos de la función
            fclose(index_file);
            fclose(csv_file);
            perror("Error leyendo nodo del archivo de índice");
            break;
        }

        // Ahora leemos el registro correspondiente en el archivo CSV usando el offset del nodo
        // fseek mueve el puntero de lectura/escritura al offset del registro en el CSV
        fseek(csv_file, current_node.data_offset, SEEK_SET);

        // fgets lee una línea completa del archivo CSV
        char *full_line = read_full_line(csv_file);
        if (full_line == NULL)
        {
            break; // No hay más líneas o error de memoria
        }

        // 2. Para strtok, necesitamos una copia. La creamos con malloc del tamaño exacto.
        char *line_copy_for_id = malloc(strlen(full_line) + 1);
        if (line_copy_for_id == NULL)
        {
            free(full_line);
            break;
        }
        strcpy(line_copy_for_id, full_line);

        char *record_id = strtok(line_copy_for_id, ",");

        // Verificamos si el ID del registro coincide con el ID que estamos buscando
        if (record_id != NULL && strcmp(record_id, id_to_find) == 0)
        {
            // El ID coincide, ahora filtramos por fecha.
            // Necesitamos otra copia para el segundo strtok.
            char *line_copy_for_date = malloc(strlen(full_line) + 1);
            if (line_copy_for_date == NULL)
            {
                free(full_line);
                free(line_copy_for_id);
                break;
            }
            strcpy(line_copy_for_date, full_line);

            char *date_time_str = NULL;
            char *token = strtok(line_copy_for_date, ",");
            for (int col_idx = 0; token != NULL && col_idx < 5; col_idx++)
            {
                token = strtok(NULL, ",");
            }
            if (token != NULL)
            {
                date_time_str = token;
            }

            int record_month, record_day, record_year;
            if (date_time_str && sscanf(date_time_str, "%d/%d/%d", &record_month, &record_day, &record_year) == 3)
            {
                int year_matches = (filter_year == 0 || record_year == filter_year);
                int month_matches = (filter_month == 0 || record_month == filter_month);
                if (year_matches && month_matches)
                {
                    if (found_count == 0)
                    {
                        result_buffer = append_to_buffer(result_buffer, &buffer_pos, &buffer_size, "Registros encontrados para el ID '");
                        result_buffer = append_to_buffer(result_buffer, &buffer_pos, &buffer_size, id_to_find);
                        result_buffer = append_to_buffer(result_buffer, &buffer_pos, &buffer_size, "'");

                        result_buffer = append_to_buffer(result_buffer, &buffer_pos, &buffer_size, ":\n");
                        result_buffer = append_to_buffer(result_buffer, &buffer_pos, &buffer_size, csv_headers);
                    }
                    result_buffer = append_to_buffer(result_buffer, &buffer_pos, &buffer_size, full_line);
                    result_buffer = append_to_buffer(result_buffer, &buffer_pos, &buffer_size, "\n"); // Añadir salto de línea
                    // Si en algún punto append_to_buffer falla, result_buffer será NULL
                    if (result_buffer == NULL)
                    {
                        free(full_line);
                        free(line_copy_for_id);
                        free(line_copy_for_date);
                        fclose(index_file);
                        fclose(csv_file);
                        return; // Salimos limpiamente
                    }

                    // Añadimos la línea del CSV

                    found_count++;
                }
            }
            free(line_copy_for_date); // Liberar la copia para la fecha
        }

        free(line_copy_for_id);
        free(full_line);

        current_node_offset = current_node.next_node_offset;
    }

    if (found_count == 0)
    {
        // Limpiamos el búfer y construimos el mensaje de forma segura
        buffer_pos = 0;
        result_buffer[0] = '\0';

        result_buffer = append_to_buffer(result_buffer, &buffer_pos, &buffer_size, "ID '");
        result_buffer = append_to_buffer(result_buffer, &buffer_pos, &buffer_size, id_to_find);
        result_buffer = append_to_buffer(result_buffer, &buffer_pos, &buffer_size, "' no encontrado");
        if (filter_year > 0 || filter_month > 0)
        {
            result_buffer = append_to_buffer(result_buffer, &buffer_pos, &buffer_size, " o no hay registros que coincidan con los filtros de fecha.");
        }
    }

    // Send response back to frontend
    int fd = open(OUTPUT_PIPE, O_WRONLY);

    // La longitud de los datos es ahora buffer_pos
    if (fd != -1)
    {
        write(fd, result_buffer, buffer_pos + 1); // +1 para enviar el '\0'
        close(fd);
    }
    else
    {
        perror("Error al abrir el pipe de salida");
    }
    // ¡Paso más importante! Liberar la memoria.
    free(result_buffer);

    fclose(index_file);
    fclose(csv_file);
}

int main()
{
    char request[MAX_LINE_LEN];
    char id_to_find[256];
    char year_str[16];
    char month_str[16];
    int filter_year, filter_month;

    // Create pipes (just in case they don't exist)
    mkfifo(INPUT_PIPE, 0666);
    mkfifo(OUTPUT_PIPE, 0666);

    while (1)
    {
        // Read from input pipe
        int fd = open(INPUT_PIPE, O_RDONLY);
        read(fd, request, sizeof(request));
        close(fd);

        // Parse the request
        char *token = strtok(request, "|");
        if (token)
            strncpy(id_to_find, token, sizeof(id_to_find) - 1);

        token = strtok(NULL, "|");
        if (token)
            strncpy(year_str, token, sizeof(year_str) - 1);
        else
            year_str[0] = '\0';

        token = strtok(NULL, "|");
        if (token)
            strncpy(month_str, token, sizeof(month_str) - 1);
        else
            month_str[0] = '\0';

        // Convert year and month to integers
        filter_year = (strlen(year_str) > 0) ? atoi(year_str) : 0;
        filter_month = (strlen(month_str) > 0) ? atoi(month_str) : 0;

        // Perform the search
        perform_search(id_to_find, filter_year, filter_month, "DataC.csv");
    }

    return 0;
}