#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "entrada_salida.h"
#include "estructuras.h"

#define BUFFER_SIZE 10

static BufferEstructurado buffer;
static pthread_mutex_t mutex_buffer = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_no_vacio = PTHREAD_COND_INITIALIZER;
static pthread_cond_t cond_no_lleno = PTHREAD_COND_INITIALIZER;

void inicializar_buffer() {
    buffer.inicio = 0;
    buffer.fin = 0;
    buffer.count = 0;
}

// Inserta una cuenta modificada en el buffer circular
void insertar_en_buffer(Cuenta cuenta) {
    pthread_mutex_lock(&mutex_buffer);

    while (buffer.count == BUFFER_SIZE) {
        pthread_cond_wait(&cond_no_lleno, &mutex_buffer);
    }

    buffer.operaciones[buffer.fin] = cuenta;
    buffer.fin = (buffer.fin + 1) % BUFFER_SIZE;
    buffer.count++;

    pthread_cond_signal(&cond_no_vacio);
    pthread_mutex_unlock(&mutex_buffer);
}

// Hilo que escribe periódicamente en disco desde el buffer
void *gestionar_entrada_salida(void *arg) {
    const char *archivo = (const char *)arg;

    while (1) {
        pthread_mutex_lock(&mutex_buffer);

        while (buffer.count == 0) {
            pthread_cond_wait(&cond_no_vacio, &mutex_buffer);
        }

        Cuenta cuenta = buffer.operaciones[buffer.inicio];
        buffer.inicio = (buffer.inicio + 1) % BUFFER_SIZE;
        buffer.count--;

        pthread_cond_signal(&cond_no_lleno);
        pthread_mutex_unlock(&mutex_buffer);

        // Escritura en archivo cuentas.dat
        FILE *f = fopen(archivo, "rb+");
        if (!f) {
            perror("Error al abrir cuentas.dat desde hilo de E/S");
            continue;
        }

        // Buscar por número de cuenta
        Cuenta tmp;
        int index = 0;
        while (fread(&tmp, sizeof(Cuenta), 1, f)) {
            if (tmp.numero_cuenta == cuenta.numero_cuenta) {
                fseek(f, index * sizeof(Cuenta), SEEK_SET);
                fwrite(&cuenta, sizeof(Cuenta), 1, f);
                break;
            }
            index++;
        }

        fclose(f);
    }

    return NULL;
}
