#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "indexer.h"

#define MAX_LINE_LEN 2048 // Asumimos un largo máximo de línea en el CSV

int main(void) {

    const char *csv_filepath = "DataC.csv"; // Archivo CSV de entrada
    const char *header_filepath = "header.dat"; // Archivo de cabecera de salida
    const char *index_filepath = "index.dat"; // Archivo de índice de salida


    // 1. Inicializar la tabla de cabecera en memoria
    long header_table[HASH_TABLE_SIZE];
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        header_table[i] = -1; // -1 significa que la lista está vacía
    }

    FILE *csv_file = fopen(csv_filepath, "r");
    if (!csv_file) {
        perror("Error abriendo archivo CSV");
        return 1;
    }

    // Usamos "wb" porque escribiremos datos binarios (structs)
    FILE *index_file = fopen(index_filepath, "wb");
    if (!index_file) {
        perror("Error creando archivo de índice");
        fclose(csv_file);
        return 1;
    }

    char line_buffer[MAX_LINE_LEN];
    // Omitir la primera línea si es una cabecera
    fgets(line_buffer, MAX_LINE_LEN, csv_file);

    printf("Construyendo índice...\n");

    // 2. Recorrer el archivo CSV línea por línea
    long current_data_offset = ftell(csv_file);
    while (fgets(line_buffer, MAX_LINE_LEN, csv_file) != NULL) {
        
        // Copiamos la línea para no modificarla con strtok
        char line_copy[MAX_LINE_LEN];
        strcpy(line_copy, line_buffer);
        
        // Extraer el ID (primera columna)
        char *record_id = strtok(line_copy, ",");
        if (record_id == NULL) {
            current_data_offset = ftell(csv_file);
            continue; // Línea vacía o mal formada
        }

        // 3. Calcular el índice hash
        unsigned int hash_index = hash_function(record_id) % HASH_TABLE_SIZE;

        // 4. Crear el nuevo nodo de índice
        IndexNode new_node;
        new_node.data_offset = current_data_offset;
        // El nuevo nodo apuntará a la "cabeza" anterior de la lista
        new_node.next_node_offset = header_table[hash_index]; 
        
        // 5. Escribir el nuevo nodo al final del archivo de índice
        fseek(index_file, 0, SEEK_END); // Nos aseguramos de escribir al final del archivo
        long new_node_offset = ftell(index_file); //ftell nos da la posición actual en el archivo
        fwrite(&new_node, sizeof(IndexNode), 1, index_file);

        // 6. Actualizar la tabla de cabecera para que apunte a este nuevo nodo
        header_table[hash_index] = new_node_offset;

        // Guardar la posición para la siguiente línea
        current_data_offset = ftell(csv_file);
    }
    
    printf("Proceso de indexación completado.\n");
    fclose(csv_file);
    fclose(index_file);

    // 7. Guardar la tabla de cabecera en su propio archivo
    FILE *header_file = fopen(header_filepath, "wb");
    if (!header_file) {
        perror("Error creando archivo de cabecera");
        return 1;
    }
    fwrite(header_table, sizeof(long), HASH_TABLE_SIZE, header_file);
    fclose(header_file);

    printf("Archivos de índice '%s' y '%s' creados exitosamente.\n", header_filepath, index_filepath);

    return 0;
}