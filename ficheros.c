#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <fcntl.h>
#include <time.h>
#include "ficheros.h"
#include "estructuras.h"  // Asegura el uso de Cuenta*

#define SEM_LOG_NOMBRE "/sem_logs"

// Crea la carpeta del usuario si no existe
void crear_directorio_usuario(int numero_cuenta) {
    char path[100];
    snprintf(path, sizeof(path), "transacciones/%d", numero_cuenta);
    mkdir("transacciones", 0777);
    mkdir(path, 0777);
}

// Escribe una transacción en el log del usuario
void escribir_log_usuario(int numero_cuenta, const char *mensaje) {
    char path[150];
    snprintf(path, sizeof(path), "transacciones/%d/transacciones.log", numero_cuenta);

    sem_t *sem = sem_open(SEM_LOG_NOMBRE, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        perror("Error al abrir semáforo de logs");
        return;
    }

    sem_wait(sem);
    FILE *log = fopen(path, "a");
    if (!log) {
        perror("No se pudo abrir el archivo de log del usuario");
        sem_post(sem);
        sem_close(sem);
        return;
    }

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "[%Y-%m-%d %H:%M:%S]", t);
    fprintf(log, "%s %s\n", timestamp, mensaje);

    fclose(log);
    sem_post(sem);
    sem_close(sem);
}

// Guarda las cuentas en el fichero binario (cuentas.dat)
void guardar_cuentas_en_fichero(Cuenta *cuentas, int num_cuentas, const char *ruta_archivo) {
    FILE *archivo = fopen(ruta_archivo, "wb");
    if (!archivo) {
        perror("Error al abrir archivo para guardar cuentas");
        return;
    }

    fwrite(cuentas, sizeof(Cuenta), num_cuentas, archivo);
    fclose(archivo);
}
