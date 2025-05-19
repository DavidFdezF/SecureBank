#ifndef MEMORIA_H
#define MEMORIA_H

#include "estructuras.h"

int crear_memoria_compartida();
TablaCuentas *acceder_a_memoria(int shm_id);
void cargar_cuentas_a_memoria(TablaCuentas *tabla, const char *archivo);
void desconectar_memoria(TablaCuentas *tabla);
void eliminar_memoria(int shm_id);

#endif
