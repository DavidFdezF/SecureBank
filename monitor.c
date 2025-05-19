#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <pthread.h>

#include "estructuras.h"
#include "memoria.h"
#include "configuracion.h"
#include "ficheros.h"

#define MSGKEY 1234
#define MSGKEY_RESPUESTA 5678

TablaCuentas *tabla = NULL;

// Revierte una transacción en memoria compartida
void revertir_transaccion(int numero_cuenta, float monto, int tipo_operacion) {
    for (int i = 0; i < tabla->num_cuentas; i++) {
        Cuenta *cuenta = &tabla->cuentas[i];
        if (cuenta->numero_cuenta == numero_cuenta) {
            pthread_mutex_lock(&cuenta->mutex);

            if (tipo_operacion == 1) {
                cuenta->saldo -= monto; // Revertir depósito
            } else if (tipo_operacion == 2 || tipo_operacion == 3) {
                cuenta->saldo += monto; // Revertir retiro o transferencia
            }

            if (cuenta->num_transacciones > 0) {
                cuenta->num_transacciones--;
            }

            pthread_mutex_unlock(&cuenta->mutex);

            printf("[MONITOR] Reversión en cuenta %d | Nuevo saldo: %.2f\n",
                   cuenta->numero_cuenta, cuenta->saldo);
            break;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <shm_id>\n", argv[0]);
        return 1;
    }

    int shm_id = atoi(argv[1]);
    tabla = acceder_a_memoria(shm_id);

    Config cfg = leer_configuracion("config.txt");

    int msgid = msgget(MSGKEY, 0666);
    int msgid_respuesta = msgget(MSGKEY_RESPUESTA, IPC_CREAT | 0666);

    if (msgid == -1 || msgid_respuesta == -1) {
        perror("Monitor: error al acceder a colas de mensajes");
        return 1;
    }

    struct msgbuf mensaje;
    struct msgbuf respuesta;
    printf("[MONITOR] Iniciado y escuchando transacciones sospechosas...\n");

    while (1) {
        memset(&mensaje, 0, sizeof(mensaje));

        if (msgrcv(msgid, &mensaje, sizeof(mensaje.mtext), 0, 0) > 0) {
            float monto = 0.0;
            int cuenta = 0;
            int cuenta_destino = 0;
            int tipo_operacion = 0;

            if (strstr(mensaje.mtext, "Depósito") != NULL) {
                tipo_operacion = 1;
                sscanf(mensaje.mtext, "%*[^0123456789]%f%*[^0123456789]%d", &monto, &cuenta);
            } else if (strstr(mensaje.mtext, "Retirada") != NULL) {
                tipo_operacion = 2;
                sscanf(mensaje.mtext, "%*[^0123456789]%f%*[^0123456789]%d", &monto, &cuenta);
            } else if (strstr(mensaje.mtext, "Transferencia") != NULL) {
                tipo_operacion = 3;
                sscanf(mensaje.mtext, "Transferencia de %f desde cuenta %d a cuenta %d",
                       &monto, &cuenta, &cuenta_destino);
            }

            respuesta.mtype = 1;

            int umbral = 0;
            if (tipo_operacion == 2) umbral = cfg.limite_retiro;
            else if (tipo_operacion == 3) umbral = cfg.limite_transferencia;

            if (tipo_operacion != 0 && monto > umbral) {
                if (tipo_operacion == 1) {
                    strcpy(respuesta.mtext, "OK"); // No se revierte depósitos
                } else {
                    revertir_transaccion(cuenta, monto, tipo_operacion);
                    if (tipo_operacion == 3) {
                        revertir_transaccion(cuenta_destino, monto, 1);
                    }

                    snprintf(respuesta.mtext, sizeof(respuesta.mtext),
                             "ALERTA: Operación revertida en cuenta %d por monto %.2f", cuenta, monto);

                    // ✍ Registrar la alerta en el log del usuario
                    escribir_log_usuario(cuenta, respuesta.mtext);
                    if (tipo_operacion == 3) {
                        escribir_log_usuario(cuenta_destino, respuesta.mtext);
                    }
                }
            } else {
                strcpy(respuesta.mtext, "OK");
            }

            msgsnd(msgid_respuesta, &respuesta, strlen(respuesta.mtext) + 1, 0);
        } else {
            usleep(100000); // Espera corta antes de siguiente intento
        }
    }

    desconectar_memoria(tabla);
    return 0;
}
