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

void perform_search(const char *id_to_find, int filter_year, int filter_month) {
    //los 3 archivos necesarios
    const char *csv_filepath = "Data2005.csv";
    const char *header_filepath = "header.dat";
    const char *index_filepath = "index.dat";

    // Abrir el archivo de cabecera para leer la tabla hash
    FILE *header_file = fopen(header_filepath, "rb");
    if (!header_file) {
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

    if (current_node_offset == -1) {

        return;
    }

    // Abrir el archivo de índice y el archivo CSV
    FILE *index_file = fopen(index_filepath, "rb");
    if (!index_file) {
        fclose(header_file);
        perror("Error abriendo archivo de índice");
        return;
    }

    FILE *csv_file = fopen(csv_filepath, "r");
    if (!index_file || !csv_file) {
        if (index_file) fclose(index_file);
        if (csv_file) fclose(csv_file);
        perror("Error abriendo archivo CSV");
        return;
    }

    // Preparar el buffer para almacenar los resultados
    int found_count = 0;

    // Buffer para leer líneas del CSV y almacenar resultados
    char line_buffer[MAX_LINE_LEN]; 
    char result_buffer[MAX_LINE_LEN * 10]; // Buffer for results

    // la inicializacion del buffer de resultados con una cadena vacía
    result_buffer[0] = '\0';

    const char *csv_headers = "BibNumber,ItemBarcode,ItemType,Collection,CallNumber,CheckoutDateTime\n";

    // Leer el archivo CSV y buscar el ID
    // Recorremos la lista enlazada de nodos en el índice
    // Cada nodo contiene un offset al registro en el CSV( el offset es como la dirección de memoria del registro en el CSV)
    while (current_node_offset != -1) {


        fseek(index_file, current_node_offset, SEEK_SET);// Mover al nodo actual en el archivo de índice, para eso es fseek(mover puntero de lectura/escritura)
        
        // Leemos el nodo completo en la variable 'current_node'.
        IndexNode current_node;
        fread(&current_node, sizeof(IndexNode), 1, index_file);

        // Ahora leemos el registro correspondiente en el archivo CSV usando el offset del nodo
        // fseek mueve el puntero de lectura/escritura al offset del registro en el CSV
        fseek(csv_file, current_node.data_offset, SEEK_SET);

        // fgets lee una línea completa del archivo CSV
        fgets(line_buffer, MAX_LINE_LEN, csv_file);

        // Verificación de colisión (¿realmente es el mismo ID?).
        char line_copy[MAX_LINE_LEN];

        // Copiamos la línea leída para no modificarla con strtok
        // Esto es necesario porque strtok modifica la cadena original al dividirla en tokens.  
        strncpy(line_copy, line_buffer, sizeof(line_copy) - 1);

        // Aseguramos que la cadena esté terminada en nulo
        line_copy[sizeof(line_copy) - 1] = '\0';

        // Extraer el ID del registro (primera columna)
        // Usamos strtok para dividir la línea en tokens, usando la coma como delimitador
        // El primer token será el ID del registro
        // strtok devuelve un puntero al primer token encontrado en la cadena
        // Si no hay tokens, devuelve NULL.
        char *record_id = strtok(line_copy, ",");


        // Verificamos si el ID del registro coincide con el ID que estamos buscando
        if (record_id != NULL && strcmp(record_id, id_to_find) == 0) {

            //creamos un buffer temporal para almacenar la línea completa del CSV
            // Esto es necesario porque la línea original puede ser modificada por strtok
            // y queremos conservarla intacta para agregarla al resultado final.
            // Usamos strncpy para copiar la línea completa del CSV al buffer temporal.
            char temp_line[MAX_LINE_LEN];
            strncpy(temp_line, line_buffer, sizeof(temp_line) - 1);
            temp_line[sizeof(temp_line) - 1] = '\0';

            // Ahora extraemos la fecha del registro, que está en la sexta columna (índice 5)
            // Usamos strtok nuevamente para dividir la línea en tokens, buscando la fecha.
            char *date_time_str = NULL;
            int col_idx = 0;

            // Usamos strtok para dividir la línea en tokens, buscando la fecha.
            // La fecha está en la sexta columna (índice 5), así que contamos los tokens hasta llegar a esa columna.
            // strtok devuelve un puntero al primer token encontrado en la cadena.
            // Si no hay tokens, devuelve NULL.
            // Usamos un contador col_idx para llevar la cuenta de cuántos tokens hemos encontrado
            // y así poder llegar a la columna de la fecha.
            // Si llegamos a la sexta columna, guardamos el token como date_time_str.
            // Si no llegamos a la sexta columna, date_time_str permanecerá como NULL.
            char *token = strtok(temp_line, ",");
            while (token != NULL && col_idx < 5) {
                token = strtok(NULL, ",");
                col_idx++;
            }
            
            // Si hemos llegado a la sexta columna, token contendrá la fecha y hora del registro.
            // Si no hemos llegado a la sexta columna, token será NULL.
            // En ese caso, date_time_str permanecerá como NULL.
            if (token != NULL) {
                date_time_str = token;
            }
            
            // Ahora tenemos el ID del registro y la fecha y hora del registro.
            // Vamos a verificar si la fecha coincide con los filtros de año y mes.
            // Si los filtros son 0, significa que no se aplican.
            // Si los filtros son mayores que 0, significa que se aplican.
            // Para eso, vamos a extraer el mes y el año de la fecha y hora
            // y compararlos con los filtros.
            // La fecha y hora están en el formato "MM/DD/YYYY HH:MM:SS".
            // Vamos a usar sscanf para extraer el mes, el día y el año de la fecha y hora.
            // sscanf es una función que permite leer datos de una cadena de caracteres
            // y almacenarlos en variables. En este caso, vamos a leer el mes, el día y el año
            // de la fecha y hora y almacenarlos en las variables record_month, record_day y record_year.
            // Luego, vamos a comparar el año y el mes con los filtros.
            // Si el año y el mes coinciden con los filtros, agregamos el registro al resultado.
            // Si el año y el mes no coinciden con los filtros, no agregamos el registro al resultado.
            // Si no hay filtros, agregamos el registro al resultado

            int record_month, record_day, record_year;

            // Extraemos el mes, el día y el año de la fecha y hora
            // usando sscanf. La fecha y hora están en el formato "MM/DD/YYYY HH:
            if (date_time_str && sscanf(date_time_str, "%d/%d/%d", &record_month, &record_day, &record_year) == 3) {


                int year_matches = (filter_year == 0 || record_year == filter_year);
                int month_matches = (filter_month == 0 || record_month == filter_month);

                if (year_matches && month_matches) {
                    if (found_count == 0) {
                        strcat(result_buffer, "Registros encontrados para el ID '");
                        strcat(result_buffer, id_to_find);
                        strcat(result_buffer, "'");
                        
                        if (filter_year > 0) {
                            char year_str[16];
                            snprintf(year_str, sizeof(year_str), " (Año: %d)", filter_year);
                            strcat(result_buffer, year_str);
                        }
                        
                        if (filter_month > 0) {
                            char month_str[16];
                            snprintf(month_str, sizeof(month_str), " (Mes: %d)", filter_month);
                            strcat(result_buffer, month_str);
                        }
                        
                        strcat(result_buffer, ":\n");
                        strcat(result_buffer, csv_headers);
                    }
                    strcat(result_buffer, line_buffer);
                    found_count++;
                }
            }
        }

        current_node_offset = current_node.next_node_offset;
    }

    if (found_count == 0) {
        snprintf(result_buffer, sizeof(result_buffer), 
                "ID '%s' no encontrado", id_to_find);
        if (filter_year > 0) {
            char year_str[16];
            snprintf(year_str, sizeof(year_str), " (Año: %d)", filter_year);
            strcat(result_buffer, year_str);
        }
        if (filter_month > 0) {
            char month_str[16];
            snprintf(month_str, sizeof(month_str), " (Mes: %d)", filter_month);
            strcat(result_buffer, month_str);
        }
        strcat(result_buffer, " o no hay registros que coincidan con los filtros de fecha.");
    }

    // Send response back to frontend
    int fd = open(OUTPUT_PIPE, O_WRONLY);
    write(fd, result_buffer, strlen(result_buffer)+1);
    close(fd);

    fclose(index_file);
    fclose(csv_file);
}

int main() {
    char request[MAX_LINE_LEN];
    char id_to_find[256];
    char year_str[16];
    char month_str[16];
    int filter_year, filter_month;
    
    // Create pipes (just in case they don't exist)
    mkfifo(INPUT_PIPE, 0666);
    mkfifo(OUTPUT_PIPE, 0666);

    while (1) {
        // Read from input pipe
        int fd = open(INPUT_PIPE, O_RDONLY);
        read(fd, request, sizeof(request));
        close(fd);
        
        // Parse the request
        char *token = strtok(request, "|");
        if (token) strncpy(id_to_find, token, sizeof(id_to_find)-1);
        
        token = strtok(NULL, "|");
        if (token) strncpy(year_str, token, sizeof(year_str)-1);
        else year_str[0] = '\0';
        
        token = strtok(NULL, "|");
        if (token) strncpy(month_str, token, sizeof(month_str)-1);
        else month_str[0] = '\0';
        
        // Convert year and month to integers
        filter_year = (strlen(year_str) > 0) ? atoi(year_str) : 0;
        filter_month = (strlen(month_str) > 0) ? atoi(month_str) : 0;
        
        // Perform the search
        perform_search(id_to_find, filter_year, filter_month);
    }
    
    return 0;
}