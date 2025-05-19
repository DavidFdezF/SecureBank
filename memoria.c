#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <pthread.h>
#include "estructuras.h"

// Crea y retorna el ID de la memoria compartida
int crear_memoria_compartida() {
    int shm_id = shmget(IPC_PRIVATE, sizeof(TablaCuentas), IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("Error al crear la memoria compartida");
        exit(EXIT_FAILURE);
    }
    return shm_id;
}

// Asocia el segmento de memoria compartida al proceso y devuelve el puntero a la tabla
TablaCuentas *acceder_a_memoria(int shm_id) {
    TablaCuentas *tabla = (TablaCuentas *)shmat(shm_id, NULL, 0);
    if (tabla == (void *)-1) {
        perror("Error al conectar la memoria compartida");
        exit(EXIT_FAILURE);
    }
    return tabla;
}

// Carga los datos desde el archivo cuentas.dat a la memoria compartida
void cargar_cuentas_a_memoria(TablaCuentas *tabla, const char *archivo) {
    FILE *f = fopen(archivo, "rb");
    if (!f) {
        perror("Error al abrir el archivo de cuentas");
        exit(EXIT_FAILURE);
    }

    Cuenta cuenta;
    int i = 0;
    while (fread(&cuenta, sizeof(Cuenta), 1, f) == 1 && i < 100) {
        tabla->cuentas[i] = cuenta;
        tabla->cuentas[i].bloqueado = 0;  // Aseguramos que todas estén activas

        // Inicializamos el mutex de la cuenta
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

        if (pthread_mutex_init(&tabla->cuentas[i].mutex, &attr) != 0) {
            perror("Error al inicializar mutex compartido");
            exit(EXIT_FAILURE);
        }

        i++;
    }

    tabla->num_cuentas = i;
    fclose(f);
}

// Desvincula el segmento de memoria del proceso
void desconectar_memoria(TablaCuentas *tabla) {
    if (shmdt(tabla) == -1) {
        perror("Error al desconectar la memoria compartida");
    }
}

// Elimina definitivamente el segmento de memoria compartida
void eliminar_memoria(int shm_id) {
    if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
        perror("Error al eliminar la memoria compartida");
    }
}
