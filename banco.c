#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <time.h>
#include <pthread.h>

#include "estructuras.h"
#include "memoria.h"
#include "configuracion.h"
#include "entrada_salida.h"
#include "ficheros.h"

#define MAX_USUARIOS 4
#define MSGKEY 1234
#define MSGKEY_RESPUESTA 5678

int main() {
    // Cargamos la configuracion del archivo
    Config cfg = leer_configuracion("config.txt");

    // Creamos y accedenis a la memoria compartida
    int shm_id = crear_memoria_compartida();
    TablaCuentas *tabla = acceder_a_memoria(shm_id);
    cargar_cuentas_a_memoria(tabla, cfg.archivo_cuentas);
    printf("[BANCO] Memoria compartida cargada con %d cuentas.\n", tabla->num_cuentas);

    // Se crean dos colas de mensajes
    int msgid = msgget(MSGKEY, IPC_CREAT | 0666);
    int msgid_respuesta = msgget(MSGKEY_RESPUESTA, IPC_CREAT | 0666);
    if (msgid == -1 || msgid_respuesta == -1) {
        perror("[BANCO] Error creando colas de mensajes");
        exit(1);
    }

    // Lanzamos el proceso monitor
    char shm_id_str[16];
    snprintf(shm_id_str, sizeof(shm_id_str), "%d", shm_id);
    pid_t pid_monitor = fork();
    if (pid_monitor == 0) {
        execlp("./monitor", "monitor", shm_id_str, NULL);
        perror("Error al ejecutar monitor");
        exit(1);
    }

    // Hilo para para las operaciones del archivo cuentas.dat
    pthread_t hilo_escritura;
    inicializar_buffer();
    pthread_create(&hilo_escritura, NULL, gestionar_entrada_salida, cfg.archivo_cuentas);

    int fds[MAX_USUARIOS][2];
    pid_t pids[MAX_USUARIOS];
    int usuarios_activos = 0;

    // En caso de inicio de sesión lanzamos un proceso hijo
    while (usuarios_activos < MAX_USUARIOS) {
        char opcion;
        printf("\n¿Desea iniciar sesión? (s/n): ");
        scanf(" %c", &opcion);

        if (opcion == 's' || opcion == 'S') {
            if (pipe(fds[usuarios_activos]) == -1) {
                perror("[BANCO] Error creando tubería");
                exit(1);
            }

            pid_t pid = fork();
            if (pid == 0) {
                close(fds[usuarios_activos][0]);
                char fd_str[10];
                snprintf(fd_str, sizeof(fd_str), "%d", fds[usuarios_activos][1]);
                execlp("xterm", "xterm", "-e", "./usuario", fd_str, shm_id_str, NULL);
                perror("Error al lanzar usuario");
                exit(1);
            } else {
                close(fds[usuarios_activos][1]);
                pids[usuarios_activos] = pid;
                usuarios_activos++;
            }
        } else if (opcion == 'n' || opcion == 'N') {
            break;
        }
    }

    // Esperamos los mensajes de los procesos hijos (usuarios)
    fd_set readfds;
    int max_fd = 0;
    for (int i = 0; i < usuarios_activos; i++) {
        if (fds[i][0] > max_fd) max_fd = fds[i][0];
    }

    int cerrados[MAX_USUARIOS] = {0};
    int finalizados = 0;

    while (finalizados < usuarios_activos) {
        FD_ZERO(&readfds);
        for (int i = 0; i < usuarios_activos; i++) {
            if (!cerrados[i]) FD_SET(fds[i][0], &readfds);
        }

        if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0) {
            perror("Error en select()");
            break;
        }

        for (int i = 0; i < usuarios_activos; i++) {
            if (!cerrados[i] && FD_ISSET(fds[i][0], &readfds)) {
                char buffer[256];
                int n = read(fds[i][0], buffer, sizeof(buffer) - 1);
                if (n > 0) {
                    buffer[n] = '\0';
                    printf("[BANCO] Usuario %d: %s", i + 1, buffer);

                    if (strstr(buffer, "Consulta") == NULL) {
                        struct msgbuf msg;
                        msg.mtype = 1;
                        strncpy(msg.mtext, buffer, sizeof(msg.mtext) - 1);
                        msg.mtext[sizeof(msg.mtext) - 1] = '\0';
                        msgsnd(msgid, &msg, strlen(msg.mtext) + 1, 0);

                        // Esperamos la respuesta del monitor y lo registramos en el log
                        struct msgbuf respuesta;
                        msgrcv(msgid_respuesta, &respuesta, sizeof(respuesta.mtext), 0, 0);

                        FILE *log = fopen(cfg.archivo_log, "a");
                        if (log) {
                            time_t now = time(NULL);
                            char timestamp[64];
                            strftime(timestamp, sizeof(timestamp), "[%Y-%m-%d %H:%M:%S]", localtime(&now));
                            fprintf(log, "%s %s\n", timestamp, respuesta.mtext);
                            fclose(log);
                        }
                    }
                    // Si un usuario cierra sesión, se limpia la tubería y se actualiza el contador
                } else if (n == 0) {
                    printf("[BANCO] Usuario %d ha cerrado sesión.\n", i + 1);
                    close(fds[i][0]);
                    cerrados[i] = 1;
                    finalizados++;
                }
            }
        }
    }

    // Esperamos a que los usuarios terminen
    for (int i = 0; i < usuarios_activos; i++) {
        waitpid(pids[i], NULL, 0);
    }

    guardar_cuentas_en_fichero(tabla->cuentas, tabla->num_cuentas, cfg.archivo_cuentas);
    msgctl(msgid, IPC_RMID, NULL);
    msgctl(msgid_respuesta, IPC_RMID, NULL);
    eliminar_memoria(shm_id);
    desconectar_memoria(tabla);

    printf("[BANCO] Todos los usuarios han finalizado. Banco cerrado.\n");
    return 0;
}
