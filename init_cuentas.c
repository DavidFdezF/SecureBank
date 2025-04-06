#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "estructuras.h"

#define ARCHIVO_CUENTAS "cuentas.dat"  // Definimos el nombre del archivo binario de cuentas

int main() {
    // Abrimos el archivo cuentas.dat en modo escritura binaria (creamos o sobrescribimos)
    FILE *archivo = fopen(ARCHIVO_CUENTAS, "wb");
    if (!archivo) {
        perror("No se pudo crear cuentas.dat");
        return 1;
    }

    // Definimos un arreglo de cuentas bancarias iniciales con saldo y datos básicos
    Cuenta cuentas[] = {
        {1001, "David Fernandez",     3000.0, 0},
        {1002, "Ignacio Perez",       1500.0, 0},
        {1003, "Diego Rivera",        1000.0, 0},
        {1004, "Guillermo Rodriguez",  4000.0, 0}
    };

    // Calculamos el número total de cuentas a escribir
    size_t num = sizeof(cuentas) / sizeof(Cuenta);

    // Escribimos las cuentas en el archivo binario
    fwrite(cuentas, sizeof(Cuenta), num, archivo);

    // Informamos al usuario que el archivo fue creado correctamente
    printf("Archivo cuentas.dat creado con 4 cuentas iniciales.\n");

    // Cerramos el archivo
    fclose(archivo);
    return 0;
}
