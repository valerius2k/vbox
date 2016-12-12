#include <VBox/sup.h>

void *__dummy(unsigned long dummyarg)
{
    SUPR0Printf("*** dummy\n");
    return 0;
}
