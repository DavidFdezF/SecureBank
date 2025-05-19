#ifndef FICHEROS_H
#define FICHEROS_H

#include "estructuras.h"

void crear_directorio_usuario(int numero_cuenta);
void escribir_log_usuario(int numero_cuenta, const char *mensaje);
void guardar_cuentas_en_fichero(Cuenta *cuentas, int num_cuentas, const char *ruta_archivo);

#endif
