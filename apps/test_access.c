#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>


int main(int argc, char * argv[]) {    
    if ( argc > 1 && argc <= 2 ) {
        
        char * filename = argv[1];

        if (access(filename, F_OK) != 0){          
            printf("ERROR %s: %s: %s\n", argv[0], argv[1], strerror(errno));
        }
        else {
            if (access(filename, R_OK) == 0){
                printf("You have read access to '%s'\n", filename);                
            } else {
                printf("ERROR %s: %s: %s\n", argv[0], argv[1], strerror(errno));
            }
            if (access(filename, W_OK) == 0){
                printf("You have write access to '%s'\n", filename);
            } else{
                printf("ERROR %s: %s: %s\n", argv[0], argv[1], strerror(errno));
            }
            if (access(filename, X_OK) == 0){
                printf("You have search access to '%s'\n", filename);
            } else {
                printf("ERROR %s: %s: %s\n", argv[0], argv[1], strerror(errno));
            }
        }
    }else{
        printf("\nUsage: %s [test_file]",argv[0]);
        exit (1);
    }      
    exit(0);  
}

