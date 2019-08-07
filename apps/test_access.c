#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
int main(int argc, char * argv[]) {
    printf("\nUsage: %s [test_file]",argv[0]);
    printf("\nFile %s:",argv[1]);
    
    int result;

    result = access( argv[1], F_OK );
    printf("\nAccess result F_OK: %d",result);

    if( result != -1 ) {
        printf("\nTest for existence - SUCCESS!");
    } else {
         printf("\nTest for existence - FAILLURE!");
    }

    result = access( argv[1], X_OK );
    printf("\nAccess result X_OK: %d",result);

    if(  result != -1 ) {
        printf("\nTest for execute or search permission - SUCCESS!");
    } else {
         printf("\nTest for execute or search permission - FAILLURE!");
    }

    result = access( argv[1], W_OK );
    printf("\nAccess result W_OK: %d",result);

    if(  result != -1 ) {
        printf("\nTest for write permission - SUCCESS!");
    } else {
         printf("\nTest for write permission - FAILLURE!");
    }
    

    result = access( argv[1], R_OK );
    printf("\nAccess result R_OK: %d",result);

    if(  result != -1 ) {
        printf("\nTest for read permission - SUCCESS!");
    } else {
         printf("\nTest for read permission - FAILLURE!");
    }
    exit(0);
}

