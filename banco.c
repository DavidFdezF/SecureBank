#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <semaphore.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <time.h>
#include "estructuras.h" 

#define MAX_USUARIOS 4
#define SEM_NOMBRE "/sem_validacion"
#define MSGKEY 1234
#define MSGKEY_RESPUESTA 5678  

// Leemos la configuración del sistema desde el archivo config.txt
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


int main() {
    // Cargamos los parámetros de configuración
    Config cfg = leer_configuracion("config.txt");

    // Mostramos por consola la configuración cargada
    printf("Configuración cargada:\n");
    printf("LIMITE_RETIRO: %d\n", cfg.limite_retiro);
    printf("LIMITE_TRANSFERENCIA: %d\n", cfg.limite_transferencia);
    printf("UMBRAL_RETIROS: %d\n", cfg.umbral_retiros);
    printf("UMBRAL_TRANSFERENCIAS: %d\n", cfg.umbral_transferencias);
    printf("NUM_HILOS: %d\n", cfg.num_hilos);
    printf("ARCHIVO_CUENTAS: %s\n", cfg.archivo_cuentas);
    printf("ARCHIVO_LOG: %s\n", cfg.archivo_log);

    // Creamos la cola de mensajes para el monitor
    int msgid = msgget(MSGKEY, IPC_CREAT | 0666);
    if (msgid == -1) {
        perror("Error al crear la cola de mensajes");
        return 1;
    }
    // Creamos la cola de respuestas para el monitor
    int msgid_respuesta = msgget(MSGKEY_RESPUESTA, IPC_CREAT | 0666);
    if (msgid_respuesta == -1) {
        perror("Banco: No se pudo acceder a la cola de respuestas");
        return 1;
    }

    // Creamos un semáforo POSIX para el acceso a cuentas.dat
    sem_t *sem = sem_open(SEM_NOMBRE, O_CREAT, 0644, 1);
    if (sem == SEM_FAILED) {
        perror("Banco: No se pudo crear el semáforo");
        return 1;
    }
    sem_close(sem); 

    // Lanzamos el monitor en paralelo
    pid_t pid_monitor = fork();
    if (pid_monitor == 0) {
        execlp("./monitor", "monitor", NULL);
        perror("Error al ejecutar monitor");
        exit(1);
    }

    // Creamos tuberías para usuarios
    int fds[MAX_USUARIOS][2]; 
    pid_t pids[MAX_USUARIOS];

    for (int i = 0; i < MAX_USUARIOS; i++) {
        if (pipe(fds[i]) == -1) {
            perror("Error al crear la tubería");
            return 1;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("Error en fork");
            return 1;
        }

        if (pid == 0) {
            // Proceso usuario
            close(fds[i][0]); // Cierra la lectura

            char write_fd_str[10];
            snprintf(write_fd_str, sizeof(write_fd_str), "%d", fds[i][1]);

            execlp("xterm", "xterm", "-e", "./usuario", write_fd_str, NULL); // Lanzamos a usuario con terminal xterm
            perror("Error al lanzar usuario");
            exit(1);
        } else {
            // Proceso padre
            close(fds[i][1]); // Cierra la escritura
            pids[i] = pid;
        }
    }

    // Leemeos todas las tuberías 
    fd_set readfds;
    int max_fd = 0;
    for (int i = 0; i < MAX_USUARIOS; i++) {
        if (fds[i][0] > max_fd) max_fd = fds[i][0];
    }

    int hijos_terminados = 0;
    int hijos_finalizados[MAX_USUARIOS] = {0};

    // Esperamos mensajes desde los usuarios hasta que todos cierren sesión
    while (hijos_terminados < MAX_USUARIOS) {
        FD_ZERO(&readfds);
        for (int i = 0; i < MAX_USUARIOS; i++) {
            if (!hijos_finalizados[i]) {
                FD_SET(fds[i][0], &readfds);
            }
        }

        int activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select");
            break;
        }

        for (int i = 0; i < MAX_USUARIOS; i++) {
            if (FD_ISSET(fds[i][0], &readfds)) {
                char buffer[300];
                int n = read(fds[i][0], buffer, sizeof(buffer) - 1);

                if (n > 0) {
                    buffer[n] = '\0';

                    // Procesamos cada línea de mensaje del usuario
                    char *line = strtok(buffer, "\n");
                    while (line != NULL) {
                        printf("Banco recibió de usuario %d:\n%s\n", i + 1, line);

                        if (strstr(line, "Consulta de saldo") == NULL) {
                            // Enviamos al monitor la operación si es que no es una consulta
                            struct msgbuf m;
                            m.mtype = 1;
                            strncpy(m.mtext, line, sizeof(m.mtext) - 1);
                            m.mtext[sizeof(m.mtext) - 1] = '\0';
                            msgsnd(msgid, &m, strlen(m.mtext) + 1, 0);
                        
                            // Esperamos la respuesta del monitor
                            struct msgbuf respuesta;
                            msgrcv(msgid_respuesta, &respuesta, sizeof(respuesta.mtext), 0, 0);
                        
                            // Obtenemos la fecha y hora
                            time_t now = time(NULL);
                            struct tm *t = localtime(&now);
                            char timestamp[32];
                            strftime(timestamp, sizeof(timestamp), "[%Y-%m-%d %H:%M:%S]", t);
                        
                            // Escribimos en el log la operación
                            FILE *f = fopen(cfg.archivo_log, "a");
                            if (f) {
                                if (strncmp(respuesta.mtext, "ALERTA", 6) == 0) {
                                    fprintf(f, "%s %s\n", timestamp, respuesta.mtext);
                                } else {
                                    // Identificamos el tipo de operación
                                    char tipo[20];
                                    char signo;
                                    if (strstr(line, "Depósito")) {
                                        strcpy(tipo, "Depósito");
                                        signo = '+';
                                    } else if (strstr(line, "Retirada")) {
                                        strcpy(tipo, "Retiro");
                                        signo = '-';
                                    } else if (strstr(line, "Transferencia")) {
                                        strcpy(tipo, "Transferencia");
                                        signo = '>';  
                                    } else {
                                        strcpy(tipo, "Transacción");
                                        signo = '?';
                                    } 
                                    // Escribimos en el log
                                    int cuenta_origen;
                                    float monto;
                                    if (strstr(tipo, "Transferencia")) {
                                        if (sscanf(line, "Transferencia de %f desde cuenta %d", &monto, &cuenta_origen) == 2) {
                                            char mensaje_log[256];
                                            snprintf(mensaje_log, sizeof(mensaje_log), "%s %s desde cuenta %d: %c%.2f",
                                                    timestamp, tipo, cuenta_origen, signo, monto);
                                            fprintf(f, "%s\n", mensaje_log);
                                        } else {
                                            fprintf(f, "%s %s\n", timestamp, line);  
                                        }
                                    } else {
                                        int cuenta;
                                        if (sscanf(line, "%*[^0123456789]%f%*[^0123456789]%d", &monto, &cuenta) == 2) {
                                            char mensaje_log[256];
                                            snprintf(mensaje_log, sizeof(mensaje_log), "%s %s en cuenta %d: %c%.2f",
                                                    timestamp, tipo, cuenta, signo, monto);
                                            fprintf(f, "%s\n", mensaje_log);
                                        } else {
                                            fprintf(f, "%s %s\n", timestamp, line);
                                        }
                                    }
                                }
                                fclose(f);
                            } else {
                                perror("Error al abrir archivo de log");
                            }
                        }                                                   
                        line = strtok(NULL, "\n");
                    }
                } else if (n == 0) {
                    // Cerramos la tubería si el usuario finaliza
                    printf("Usuario %d ha cerrado su sesión.\n", i + 1);
                    close(fds[i][0]);
                    hijos_finalizados[i] = 1;
                    hijos_terminados++;
                }
            }
        }
    }

    // Esperamos a que los usuarios terminen
    for (int i = 0; i < MAX_USUARIOS; i++) {
        waitpid(pids[i], NULL, 0);
    }

    // Limpiamos los recursos
    sem_unlink(SEM_NOMBRE);
    msgctl(msgid, IPC_RMID, NULL);
    printf("Todos los usuarios han cerrado sesión. Banco finalizado.\n");

    return 0;
}