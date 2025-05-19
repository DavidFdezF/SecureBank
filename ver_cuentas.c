#include <stdio.h>
#include <stdlib.h>
#include "estructuras.h"

int main() {
    FILE *f = fopen("cuentas.dat", "rb");
    if (!f) {
        perror("No se pudo abrir cuentas.dat");
        return 1;
    }

    Cuenta c;
    int i = 0;
    printf("Contenido de cuentas.dat:\n");
    while (fread(&c, sizeof(Cuenta), 1, f) == 1) {
        printf("Cuenta %d: %s | Saldo: %.2f | Transacciones: %d\n",
               c.numero_cuenta, c.titular, c.saldo, c.num_transacciones);
        i++;
    }

    fclose(f);
    printf("Total cuentas encontradas: %d\n", i);
    return 0;
}
