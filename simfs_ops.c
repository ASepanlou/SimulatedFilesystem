/* This file contains functions that are not part of the visible "interface".
 * They are essentially helper functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "simfs.h"

/* Internal helper functions first.
 */

FILE *
openfs(char *filename, char *mode)
{
    FILE *fp;
    if((fp = fopen(filename, mode)) == NULL) {
        perror("openfs");
        exit(1);
    }
    return fp;
}

void
closefs(FILE *fp)
{
    if(fclose(fp) != 0) {
        perror("closefs");
        exit(1);
    }
}

/* File system operations: creating, deleting, reading, and writing to files.
 */
int createfile(char* fsname, char* filename){
    FILE *fp = openfs(fsname, "rb+");
    fentry files[MAXFILES];
    fentry new_file;
    if(strlen(filename) > sizeof(new_file.name) - 1){
        fprintf(stderr, "Filename too long\n");
        closefs(fp);
        exit(1);
    }
    if(fread(files, sizeof(fentry), MAXFILES, fp) < MAXFILES){
        fprintf(stderr, "Error reading file entries for createfile\n");
        closefs(fp);
        exit(1);
    }

    strncpy(new_file.name, filename, sizeof(new_file.name));
    new_file.name[sizeof(new_file.name) - 1] = '\0';
    new_file.firstblock = -1;
    new_file.size = 0;
    int empty_found = 0;

    for(int i = 0; i < MAXFILES; i++){
        if (strncmp(files[i].name, filename, strlen(filename)) == 0){
            fprintf(stderr, "File already exists\n");
            closefs(fp);
            exit(1);
        }
        else if(strncmp(files[i].name, "", strlen(files[i].name)) == 0 && empty_found == 0){
            files[i] = new_file;
            empty_found = 1;
        }
    }
    if(empty_found == 0){
        closefs(fp);
        fprintf(stderr, "No empty fentries detected\n");
        return 1;
    }
    rewind(fp);
    if(fwrite(files, sizeof(fentry), MAXFILES, fp) < MAXFILES){
        fprintf(stderr, "Error in write-back of files in createfile\n");
        closefs(fp);
        exit(1);
    }
    return 0;
}

int writefile(char* fsname, char* filename, char* given_offset, char* given_length){
    FILE *fp = openfs(fsname, "rb+");
    fentry files[MAXFILES];
    fnode nodes[MAXBLOCKS];
    char *offset_error;
    char *length_error;

    long given_offset_long = strtol(given_offset, &offset_error, 10);
    long given_length_long = strtol(given_length, &length_error, 10);
    
    if(given_offset_long == __LONG_MAX__ || strlen(offset_error) != 0 || given_offset_long < 0){
        fprintf(stderr, "Not a valid offset\n");
        closefs(fp);
        exit(1);
    }
    if(given_length_long == __LONG_MAX__ || strlen(length_error) != 0 || given_length_long < 0){
        fprintf(stderr, "Not a valid length\n");
        closefs(fp);
        exit(1);
    }
    if(fread(files, sizeof(fentry), MAXFILES, fp) < MAXFILES){
        fprintf(stderr, "Error reading file entries for writefile\n");
        closefs(fp);
        exit(1);
    }

    if(fread(nodes, sizeof(fnode), MAXBLOCKS, fp) < MAXBLOCKS){
        fprintf(stderr, "Error reading file nodes for writefile\n");
        closefs(fp);
        exit(1);
    }
    char data[given_length_long];
    if(fread(data, 1, given_length_long, stdin) < given_length_long){
        fprintf(stderr, "Error reading data from standard input\n");
        closefs(fp);
        exit(1);
    }
    for(int i = 0; i < MAXFILES; i++){
        if (strncmp(files[i].name, filename, strlen(filename)) == 0){
            if(files[i].size < given_offset_long){
                fprintf(stderr, "Given offset is larger than file size\n");
                closefs(fp);
                exit(1);
            }
            if(files[i].firstblock == -1 && given_offset_long == 0){
                int nodes_needed = given_length_long/BLOCKSIZE;
                if(given_length_long > 0 && given_length_long%BLOCKSIZE != 0){
                    nodes_needed += 1;
                }
                int *available_nodes;
                available_nodes = newNodeCollector(fp, files, nodes, nodes_needed);
                files[i].firstblock = available_nodes[0];
                fseek(fp, BLOCKSIZE * available_nodes[0], SEEK_SET);
                if(nodes_needed == 1){
                    if(fwrite(data, 1, given_length_long, fp) < given_length_long){
                        fprintf(stderr, "Error writing data to file\n");
                        closefs(fp);
                        free(available_nodes);
                        exit(1);
                    }
                }
                else{
                    if(fwrite(data, 1, BLOCKSIZE, fp) < BLOCKSIZE){
                        fprintf(stderr, "Error writing data to file\n");
                        closefs(fp);
                        free(available_nodes);
                        exit(1);
                    }
                    for(int i = 1; i < nodes_needed; i++){
                        nodes[available_nodes[i-1]].nextblock = available_nodes[i];
                        fseek(fp, BLOCKSIZE * available_nodes[i], SEEK_SET);
                        if(i != nodes_needed - 1){
                            if(fwrite(&data[BLOCKSIZE * i], 1, BLOCKSIZE, fp) < BLOCKSIZE){
                                fprintf(stderr, "Error writing data to file\n");
                                closefs(fp);
                                free(available_nodes);
                                exit(1);
                            }
                        }
                        else{
                            if(fwrite(&data[BLOCKSIZE * i], 1, given_length_long - BLOCKSIZE * i, fp) < given_length_long - BLOCKSIZE * i){
                                fprintf(stderr, "Error writing data to file\n");
                                closefs(fp);
                                free(available_nodes);
                                exit(1);
                            }
                        }
                    }
                }
                files[i].size += given_length_long;
                free(available_nodes);
            }
            else if(files[i].firstblock != -1 && files[i].size >= given_offset_long){
                int nodes_in_file = files[i].size/BLOCKSIZE;
                if(files[i].size > 0 && files[i].size % BLOCKSIZE != 0){
                    nodes_in_file += 1;
                }
                int curr_node = given_offset_long / BLOCKSIZE;
                int *file_nodes = existingNodeCollector(fp, files, nodes, i, nodes_in_file);
                int bytes_written = BLOCKSIZE - (given_offset_long % BLOCKSIZE);
                if(given_offset_long != files[i].size){
                    fseek(fp, BLOCKSIZE * file_nodes[curr_node], SEEK_SET);
                    fseek(fp, given_offset_long % BLOCKSIZE, SEEK_CUR);
                }
                if(given_offset_long + given_length_long <= files[i].size){
                    if(given_length_long <= bytes_written){
                        if(fwrite(data, 1, given_length_long, fp) != given_length_long){
                            fprintf(stderr, "Error writing data to file\n");
                            closefs(fp);
                            free(file_nodes);
                            exit(1);
                        }
                        free(file_nodes);
                    }
                    else{
                        int remainder_of_write = given_length_long;
                        if(fwrite(data, 1, bytes_written, fp) != bytes_written){
                            fprintf(stderr, "Error writing data to file\n");
                            closefs(fp);
                            free(file_nodes);
                            exit(1);
                        }
                        remainder_of_write -= bytes_written;
                        curr_node += 1;
                        fseek(fp, BLOCKSIZE * file_nodes[curr_node], SEEK_SET);
                        while(remainder_of_write > BLOCKSIZE){
                            if(fwrite(&data[bytes_written], 1, BLOCKSIZE, fp) != BLOCKSIZE){
                            fprintf(stderr, "Error writing data to file\n");
                            closefs(fp);
                            free(file_nodes);
                            exit(1);
                            }
                            curr_node += 1;
                            remainder_of_write -= BLOCKSIZE;
                            bytes_written += BLOCKSIZE;
                            fseek(fp, BLOCKSIZE * file_nodes[curr_node], SEEK_SET);
                        }
                        if(fwrite(&data[bytes_written], 1, remainder_of_write, fp) != remainder_of_write){
                            fprintf(stderr, "Error writing data to file\n");
                            closefs(fp);
                            free(file_nodes);
                            exit(1);
                        }
                        free(file_nodes);
                    }
                }
                else if (given_offset_long + given_length_long > files[i].size){
                    int current_total_file_size = nodes_in_file * BLOCKSIZE;
                    int overflow_bytes = (given_offset_long + given_length_long) - files[i].size;
                    int remainder_of_write = given_length_long;
                    if(given_offset_long + given_length_long <= current_total_file_size){
                        if(remainder_of_write <= bytes_written){
                            if(fwrite(data, 1, remainder_of_write, fp) != remainder_of_write){
                                fprintf(stderr, "Error writing data to file\n");
                                closefs(fp);
                                free(file_nodes);
                                exit(1);
                            }
                            files[i].size += overflow_bytes;
                        }
                        else{
                           if(fwrite(data, 1, bytes_written, fp) != bytes_written){
                                fprintf(stderr, "Error writing data to file\n");
                                closefs(fp);
                                free(file_nodes);
                                exit(1);
                            }
                            remainder_of_write -= bytes_written;
                            for(int i = curr_node + 1; i < nodes_in_file; i++){
                                fseek(fp, BLOCKSIZE * file_nodes[i], SEEK_SET);
                                if(remainder_of_write > BLOCKSIZE){
                                    if(fwrite(&data[bytes_written], 1, BLOCKSIZE, fp) != BLOCKSIZE){
                                        fprintf(stderr, "Error writing data to file\n");
                                        closefs(fp);
                                        free(file_nodes);
                                        exit(1);
                                    }
                                    remainder_of_write -= BLOCKSIZE;
                                    bytes_written += BLOCKSIZE;
                                }
                                else{
                                    if(fwrite(&data[bytes_written], 1, remainder_of_write, fp) != remainder_of_write){
                                        fprintf(stderr, "Error writing data to file\n");
                                        closefs(fp);
                                        free(file_nodes);
                                        exit(1);
                                    }
                                }
                            }
                            files[i].size += overflow_bytes;
                        }
                    }
                    else{
                        int nodes_needed = overflow_bytes / BLOCKSIZE;
                        if(overflow_bytes > 0 && overflow_bytes % BLOCKSIZE != 0){
                            nodes_needed += 1;
                        }
                        int *available_nodes = newNodeCollector(fp, files, nodes, nodes_needed);
                        if(bytes_written == BLOCKSIZE){
                            bytes_written -= BLOCKSIZE;
                        }
                        if(fwrite(data, 1, bytes_written, fp) != bytes_written){
                            fprintf(stderr, "Error writing data to file 1\n");
                            closefs(fp);
                            free(file_nodes);
                            free(available_nodes);
                            exit(1);
                        }
                        remainder_of_write -= bytes_written;
                        curr_node++;
                        while(curr_node < nodes_in_file){
                            fseek(fp, BLOCKSIZE * file_nodes[i], SEEK_SET);
                            if(fwrite(&data[bytes_written], 1, BLOCKSIZE, fp) != BLOCKSIZE){
                                fprintf(stderr, "Error writing data to file 2\n");
                                closefs(fp);
                                free(file_nodes);
                                free(available_nodes);
                                exit(1);
                            }
                            remainder_of_write -= BLOCKSIZE;
                            bytes_written += BLOCKSIZE;
                            curr_node++;
                        }
                        nodes[file_nodes[nodes_in_file - 1]].nextblock = available_nodes[0];
                        fseek(fp, BLOCKSIZE * available_nodes[0], SEEK_SET);
                        if(remainder_of_write <= BLOCKSIZE){
                            if(fwrite(&data[bytes_written], 1, remainder_of_write, fp) != remainder_of_write){
                                fprintf(stderr, "Error writing data to file 3\n");
                                closefs(fp);
                                free(file_nodes);
                                free(available_nodes);
                                exit(1);
                            }
                        }
                        else{
                            if(fwrite(&data[bytes_written], 1, BLOCKSIZE, fp) != BLOCKSIZE){
                                fprintf(stderr, "Error writing data to file 4\n");
                                closefs(fp);
                                free(file_nodes);
                                free(available_nodes);
                                exit(1);
                            }
                            remainder_of_write -= BLOCKSIZE;
                            bytes_written += BLOCKSIZE;
                        }
                        
                        for(int i = 1; i < nodes_needed; i++){
                            nodes[available_nodes[i - 1]].nextblock = nodes[available_nodes[i]].blockindex;
                            fseek(fp, BLOCKSIZE * available_nodes[i], SEEK_SET);
                            if(remainder_of_write > BLOCKSIZE){
                                if(fwrite(&data[bytes_written], 1, BLOCKSIZE, fp) != BLOCKSIZE){
                                    fprintf(stderr, "Error writing data to file 5\n");
                                    closefs(fp);
                                    free(file_nodes);
                                    free(available_nodes);
                                    exit(1);
                                }
                                remainder_of_write -= BLOCKSIZE;
                                bytes_written += BLOCKSIZE;
                            }
                            else{
                                if(fwrite(&data[bytes_written], 1, remainder_of_write, fp) != remainder_of_write){
                                    fprintf(stderr, "Error writing data to file 6\n");
                                    closefs(fp);
                                    free(file_nodes);
                                    free(available_nodes);
                                    exit(1);
                                }
                            }
                        }
                        files[i].size += overflow_bytes;
                        free(file_nodes);
                        free(available_nodes);
                    }
                }
            }
            rewind(fp);
            if(fwrite(files, sizeof(fentry), MAXFILES, fp) < MAXFILES){
                fprintf(stderr, "Error in write-back of files in createfile\n");
                closefs(fp);
                exit(1);
            }
            if(fwrite(nodes, sizeof(fnode), MAXBLOCKS, fp) < MAXBLOCKS){
                fprintf(stderr, "Error in write-back of nodes in createfile\n");
                closefs(fp);
                exit(1);
            }
            closefs(fp);
            return 0;
        }
    }
    fprintf(stderr, "Filename provided does not exist\n");
    closefs(fp);
    return 1;
}

int readfile(char* fsname, char* filename, char* given_offset, char* given_length){
    FILE *fp = openfs(fsname, "rb+");
    fentry files[MAXFILES];
    fnode nodes[MAXBLOCKS];
    char *offset_error;
    char *length_error;

    long given_offset_long = strtol(given_offset, &offset_error, 10);
    long given_length_long = strtol(given_length, &length_error, 10);
    
    if(given_offset_long == __LONG_MAX__ || strlen(offset_error) != 0 || given_offset_long < 0){
        fprintf(stderr, "Not a valid offset\n");
        closefs(fp);
        exit(1);
    }
    if(given_length_long == __LONG_MAX__ || strlen(length_error) != 0 || given_length_long < 0){
        fprintf(stderr, "Not a valid length\n");
        closefs(fp);
        exit(1);
    }
    if(fread(files, sizeof(fentry), MAXFILES, fp) < MAXFILES){
        fprintf(stderr, "Error reading file entries for writefile\n");
        closefs(fp);
        exit(1);
    }

    if(fread(nodes, sizeof(fnode), MAXBLOCKS, fp) < MAXBLOCKS){
        fprintf(stderr, "Error reading file nodes for writefile\n");
        closefs(fp);
        exit(1);
    }
    for(int i = 0; i < MAXFILES; i++){
        if (strncmp(files[i].name, filename, strlen(filename)) == 0){
            if(files[i].size <= given_offset_long){
                fprintf(stderr, "Given offset is larger than or equal to file size\n");
                closefs(fp);
                exit(1);
            }
            if(files[i].size < given_length_long + given_offset_long){
                fprintf(stderr, "Given offset and length combination is larger than file size\n");
                closefs(fp);
                exit(1);
            }
            char data[files[i].size];
            int nodes_in_file = files[i].size/BLOCKSIZE;
            if(files[i].size > 0 && files[i].size % BLOCKSIZE != 0){
                nodes_in_file += 1;
            }
            int *file_nodes = existingNodeCollector(fp, files, nodes, i, nodes_in_file);
            int bytes_read = 0;
            int remainder_of_read = files[i].size;
            for(int i = 0; i < nodes_in_file; i++){
                if(i != nodes_in_file - 1){
                    if(fread(&data[bytes_read], 1, BLOCKSIZE, fp) != BLOCKSIZE){
                        fprintf(stderr, "Error reading file contents\n");
                        closefs(fp);
                        free(file_nodes);
                        exit(1);
                    }
                    bytes_read += BLOCKSIZE;
                    remainder_of_read -= BLOCKSIZE;
                }
                else{
                    if(fread(&data[bytes_read], 1, remainder_of_read, fp) != remainder_of_read){
                        fprintf(stderr, "Error reading file contents\n");
                        closefs(fp);
                        free(file_nodes);
                        exit(1);
                    }
                }
            }
            if(fwrite(&data[given_offset_long], 1, given_length_long, stdout) != given_length_long){
                fprintf(stderr, "Error writing file contents to stdout\n");
                closefs(fp);
                free(file_nodes);
                exit(1);
            }
            closefs(fp);
            free(file_nodes);
            return 0;
        }
    }
    fprintf(stderr, "This file does not exist\n");
    closefs(fp);
    return 1;
}

int deletefile(char* fsname, char* filename){
    FILE *fp = openfs(fsname, "rb+");
    fentry files[MAXFILES];
    fnode nodes[MAXBLOCKS];

    if(fread(files, sizeof(fentry), MAXFILES, fp) < MAXFILES){
        fprintf(stderr, "Error reading file entries for deletefile\n");
        closefs(fp);
        exit(1);
    }

    if(fread(nodes, sizeof(fnode), MAXBLOCKS, fp) < MAXBLOCKS){
        fprintf(stderr, "Error reading file nodes for deletefile\n");
        closefs(fp);
        exit(1);
    }

    char bin_z[BLOCKSIZE] = {0};
    for(int i = 0; i < MAXFILES; i++){
        if (strncmp(files[i].name, filename, strlen(filename)) == 0){
            if(files[i].size == 0 && files[i].firstblock == -1){
                strncpy(files[i].name, bin_z, strlen(files[i].name));
                rewind(fp);
                if(fwrite(files, sizeof(fentry), MAXFILES, fp) < MAXFILES){
                    fprintf(stderr, "Error in write-back of files in createfile\n");
                    closefs(fp);
                    exit(1);
                }
                if(fwrite(nodes, sizeof(fnode), MAXBLOCKS, fp) < MAXBLOCKS){
                    fprintf(stderr, "Error in write-back of nodes in createfile\n");
                    closefs(fp);
                    exit(1);
                }
                closefs(fp);
                return 0;

            }
            else{
                int nodes_in_file = files[i].size / BLOCKSIZE;
                if(files[i].size > 0 && files[i].size % BLOCKSIZE != 0){
                    nodes_in_file += 1;
                }
                int *file_nodes = existingNodeCollector(fp, files, nodes, i, nodes_in_file);
                for(int j = nodes_in_file - 1; j >= 0; j--){
                    nodes[file_nodes[j]].blockindex = nodes[file_nodes[j]].blockindex * -1;
                    nodes[file_nodes[j]].nextblock = -1;
                    fseek(fp, BLOCKSIZE * file_nodes[j], SEEK_SET);
                    if(j == nodes_in_file - 1){
                        int remainder = files[i].size - BLOCKSIZE * (nodes_in_file - 1);
                        if(fwrite(bin_z, sizeof(char), remainder, fp) != remainder){
                            fprintf(stderr, "Error overwriting file content\n");
                            closefs(fp);
                            free(file_nodes);
                            exit(1);
                        }
                    }
                    else{
                        if(fwrite(bin_z, sizeof(char), BLOCKSIZE, fp) != BLOCKSIZE){
                            fprintf(stderr, "Error overwriting file content\n");
                            closefs(fp);
                            free(file_nodes);
                            exit(1);
                        }
                    }
                }
                strncpy(files[i].name, bin_z, strlen(files[i].name));
                files[i].size = 0;
                files[i].firstblock = -1;
                rewind(fp);
                if(fwrite(files, sizeof(fentry), MAXFILES, fp) < MAXFILES){
                    fprintf(stderr, "Error in write-back of files in createfile\n");
                    closefs(fp);
                    exit(1);
                }
                if(fwrite(nodes, sizeof(fnode), MAXBLOCKS, fp) < MAXBLOCKS){
                    fprintf(stderr, "Error in write-back of nodes in createfile\n");
                    closefs(fp);
                    exit(1);
                }
                closefs(fp);
                free(file_nodes);
                return 0;
            }
        }
    }
    fprintf(stderr, "No such file exists\n");
    closefs(fp);
    exit(1);
}
// Signatures omitted; design as you wish.

int* newNodeCollector(FILE *fp, fentry *files, fnode *nodes, int nodes_needed){
    int *collectedNodes = malloc(sizeof(int) * nodes_needed);
    for(int i = 0; i < nodes_needed; i++){
        int nodeFound = 0;
        for(int j = 0; j < MAXBLOCKS; j++){
            if(nodes[j].blockindex < 0){
                collectedNodes[i] = nodes[j].blockindex * -1;
                nodes[j].blockindex = nodes[j].blockindex * -1;
                nodeFound = 1;
                break;
            }
        }
        if(nodeFound == 0){
            fprintf(stderr, "Not enough unused nodes to write data\n");
            closefs(fp);
            free(collectedNodes);
            exit(1);
        }
    }
    return collectedNodes;
}

int* existingNodeCollector(FILE *fp, fentry *files, fnode *nodes, int file_index, int nodes_in_file){
    int curr_node = files[file_index].firstblock;
    int *collectedNodes = malloc(sizeof(int) * nodes_in_file);
    for(int i = 0; i < nodes_in_file; i++){
        collectedNodes[i] = nodes[curr_node].blockindex;
        curr_node = nodes[curr_node].nextblock;
    }
    return collectedNodes;
}