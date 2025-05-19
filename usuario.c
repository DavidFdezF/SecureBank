#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "estructuras.h"
#include "memoria.h"
#include "configuracion.h"
#include "ficheros.h"
#include "entrada_salida.h"

TablaCuentas *tabla;
int write_fd;
int cuenta_activa;

// Operación depósito o retirada
void *ejecutar_operacion(void *arg) {
    DatosOperacion *datos = (DatosOperacion *)arg;
    char mensaje_final[256] = "";
    int encontrada = 0;

    for (int i = 0; i < tabla->num_cuentas; i++) {
        Cuenta *cuenta = &tabla->cuentas[i];

        if (cuenta->numero_cuenta == datos->numero_cuenta) {
            encontrada = 1;
            pthread_mutex_lock(&cuenta->mutex);

            if (datos->tipo_operacion == 1) {
                cuenta->saldo += datos->monto;
                cuenta->num_transacciones++;
                snprintf(mensaje_final, sizeof(mensaje_final),
                         "Depósito de %.2f dinero en cuenta %d (Titular: %s)",
                         datos->monto, cuenta->numero_cuenta, cuenta->titular);
            } else if (datos->tipo_operacion == 2) {
                if (cuenta->saldo < datos->monto) {
                    snprintf(mensaje_final, sizeof(mensaje_final),
                             "Saldo insuficiente en cuenta %d (Titular: %s)",
                             cuenta->numero_cuenta, cuenta->titular);
                    pthread_mutex_unlock(&cuenta->mutex);
                    break;
                }
                cuenta->saldo -= datos->monto;
                cuenta->num_transacciones++;
                snprintf(mensaje_final, sizeof(mensaje_final),
                         "Retirada de %.2f dinero en cuenta %d (Titular: %s)",
                         datos->monto, cuenta->numero_cuenta, cuenta->titular);
            }

            pthread_mutex_unlock(&cuenta->mutex);
            insertar_en_buffer(*cuenta);  // Escritura diferida
            break;
        }
    }

    if (!encontrada) {
        snprintf(mensaje_final, sizeof(mensaje_final),
                 "Cuenta %d no encontrada", datos->numero_cuenta);
    }

    write(write_fd, mensaje_final, strlen(mensaje_final));
    write(write_fd, "\n", 1);
    printf("\n%s\n", mensaje_final);
    escribir_log_usuario(datos->numero_cuenta, mensaje_final);

    free(datos);
    pthread_exit(NULL);
}

// Operación transferencia
void *ejecutar_transferencia(void *arg) {
    DatosOperacion *datos = (DatosOperacion *)arg;
    Cuenta *origen = NULL;
    Cuenta *destino = NULL;

    for (int i = 0; i < tabla->num_cuentas; i++) {
        if (tabla->cuentas[i].numero_cuenta == datos->numero_cuenta)
            origen = &tabla->cuentas[i];
        else if (tabla->cuentas[i].numero_cuenta == datos->cuenta_destino)
            destino = &tabla->cuentas[i];
    }

    char mensaje_final[256] = "";

    if (!origen || !destino) {
        snprintf(mensaje_final, sizeof(mensaje_final),
                 "Transferencia fallida: cuenta no encontrada.");
    } else if (origen == destino) {
        snprintf(mensaje_final, sizeof(mensaje_final),
                 "Transferencia inválida: cuentas iguales.");
    } else {
        if (origen->numero_cuenta < destino->numero_cuenta) {
            pthread_mutex_lock(&origen->mutex);
            pthread_mutex_lock(&destino->mutex);
        } else {
            pthread_mutex_lock(&destino->mutex);
            pthread_mutex_lock(&origen->mutex);
        }

        if (origen->saldo < datos->monto) {
            snprintf(mensaje_final, sizeof(mensaje_final),
                     "Transferencia fallida: saldo insuficiente en cuenta %d.", origen->numero_cuenta);
        } else {
            origen->saldo -= datos->monto;
            destino->saldo += datos->monto;
            origen->num_transacciones++;
            destino->num_transacciones++;

            snprintf(mensaje_final, sizeof(mensaje_final),
                     "Transferencia de %.2f desde cuenta %d a cuenta %d (Titular: %s)",
                     datos->monto, origen->numero_cuenta, destino->numero_cuenta, origen->titular);

            insertar_en_buffer(*origen);
            insertar_en_buffer(*destino);
        }

        pthread_mutex_unlock(&origen->mutex);
        pthread_mutex_unlock(&destino->mutex);
    }

    write(write_fd, mensaje_final, strlen(mensaje_final));
    write(write_fd, "\n", 1);
    printf("\n%s\n", mensaje_final);

    escribir_log_usuario(datos->numero_cuenta, mensaje_final);
    escribir_log_usuario(datos->cuenta_destino, mensaje_final);

    free(datos);
    pthread_exit(NULL);
}

// Consulta de saldo
void consultar_saldo(int numero_cuenta) {
    for (int i = 0; i < tabla->num_cuentas; i++) {
        Cuenta *cuenta = &tabla->cuentas[i];
        if (cuenta->numero_cuenta == numero_cuenta) {
            pthread_mutex_lock(&cuenta->mutex);
            printf("\nConsulta de saldo:\nCuenta: %d\nTitular: %s\nSaldo: %.2f\nTransacciones: %d\n",
                   cuenta->numero_cuenta, cuenta->titular,
                   cuenta->saldo, cuenta->num_transacciones);
            pthread_mutex_unlock(&cuenta->mutex);
            return;
        }
    }
    printf("Cuenta %d no encontrada.\n", numero_cuenta);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <fd_escritura> <shm_id>\n", argv[0]);
        return 1;
    }

    write_fd = atoi(argv[1]);
    int shm_id = atoi(argv[2]);

    tabla = acceder_a_memoria(shm_id);

    // Selección de cuenta activa
    printf("Bienvenido a SecureBank\n");
    printf("Introduzca su número de cuenta: ");
    char linea[100];
    if (!fgets(linea, sizeof(linea), stdin) || sscanf(linea, "%d", &cuenta_activa) != 1) {
        printf("Número de cuenta inválido.\n");
        return 1;
    }

    crear_directorio_usuario(cuenta_activa);

    int opcion;
    int cuenta_destino;
    float monto;
    int continuar = 1;

    while (continuar) {
        printf("\n1. Depósito\n2. Retirada\n3. Transferencia\n4. Consultar saldo\n5. Salir\nSeleccione una opción: ");

        if (!fgets(linea, sizeof(linea), stdin) || sscanf(linea, "%d", &opcion) != 1) {
            printf("Entrada inválida.\n");
            continue;
        }

        if (opcion == 1 || opcion == 2) {
            printf("Monto: ");
            if (!fgets(linea, sizeof(linea), stdin) || sscanf(linea, "%f", &monto) != 1 || monto <= 0)
                continue;

            DatosOperacion *datos = malloc(sizeof(DatosOperacion));
            datos->numero_cuenta = cuenta_activa;
            datos->monto = monto;
            datos->tipo_operacion = opcion;

            pthread_t hilo;
            pthread_create(&hilo, NULL, ejecutar_operacion, datos);
            pthread_join(hilo, NULL);

        } else if (opcion == 3) {
            printf("Cuenta destino: ");
            if (!fgets(linea, sizeof(linea), stdin) || sscanf(linea, "%d", &cuenta_destino) != 1)
                continue;

            printf("Monto: ");
            if (!fgets(linea, sizeof(linea), stdin) || sscanf(linea, "%f", &monto) != 1 || monto <= 0)
                continue;

            DatosOperacion *datos = malloc(sizeof(DatosOperacion));
            datos->numero_cuenta = cuenta_activa;
            datos->cuenta_destino = cuenta_destino;
            datos->monto = monto;

            pthread_t hilo;
            pthread_create(&hilo, NULL, ejecutar_transferencia, datos);
            pthread_join(hilo, NULL);

        } else if (opcion == 4) {
            consultar_saldo(cuenta_activa);

        } else if (opcion == 5) {
            continuar = 0;
            close(write_fd);

        } else {
            printf("Opción no válida.\n");
        }
    }

    desconectar_memoria(tabla);
    return 0;
}
