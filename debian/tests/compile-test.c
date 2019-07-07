#include "mupdf/fitz.h"
#include "mupdf/pdf.h"
    
#include <stdlib.h>
#include <stdio.h>
    
int main(int argc, char **argv)
{
     fz_context *ctx;
    
     ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
     if (!ctx)
     {
          fprintf(stderr, "cannot initialise context\n");
          exit(1);
     }
    
     return 0;
}
