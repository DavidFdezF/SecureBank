#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "estructuras.h"

#define ARCHIVO_CUENTAS "cuentas.dat"

int main() {
    FILE *archivo = fopen(ARCHIVO_CUENTAS, "wb");
    if (!archivo) {
        perror("No se pudo crear cuentas.dat");
        return 1;
    }

    Cuenta cuentas[4];

    cuentas[0].numero_cuenta = 1001;
    strcpy(cuentas[0].titular, "David Fernandez");
    cuentas[0].saldo = 3000.0;
    cuentas[0].bloqueado = 0;
    cuentas[0].num_transacciones = 0;

    cuentas[1].numero_cuenta = 1002;
    strcpy(cuentas[1].titular, "Ignacio Perez");
    cuentas[1].saldo = 1500.0;
    cuentas[1].bloqueado = 0;
    cuentas[1].num_transacciones = 0;

    cuentas[2].numero_cuenta = 1003;
    strcpy(cuentas[2].titular, "Diego Rivera");
    cuentas[2].saldo = 1000.0;
    cuentas[2].bloqueado = 0;
    cuentas[2].num_transacciones = 0;

    cuentas[3].numero_cuenta = 1004;
    strcpy(cuentas[3].titular, "Guillermo Rodriguez");
    cuentas[3].saldo = 4000.0;
    cuentas[3].bloqueado = 0;
    cuentas[3].num_transacciones = 0;

    fwrite(cuentas, sizeof(Cuenta), 4, archivo);
    fclose(archivo);

    printf("Archivo cuentas.dat creado correctamente con 4 cuentas.\n");
    return 0;
}
