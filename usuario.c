#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include "estructuras.h"

#define SEM_NOMBRE "/sem_validacion"

sem_t *sem;
char archivo_cuentas[100];
DatosOperacion *datos_global;  

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

// Consultamos el saldo de una cuenta 
int consultar_cuenta(int numero_cuenta, char *mensaje_final, size_t mensaje_size) {
    FILE *archivo = fopen(archivo_cuentas, "rb");
    if (!archivo) {
        perror("Error al abrir archivo de cuentas");
        return 0;
    }

    Cuenta cuenta;
    int encontrado = 0;

    // Buscamos la cuenta en el archivo binario
    while (fread(&cuenta, sizeof(Cuenta), 1, archivo)) {
        if (cuenta.numero_cuenta == numero_cuenta) {
            encontrado = 1;
            snprintf(mensaje_final, mensaje_size,
                     "Consulta de saldo:\nCuenta: %d\nTitular: %s\nSaldo: %.2f\nTransacciones: %d",
                     cuenta.numero_cuenta, cuenta.titular, cuenta.saldo, cuenta.num_transacciones);
            break;
        }
    }

    fclose(archivo);
    return encontrado;
}

// Buscamos una cuenta y actualizamos su saldo según la operación indicada
int buscar_y_actualizar_cuenta(int numero_cuenta, float monto, int operacion, char *mensaje_final, size_t mensaje_size) {
    FILE *archivo = fopen(archivo_cuentas, "rb+");
    if (!archivo) {
        perror("Error al abrir archivo de cuentas");
        return 0;
    }

    Cuenta cuenta;
    int encontrado = 0;
    // Recorremos las cuentas hasta encontrar la deseada
    while (fread(&cuenta, sizeof(Cuenta), 1, archivo)) {
        if (cuenta.numero_cuenta == numero_cuenta) {
            encontrado = 1;

            // Aplicamos la operación solicitada
            if (operacion == 1) {  // Depósito
                cuenta.saldo += monto;
                snprintf(mensaje_final, mensaje_size, "Depósito de %.2f dinero en cuenta %d (Titular: %s)",
                         monto, cuenta.numero_cuenta, cuenta.titular);
            } else if (operacion == 2) {  // Retiro
                if (cuenta.saldo < monto) {
                    snprintf(mensaje_final, mensaje_size, "Saldo insuficiente en cuenta %d (Titular: %s)",
                             cuenta.numero_cuenta, cuenta.titular);
                    fclose(archivo);
                    return 0;
                }
                cuenta.saldo -= monto;
                snprintf(mensaje_final, mensaje_size, "Retirada de %.2f dinero en cuenta %d (Titular: %s)",
                         monto, cuenta.numero_cuenta, cuenta.titular);
            } else if (operacion == 3) {  // Transferencia
                long pos_origen = ftell(archivo);
                if (cuenta.saldo < monto) {
                    snprintf(mensaje_final, mensaje_size, "Transferencia fallida: saldo insuficiente en cuenta %d (Titular: %s)",
                             cuenta.numero_cuenta, cuenta.titular);
                    fclose(archivo);
                    return 0;
                }
                if (numero_cuenta == datos_global->cuenta_destino) {
                    snprintf(mensaje_final, mensaje_size, "Transferencia inválida: la cuenta origen y destino son la misma.");
                    fclose(archivo);
                    return 0;
                }
                // Retiramos el dinero de la cuenta origen
                cuenta.saldo -= monto;
                cuenta.num_transacciones++;
                fseek(archivo, -sizeof(Cuenta), SEEK_CUR);
                fwrite(&cuenta, sizeof(Cuenta), 1, archivo);

                FILE *archivo_destino = fopen(archivo_cuentas, "rb+");
                if (!archivo_destino) {
                    perror("Error al abrir archivo de destino");
                    return 0;
                }
                // Buscamos la cuenta destino
                Cuenta cuenta_destino;
                int encontrado_destino = 0;
                while (fread(&cuenta_destino, sizeof(Cuenta), 1, archivo_destino)) {
                    if (cuenta_destino.numero_cuenta == datos_global->cuenta_destino) {
                        encontrado_destino = 1;
                        cuenta_destino.saldo += monto;
                        cuenta_destino.num_transacciones++;
                        fseek(archivo_destino, -sizeof(Cuenta), SEEK_CUR);
                        fwrite(&cuenta_destino, sizeof(Cuenta), 1, archivo_destino);
                        break;
                    }
                }

                fclose(archivo_destino);
                // Si no encontramos la cuenta destino, revertimos el retiro
                if (!encontrado_destino) {
                    fseek(archivo, pos_origen, SEEK_SET);
                    fread(&cuenta, sizeof(Cuenta), 1, archivo);
                    cuenta.saldo += monto;
                    cuenta.num_transacciones--;
                    fseek(archivo, -sizeof(Cuenta), SEEK_CUR);
                    fwrite(&cuenta, sizeof(Cuenta), 1, archivo);
                    snprintf(mensaje_final, mensaje_size, "Transferencia fallida: cuenta destino no encontrada.");
                    fclose(archivo);
                    return 0;
                }

                snprintf(mensaje_final, mensaje_size,
                         "Transferencia de %.2f desde cuenta %d a cuenta %d (Titular origen: %s)",
                         monto, cuenta.numero_cuenta, datos_global->cuenta_destino, cuenta.titular);
            }

            if (operacion != 3) {
                cuenta.num_transacciones++;
            }
            fseek(archivo, -sizeof(Cuenta), SEEK_CUR);
            fwrite(&cuenta, sizeof(Cuenta), 1, archivo);
            break;
        }
    }

    fclose(archivo);
    return encontrado;
}

// Ejecutamos una operación bancaria dentro de un hilo
void *ejecutar_operacion(void *arg) {
    DatosOperacion *datos = (DatosOperacion *)arg;
    datos_global = datos;

    char mensaje[256];
    sem_wait(sem);

    if (buscar_y_actualizar_cuenta(datos->numero_cuenta, datos->monto, datos->tipo_operacion, mensaje, sizeof(mensaje))) {
        write(datos->write_fd, mensaje, strlen(mensaje)); // Enviamos mensaje al banco
        write(datos->write_fd, "\n", 1);
        printf("\n%s\n", mensaje); // Mostramos resultado en pantalla
    } else {
        printf("Cuenta no encontrada o error en operación.\n");
    }

    sem_post(sem);
    free(datos);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    Config cfg = leer_configuracion("config.txt");
    strcpy(archivo_cuentas, cfg.archivo_cuentas);

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <fd_escritura>\n", argv[0]);
        return 1;
    }

    int write_fd = atoi(argv[1]); // Descriptor de escritura de la tubería
    char linea[100];
    int opcionmenu = 1;
    int opcion;
    int cuenta_numero, cuenta_numero2;
    float monto;
    char mensaje[256];

    sem = sem_open(SEM_NOMBRE, 0);
    if (sem == SEM_FAILED) {
        perror("Error al abrir semáforo");
        return 1;
    }

    while (opcionmenu == 1) {

        printf("\n");
        printf("1. Depósito\n");
        printf("2. Retirada\n");
        printf("3. Transferencia\n");
        printf("4. Consultar saldo\n");
        printf("5. Salir\n");
        printf("Seleccione una opción: ");

        if (!fgets(linea, sizeof(linea), stdin) || sscanf(linea, "%d", &opcion) != 1) {
            printf("Entrada inválida.\n");
            continue;
        }

        // Procesamos depósito o retiro
        if (opcion == 1 || opcion == 2) {
            printf("Número de cuenta: ");
            if (!fgets(linea, sizeof(linea), stdin) || sscanf(linea, "%d", &cuenta_numero) != 1) {
                printf("Número de cuenta inválido.\n");
                continue;
            }

            printf("Monto: ");
            if (!fgets(linea, sizeof(linea), stdin) || sscanf(linea, "%f", &monto) != 1 || monto <= 0) {
                printf("Monto inválido.\n");
                continue;
            }

            DatosOperacion *datos = malloc(sizeof(DatosOperacion));
            datos->numero_cuenta = cuenta_numero;
            datos->monto = monto;
            datos->tipo_operacion = opcion;
            datos->write_fd = write_fd;

            pthread_t hilo;
            pthread_create(&hilo, NULL, ejecutar_operacion, datos);
            pthread_join(hilo, NULL);

        // Procesamos transferencia
        } else if (opcion == 3) {
            printf("Número de cuenta de origen: ");
            if (!fgets(linea, sizeof(linea), stdin) || sscanf(linea, "%d", &cuenta_numero) != 1) {
                printf("Número de cuenta inválido.\n");
                continue;
            }

            printf("Número de cuenta de destino: ");
            if (!fgets(linea, sizeof(linea), stdin) || sscanf(linea, "%d", &cuenta_numero2) != 1) {
                printf("Número de cuenta inválido.\n");
                continue;
            }

            printf("Monto: ");
            if (!fgets(linea, sizeof(linea), stdin) || sscanf(linea, "%f", &monto) != 1 || monto <= 0) {
                printf("Monto inválido.\n");
                continue;
            }

            DatosOperacion *datos = malloc(sizeof(DatosOperacion));
            datos->numero_cuenta = cuenta_numero;
            datos->cuenta_destino = cuenta_numero2;
            datos->monto = monto;
            datos->tipo_operacion = 3;  
            datos->write_fd = write_fd;

            pthread_t hilo;
            pthread_create(&hilo, NULL, ejecutar_operacion, datos);
            pthread_join(hilo, NULL);

        // Consultamos saldo
        } else if (opcion == 4) {
            printf("Número de cuenta: ");
            if (!fgets(linea, sizeof(linea), stdin) || sscanf(linea, "%d", &cuenta_numero) != 1) {
                printf("Número de cuenta inválido.\n");
                continue;
            }

            if (consultar_cuenta(cuenta_numero, mensaje, sizeof(mensaje))) {
                printf("\n%s\n", mensaje);
            } else {
                printf("Cuenta no encontrada.\n");
            }

        // Salimos del menú
        } else if (opcion == 5) {
            printf("Saliendo\n");
            opcionmenu = 0;
            close(write_fd);
            sem_close(sem);
        } else {
            printf("Opción no válida.\n");
        }
    }

    return 0;
}
