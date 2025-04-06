#ifndef ESTRUCTURAS_H
#define ESTRUCTURAS_H

// Definimos constantes de uso general en el sistema
#define ARCHIVO_LOG "transacciones.log"     
#define CONFIG_PATH "config.txt"            
#define PIPE_ALERTA "/tmp/pipe_alerta"      

// Definimos la estructura para los mensajes que enviamos por colas de mensajes
struct msgbuf {
    long mtype;               
    char mtext[256];          
};

// Definimos estructura para encapsular datos de la operación bancaria
typedef struct {
    int numero_cuenta;       
    int cuenta_destino;      
    float monto;
    int tipo_operacion;      
    int write_fd;
} DatosOperacion;


// Definimos la estructura principal que representa una cuenta bancaria
typedef struct {
    int numero_cuenta;             
    char titular[50];             
    float saldo;                  
    int num_transacciones;        
} Cuenta;

// Estructura alternativa de configuración más detallada (usada en procesos principales)
typedef struct {
    int limite_retiro;               
    int limite_transferencia;       
    int umbral_retiros;            
    int umbral_transferencias;      
    int num_hilos;                  
    char archivo_cuentas[100];      
    char archivo_log[100];          
} Config;

#endif
