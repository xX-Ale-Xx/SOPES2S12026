#include <stdio.h>

#define PAGE_SIZE 4096      // 4 KB (2^12)
#define PAGE_MASK 0xFFF     // máscara para obtener los 12 bits del desplazamiento (offset)
#define OFFSET_BITS 12      // bits necesarios para el desplazamiento

// Simulación de una tabla de páginas (mapeo de Página -> Marco/Frame)
// en un sistema real esto lo gestiona el Kernel en la estructura task_struct
int page_table[] = {5, 2, 8, 1, 9}; 

void translate(unsigned int logical_addr) {
    // número de página (desplazando los bits a la derecha)
    unsigned int page_number = logical_addr >> OFFSET_BITS;
    
    // desplazamiento u offset (usando la máscara AND) 
    unsigned int offset = logical_addr & PAGE_MASK;

    printf("Direccion Logica: 0x%08X\n", logical_addr);
    printf("-> Numero de Pagina: %u\n", page_number);
    printf("-> Desplazamiento (Offset): 0x%X\n", offset);

    // busqueda en la tabla de páginas
    if (page_number < (sizeof(page_table) / sizeof(int))) {
        int frame_number = page_table[page_number];
        
        // dirección física: (Frame * Tamaño) + Offset
        unsigned int physical_addr = (frame_number << OFFSET_BITS) | offset;
        
        printf("=> Direccion Fisica: 0x%08X (En el Marco %d)\n", physical_addr, frame_number);
    } else {
        // Fallo de pagina: la página no está cargada en RAM
        // en un sistema real el Kernel manejaría este fallo, cargando la página desde el disco a RAM y actualizando la tabla de páginas
        printf("=> ERROR: Fallo de Pagina (Pagina no cargada en RAM)\n");
    }
    printf("-------------------------------------------\n");
}

int main() {
    printf("Simulador de Paginacion - Sistemas Operativos 2\n\n");

    // Ejemplo 1: Dirección dentro del rango
    translate(0x00001005); // Página 1, Offset 5

    // Ejemplo 2: Dirección en otra página
    translate(0x00002FFFF); // Página 2, Offset FFF
    
    // Ejemplo 3: Dirección en otra página
    translate(0x00003ABC); // Página 3, Offset ABC

    // Ejemplo 4: Dirección en la última página válida
    translate(0x00004000); // Página 4, Offset FFF
    
    // Ejemplo 5: Dirección fuera de la tabla (Simula Fallo de Página)
    translate(0x0000A000); 

    return 0;
}