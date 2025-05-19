#ifndef ESTRUCTURAS_H
#define ESTRUCTURAS_H

#include <pthread.h>

// -------------------- ESTRUCTURA DE CUENTA --------------------

typedef struct {
    int numero_cuenta;
    char titular[50];
    float saldo;
    int bloqueado;              // 0 = activa, 1 = bloqueada
    int num_transacciones;
    pthread_mutex_t mutex;      // Protección por cuenta
} Cuenta;

// -------------------- TABLA GLOBAL DE CUENTAS --------------------

typedef struct {
    Cuenta cuentas[100];        // Hasta 100 cuentas en el sistema
    int num_cuentas;
} TablaCuentas;

// -------------------- DATOS PARA OPERACIÓN DEL USUARIO --------------------

typedef struct {
    int numero_cuenta;          // Cuenta origen
    int cuenta_destino;         // Para transferencias
    float monto;
    int tipo_operacion;         // 1 = depósito, 2 = retiro, 3 = transferencia
    int write_fd;               // Descriptor de escritura hacia banco (pipes)
} DatosOperacion;

// -------------------- CONFIGURACIÓN DESDE ARCHIVO --------------------

typedef struct {
    int limite_retiro;
    int limite_transferencia;
    int umbral_retiros;
    int umbral_transferencias;
    int num_hilos;
    char archivo_cuentas[100];
    char archivo_log[100];
} Config;

// -------------------- COLA DE MENSAJES --------------------

struct msgbuf {
    long mtype;
    char mtext[256];
};

#endif
