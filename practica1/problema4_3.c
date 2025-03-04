#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <dlfcn.h>

int main() {
    double numero = 0.0;
    double (*cos_ptr)(double);
    char cmd[50];

    sprintf(cmd, "cat /proc/%d/maps", getpid());
    
    printf("Mapa de memoria antes de dlopen:\n");
    system(cmd);
    printf("--\n");
    printf("Variable / funcion Direccion                  Tamaño\n");
    printf("              main %p\n", main);
    printf("    cos (dinamico) %p\n", cos_ptr);
    printf("--\n");
    
    void *handle = dlopen("libm.so.6", RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "Error al cargar libm: %s\n", dlerror());
        exit(EXIT_FAILURE);
    }

    cos_ptr = dlsym(handle, "cos");
    if (!cos_ptr) {
        fprintf(stderr, "Error al obtener cos: %s\n", dlerror());
        dlclose(handle);
        exit(EXIT_FAILURE);
    }

    double resultado = cos_ptr(numero);

    printf("Mapa de memoria despues de dlopen:\n");
    system(cmd);
    printf("--\n");
    printf("Variable / funcion Direccion                  Tamaño\n");
    printf("              main %p\n", main);
    printf("               cos %p\n", cos_ptr);
    printf("         resultado %p  %12ld\n", &resultado, sizeof(resultado));
    printf("--\n");

    dlclose(handle);
    
    printf("Mapa de memoria después de dlclose:\n");
    system(cmd);
    printf("--\n");
    printf("Variable / funcion Direccion                  Tamaño\n");
    printf("              main %p\n", main);
    printf("               cos %p\n", cos_ptr);
    printf("         resultado %p  %12ld\n", &resultado, sizeof(resultado));
    printf("--\n");
    
    return 0;
}
