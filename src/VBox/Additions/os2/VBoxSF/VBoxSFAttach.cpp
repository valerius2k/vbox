#define  OS2EMX_PLAIN_CHAR
#define  INCL_DOSFILEMGR
#define  INCL_DOSERRORS
#include <os2.h>

#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    APIRET rc;

    if (argc == 1)
    {
        printf("attach [d: [<sharename> | /delete]]\n\n");
        return 0;
    }

    if (argc == 3 && !stricmp(argv[2], "/delete"))
        rc = DosFSAttach(argv[1], "VBOXSF", NULL, 0, FS_DETACH);
    else
    {
        if (argc == 2)
            rc = DosFSAttach(argv[1], "VBOXSF", NULL, 0, FS_ATTACH);
        else
            rc = DosFSAttach(argv[1], "VBOXSF", argv[2], strlen(argv[2]) + 1, FS_ATTACH);
    }

    switch (rc)
    {
      case ERROR_INVALID_FSD_NAME:
             puts ("Error: IFS=VBOXSF.IFS not loaded in CONFIG.SYS");
             return 1;

      case ERROR_INVALID_PATH:
             puts ("Error: Invalid drive letter");
             return 1;

      case ERROR_ALREADY_ASSIGNED:
             puts ("Error: This drive letter is already in use");
             return 1;
 
      case NO_ERROR:
             puts ("OK");
             return 0;

      default:
             puts ("DosFSAttach failed");
             return 1;
    }

    return 0;
}
