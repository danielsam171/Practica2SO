#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "indexer.h"

#define INPUT_PIPE "/tmp/frontend_input"
#define OUTPUT_PIPE "/tmp/frontend_output"
#define MAX_LINE_LEN 4096
#define INITIAL_BUFFER_SIZE 4096 // Empezamos con 4KB, un tamaño inicial razonable
#define PORT 3550
#define BACKLOG 8

int serverfd,clientfd;

void cerrar_servidor(int signo) {
    close(serverfd);
    close(clientfd);
    printf("\nServidor cerrado correctamente.\n");
    fflush(stdout);
    exit(0);
}

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
        perror("Error: Fallo al asignar memoria para la línea");
        return NULL;
    }

    // Leemos carácter por carácter (fgetc) hasta encontrar un salto de línea o EOF(Final del archivo)
    while ((c = fgetc(stream)) != EOF && c != '\n')
    {
        //En caso de que el búfer se llene, lo redimensionamos
        if (line_len + 1 >= buffer_size)
        {
            buffer_size *= 2; // Duplicar el tamano es la mejor opción para redimencionar
            char *new_line = realloc(line, buffer_size); //Copiamos todo lo que teniuamos en el búfer pero ahora con más espacio
            if (new_line == NULL)
            {
                perror("Error: Fallo al redimensionar el búfer");
                free(line);
                return NULL;
            }
            line = new_line; // Actualizamos el puntero al nuevo búfer
        }
        line[line_len++] = c; //Construyo la nueva línea, caracter por caracter
    }
    line[line_len] = '\0'; //Añadimos el carácter nulo al final de la cadena

    // Si no leímos nada y llegamos al final del archivo, devolvemos NULL
    if (line_len == 0 && c == EOF)
    {   
        free(line);
        return NULL;
    }

    return line; // Devolvemos la línea completa leída
}

// Esta función se encarga de añadir una cadena a nuestro búfer dinámico, y lo redimensiona si es necesario.

//buffer es el bufer donde se almacenan los datos
//buffer_pos es la posición actual de escritura en el bufer
//buffer_size es el tamaño actual del búfer
//str_to_add es la cadena que queremos añadir al búfer
char *append_to_buffer(char *buffer, size_t *buffer_pos, size_t *buffer_size, const char *str_to_add)
{
    size_t len_to_add = strlen(str_to_add);

    // Comprobamos si necesitamos más espacio. (+1 para el carácter nulo  '\0')
    if (*buffer_pos + len_to_add + 1 > *buffer_size)
    {
        size_t new_size = *buffer_size * 2; // Duplicamos el tamaño
        // Nos aseguramos de que el nuevo tamaño sea suficiente
        if (new_size < *buffer_pos + len_to_add + 1)
        {
            new_size = *buffer_pos + len_to_add + 1;
        }
        //Redimencionamos el búfer existente, este contienen la misma información pero ahora con más espacio
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

// Esta función realiza la búsqueda del ID en el archivo CSV y filtra por año y mes si se proporcionan.
void perform_search(const char *id_to_find, int filter_year, int filter_month, char *archivo_csv)
{
    int r;
    // los 3 archivos necesarios
    const char *csv_filepath = archivo_csv; // El archivo CSV que contiene los registros
    const char *header_filepath = "header.dat";
    const char *index_filepath = "index.dat";

    // Abrir el archivo de cabecera para leer la tabla hash
    FILE *header_file = fopen(header_filepath, "rb");
    if (!header_file)
    {
        perror("Error abriendo archivo de cabecera");
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

        /*
        int fd = open(OUTPUT_PIPE, O_WRONLY);
        write(fd, not_found_msg, strlen(not_found_msg) + 1);
        close(fd);
        */
        //----------Enviar un mensaje al cliente-------------

        r = send(clientfd, not_found_msg,sizeof(not_found_msg), 0);
        if (r < 0) {
            perror("Error al enviar datos al cliente");
            close(clientfd);
            close(serverfd);
            return;
        }
        return;
    }

    // Abrir el archivo de índice y el archivo CSV
    FILE *index_file = fopen(index_filepath, "rb");
    if (!index_file)
    {
        fclose(header_file);
        fclose(index_file);
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

    size_t buffer_size = INITIAL_BUFFER_SIZE;
    size_t buffer_pos = 0; // Posición actual de escritura (longitud de la cadena)

    //reservo espacio para el buffer de resultados
    char *result_buffer = malloc(buffer_size);

    // Vaalido error
    if (result_buffer == NULL)
    {
        perror("Error: Fallo al asignar memoria inicial");
        // Cerramos los archivos que ya estaban abiertos
        fclose(index_file);
        fclose(csv_file);
        return; // Salimos de la función si no hay memoria
    }

    // la inicializacion del buffer de resultados con una cadena vacía para iniciar
    result_buffer[0] = '\0';

    const char *csv_headers = "BibNumber,ItemBarcode,ItemType,Collection,CallNumber,CheckoutDateTime\n";

    // Leer el archivo CSV y buscar el ID
    // Recorremos la lista enlazada de nodos en el índice
    // Cada nodo contiene un offset al registro en el CSV(el offset es como la dirección de memoria del registro en el CSV)
    while (current_node_offset != -1)
    {
        // Mover al nodo actual en el archivo de índice, para eso es fseek(mover puntero de lectura/escritura)
        fseek(index_file, current_node_offset, SEEK_SET); 

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
        // ya tenemos la dirección del registro en el CSV, así que movemos el puntero de lectura al offset del registro
        fseek(csv_file, current_node.data_offset, SEEK_SET);

        // leemos la línea completa del CSV sin importar su longitud
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

        // Copiamos la línea completa para no modificarla con strtok
        strcpy(line_copy_for_id, full_line);

        // Extraemos el ID del registro, que está en la primera columna (índice 0)
        char *record_id = strtok(line_copy_for_id, ",");

        // Verificamos si el ID del registro coincide con el ID que estamos buscando
        if (record_id != NULL && strcmp(record_id, id_to_find) == 0)
        {
            // El ID coincide, ahora filtramos por fecha.
            // Necesitamos otra copia de la línea completa para extraer la fecha
            char *line_copy_for_date = malloc(strlen(full_line) + 1);
            if (line_copy_for_date == NULL)
            {
                free(full_line);
                free(line_copy_for_id);
                break;
            }
            strcpy(line_copy_for_date, full_line);
            
            // Extraemos la fecha del registro, que está en la sexta columna (índice 5)
            // Usamos strtok para saltar a la columna de fecha
            char *date_time_str = NULL;
            char *token = strtok(line_copy_for_date, ",");
            for (int col_idx = 0; token != NULL && col_idx < 5; col_idx++)
            {
                token = strtok(NULL, ",");
            }

            // Ahora token apunta a la columna de fecha (sexta columna)
            // data_time_str <-- fecha y hora del registro
            if (token != NULL)
            {
                date_time_str = token;
            }
            
            //En este punto voy a guardar la fecha en estas 3 variables
            int record_month, record_day, record_year;

            // Ahora extraemos el mes, día y año de la fecha aunque no necesitamos el dia
            if (date_time_str && sscanf(date_time_str, "%d/%d/%d", &record_month, &record_day, &record_year) == 3)
            {
                // Comprobamos si el año y mes coinciden con los filtros o si no hay filtros
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
    /*
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
    */

   //----------Enviar un mensaje al cliente-------------

   r = send(clientfd,result_buffer, buffer_pos, 0);
   if (r < 0) {
       perror("Error al enviar datos al cliente");
       close(clientfd);
       close(serverfd);
       return;
   }
   free(result_buffer);
   fclose(index_file);
   fclose(csv_file);
   close(clientfd);
}

int main()
{
    signal(SIGINT, cerrar_servidor);
    char buffer_s[100];
    char request[MAX_LINE_LEN];
    char id_to_find[256];
    char year_str[16];
    char month_str[16];
    int filter_year, filter_month;

    socklen_t lenclient;
    struct sockaddr_in server,client;
    int r;

    //-----------------Creacion del socket del servidor-------------
 
    serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverfd < 0) {
        perror("Error al crear el socket");
        return -1;
    }

    //---------Configuracion de la estructura del servidor----------

    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = INADDR_ANY; // Acepta conexiones de cualquier dirección IP
//   memset(server.sin_zero, 0, sizeof(server.sin_zero));
    bzero(server.sin_zero, sizeof(server.sin_zero));

    //-----Asociar el socket a una red y a un puerto--------

    r = bind(serverfd, (struct sockaddr *)&server, sizeof(server));
    if (r == -1) {
        perror("Error en el bind");
        exit(-1);
    }

    //------------Escuchar conexiones entrantes--------------
    r = listen(serverfd, BACKLOG);
    if (r == -1) {
        perror("Error en el listen");
        exit(-1);
    }

    while (1)
    {
        // Leer del frontend a través de sockets
        //acept devuelve el descriptor del socket del cliente
        printf("Servidor escuchando en el puerto %d...\n", PORT);
        clientfd = accept(serverfd, (struct sockaddr *)&client, &lenclient);
        if (clientfd < 0) {
            perror("Error al aceptar la conexión");
            close(serverfd);
            return -1;
        }

        //----------Recibir un mensaje del cliente-------------
        r = recv(clientfd, request, sizeof(request), 0);
        if (r < 0) {
            perror("Error al recibir datos del cliente");
            close(clientfd);
            close(serverfd);
            return -1;
        }
        printf("\nServidor: Mensaje recibido del cliente: %s\n", request);
        request[r] = '\0';
        
        /*int fd = open(INPUT_PIPE, O_RDONLY);
        read(fd, request, sizeof(request));
        close(fd);*/

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

        // convertir el mes y el año a enteros (si están vacíos, se convierten a 0)
        filter_year = (strlen(year_str) > 0) ? atoi(year_str) : 0;
        filter_month = (strlen(month_str) > 0) ? atoi(month_str) : 0;

        // Perform the search
        perform_search(id_to_find, filter_year, filter_month, "DataC.csv");
    }

    return 0;
}