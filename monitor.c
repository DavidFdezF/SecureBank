#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include "estructuras.h"

#define SEM_NOMBRE "/sem_validacion"
#define MSGKEY 1234
#define MSGKEY_RESPUESTA 5678

char archivo_cuentas[100];

// Leemos los parámetros de configuración desde config.txt
Config leer_configuracion(const char *ruta) {
    FILE *archivo = fopen(ruta, "r");
    if (archivo == NULL) {
        perror("Error al abrir config.txt");
        exit(1);
    }

    Config config;
    char linea[100];
    while (fgets(linea, sizeof(linea), archivo)) {
        if (linea[0] == '#' || strlen(linea) < 3) continue;
            
        // Extraemos cada parámetro de configuración por clave
        if (strstr(linea, "LIMITE_RETIRO")) sscanf(linea, "LIMITE_RETIRO=%d", &config.limite_retiro);
        else if (strstr(linea, "LIMITE_TRANSFERENCIA")) sscanf(linea, "LIMITE_TRANSFERENCIA=%d", &config.limite_transferencia);
        else if (strstr(linea, "UMBRAL_RETIROS")) sscanf(linea, "UMBRAL_RETIROS=%d", &config.umbral_retiros);
        else if (strstr(linea, "UMBRAL_TRANSFERENCIAS")) sscanf(linea, "UMBRAL_TRANSFERENCIAS=%d", &config.umbral_transferencias);
        else if (strstr(linea, "NUM_HILOS")) sscanf(linea, "NUM_HILOS=%d", &config.num_hilos);
        else if (strstr(linea, "ARCHIVO_CUENTAS")) sscanf(linea, "ARCHIVO_CUENTAS=%s", config.archivo_cuentas);
        else if (strstr(linea, "ARCHIVO_LOG")) sscanf(linea, "ARCHIVO_LOG=%s", config.archivo_log);
    }

    fclose(archivo);
    return config;
}

// Revertimos una transacción sospechosa
void revertir_transaccion(int numero_cuenta, float monto, int tipo_operacion) {
    sem_t *sem = sem_open(SEM_NOMBRE, 0);
    if (sem == SEM_FAILED) {
        perror("Monitor: No se pudo abrir el semáforo");
        return;
    }

    sem_wait(sem); // Bloqueamos el acceso al archivo de cuentas

    FILE *archivo = fopen(archivo_cuentas, "rb+");
    if (!archivo) {
        perror("Monitor: No se pudo abrir el archivo de cuentas");
        sem_post(sem);
        return;
    }

    Cuenta cuenta;
     // Recorremos todas las cuentas hasta encontrar la cuenta afectada
    while (fread(&cuenta, sizeof(Cuenta), 1, archivo)) {
        if (cuenta.numero_cuenta == numero_cuenta) {
             // Revertimos según el tipo de operación original
            if (tipo_operacion == 1) {
                cuenta.saldo -= monto;  // Revertir depósito
            } else if (tipo_operacion == 2 || tipo_operacion == 3) {
                cuenta.saldo += monto;  // Revertir retiro o transferencia
            }
            cuenta.num_transacciones--;

            // Reposicionamos el puntero y sobrescribimos la cuenta actualizada
            fseek(archivo, -sizeof(Cuenta), SEEK_CUR);
            fwrite(&cuenta, sizeof(Cuenta), 1, archivo);

            printf("\nAVISO MONITOR: Operación sospechosa revertida en cuenta %d\n", numero_cuenta);
            printf("Monto: %.2f | Nuevo saldo: %.2f\n\n", monto, cuenta.saldo);
            break;
        }
    }

    fclose(archivo);
    sem_post(sem);
    sem_close(sem);
}

int main() {
    // Cargamos la configuración
    Config cfg = leer_configuracion("config.txt");
    strcpy(archivo_cuentas, cfg.archivo_cuentas);

    // Accedemos a la cola de mensajes principal del banco
    int msgid = msgget(MSGKEY, 0666);
    if (msgid == -1) {
        perror("Monitor: No se pudo acceder a la cola de mensajes");
        return 1;
    }

    // Accedemos a la cola de respuestas
    int msgid_respuesta = msgget(MSGKEY_RESPUESTA, IPC_CREAT | 0666);
    if (msgid_respuesta == -1) {
        perror("Monitor: No se pudo acceder a la cola de respuestas");
        return 1;
    }

    sem_t *sem = sem_open(SEM_NOMBRE, 0);
    if (sem == SEM_FAILED) {
        perror("Monitor: No se pudo abrir el semáforo");
        return 1;
    }

    printf("Monitor activo: escuchando transacciones...\n");
    printf("Límite de retiro: %d\n", cfg.limite_retiro);
    printf("Límite de transferencia: %d\n", cfg.limite_transferencia);

    struct msgbuf mensaje;

    // Iniciamos ciclo infinito de escucha
    while (1) {
        memset(&mensaje, 0, sizeof(mensaje));

         // Esperamos recibir un mensaje del banco
        if (msgrcv(msgid, &mensaje, sizeof(mensaje.mtext), 0, 0) > 0) {
            float monto = 0.0;
            int cuenta = 0;
            int cuenta_destino = 0;
            int tipo_operacion = 0;

            // Detectamos tipo de operación y extraemos datos
            if (strstr(mensaje.mtext, "Depósito") != NULL) {
                tipo_operacion = 1;
                sscanf(mensaje.mtext, "%*[^0123456789]%f%*[^0123456789]%d", &monto, &cuenta);
            } else if (strstr(mensaje.mtext, "Retirada") != NULL) {
                tipo_operacion = 2;
                sscanf(mensaje.mtext, "%*[^0123456789]%f%*[^0123456789]%d", &monto, &cuenta);
            } else if (strstr(mensaje.mtext, "Transferencia") != NULL) {
                tipo_operacion = 3;
                int r = sscanf(mensaje.mtext, "Transferencia de %f desde cuenta %d a cuenta %d", &monto, &cuenta, &cuenta_destino);
                if (r != 3) {
                    printf("Monitor: no se pudo interpretar correctamente la transferencia:\n%s\n", mensaje.mtext);
                    tipo_operacion = 0;
                }
            }

            // Preparamos estructura de respuesta al banco
            struct msgbuf respuesta;
            respuesta.mtype = 1;

            // Definimos el umbral de control según tipo de operación
            int umbral = 0;
            if (tipo_operacion == 2) {
                umbral = cfg.limite_retiro;
            } else if (tipo_operacion == 3) {
                umbral = cfg.limite_transferencia;
            }

            // Evaluamos si la operación es sospechosa
            if (tipo_operacion != 0 && monto > umbral) {
                if (tipo_operacion == 1) {
                    // Ignoramos depósitos simples
                    strcpy(respuesta.mtext, "OK");
                    sem_post(sem);
                    msgsnd(msgid_respuesta, &respuesta, strlen(respuesta.mtext) + 1, 0);
                    continue;  // Pasamos a la siguiente operación
                }                
                // Revertimos la transacción
                revertir_transaccion(cuenta, monto, tipo_operacion);
                if (tipo_operacion == 3) {
                    revertir_transaccion(cuenta_destino, monto, 1);  // revertir depósito
                }
                // Enviamos alerta al banco
                snprintf(respuesta.mtext, sizeof(respuesta.mtext), "ALERTA: Operación revertida en cuenta %d por monto %.2f", cuenta, monto);
            } else {
                // Si es válida respondemos OK 
                strcpy(respuesta.mtext, "OK");
                sem_post(sem);
            }
             // Enviamos la respuesta al banco
            msgsnd(msgid_respuesta, &respuesta, strlen(respuesta.mtext) + 1, 0);
        } else {
            usleep(100000);
        }
    }

    sem_close(sem);
    return 0;
}
