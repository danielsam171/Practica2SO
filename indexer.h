#ifndef INDEXER_H
#define INDEXER_H

// Tamaño de nuestra tabla hash principal. Un número primo suele ser una buena elección.
#define HASH_TABLE_SIZE 65536

// Estructura para cada nodo en nuestro archivo index.dat
// Cada nodo es un eslabón de una lista enlazada.
typedef struct {
    long data_offset;      // Posición del registro en dataset.csv
    long next_node_offset; // Posición del siguiente IndexNode en index.dat (-1 si es el final)
} IndexNode;

// Función Hash (djb2, una de las más simples y efectivas para strings)
// Toma una cadena (el ID) y devuelve un entero sin signo.
unsigned long hash_function(const char *str) {
    unsigned long hash = 5381;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }

    return hash;
}

#endif // INDEXER_H