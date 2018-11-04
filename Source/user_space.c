#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_LENGTH 256

int main()
{
    char filePath[BUFFER_LENGTH];

    FILE *fd = fopen("/dev/hidefile", "w");
    if (fd == NULL)
    {
        perror("Failed to open the device...");
        return errno;
    }
    printf("Enter path of the file you want to hide:\n");
    scanf("%[^\n]%*c", filePath); // Read in a string (with spaces)
    int res = fprintf(fd, "%s", filePath); // Send the string to the LKM
    if (res < 0)
    {
        perror("Failed to write the message to the device.");
        return errno;
    }

    printf("Your file has been hidden.\n");
    fclose(fd);

    return 0;
}