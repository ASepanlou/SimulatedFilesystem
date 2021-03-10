#include <stdio.h>
#include "simfstypes.h"

/* File system operations */
void printfs(char *);
void initfs(char *);
int createfile(char *, char *);
int writefile(char *, char *, char *, char *);
int readfile(char *, char *, char *, char *);
int deletefile(char *, char *);

/* Internal functions */
FILE *openfs(char *filename, char *mode);
void closefs(FILE *fp);
int* newNodeCollector(FILE *fp, fentry *files, fnode *nodes, int nodes_needed);
int* existingNodeCollector(FILE *fp, fentry *files, fnode *nodes, int file_index, int nodes_in_file);