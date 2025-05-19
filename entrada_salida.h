#ifndef ENTRADA_SALIDA_H
#define ENTRADA_SALIDA_H

#include "estructuras.h"

typedef struct {
    Cuenta operaciones[10];
    int inicio;
    int fin;
    int count;
} BufferEstructurado;

void inicializar_buffer();
void insertar_en_buffer(Cuenta cuenta);
void *gestionar_entrada_salida(void *arg);

#endif
