#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#define MAX_LINE_LEN 4096

void hacer_combinacion(char *archivo_csv)
{
    
    const char *csv_filepath = archivo_csv;
    const char *combinado_filepath = "combinado.csv";

    FILE *csv_file = fopen(csv_filepath, "r");
    if (!csv_file)
    {
        perror("Error abriendo archivo CSV");
        return;
    }

    FILE *combinado_file = fopen(combinado_filepath, "ab");
    if (!combinado_file)
    {
        fclose(csv_file);
        perror("Error creando archivo combinado");
        return;
    }

    fseek(combinado_file, 0, SEEK_END);


    char *line_buffer = NULL; // Puntero para la línea
    size_t line_size = 0;     // Tamaño de la línea


    if(getline(&line_buffer, &line_size, csv_file) == -1 && ftell(combinado_file) != 0)
    {
        // Si getline falla, significa que el archivo CSV está vacío o hubo un error
        fclose(csv_file);
        fclose(combinado_file);
        perror("Error leyendo la primera línea del CSV");
        return;
    }
    // Leer línea por línea usando getline
    while (getline(&line_buffer, &line_size, csv_file) != -1)
    {
        // Escribir la línea en el archivo combinado
        fprintf(combinado_file, "%s", line_buffer);
    }

    // Liberar memoria y cerrar archivos
    free(line_buffer);
    fclose(csv_file);
    fclose(combinado_file);

    printf("Archivo combinado creado exitosamente.\n");
}

int main()

{
    for (int year = 2005; year <= 2017; year++) // Iterar desde 2005 hasta 2017
    {
        char archivo_csv[20]; // Buffer para el nombre del archivo
        sprintf(archivo_csv, "Data%d.csv", year); // Construir el nombre dinámicamente

        printf("Procesando archivo: %s\n", archivo_csv);
        hacer_combinacion(archivo_csv); // Llamar a la función con el nombre dinámico
    }
    return 0;
}