#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <limits.h>
#include <openssl/md5.h>
#include "boss.h"

#define BUF_MAX 4096

void myMD5(unsigned char* string, long n, char* strMD5);

int load_config_file(struct server_config *config, char *path)
{
    /* TODO finish your own config file parser */
    // open config file
    FILE* f;
    f = fopen(path, "r");
    if (f == NULL) {
        perror("load_config_file");
        exit(1);
    }
    char buf[PATH_MAX];
    int line = 0;
    config->pipes = malloc(sizeof(struct pipe_pair));  //assume at least one MINER

    // start get config file lines
    while(fgets(buf, PATH_MAX, f) != NULL) {
        char* pch;
        // store MINE path
        if (line == 0) {
            pch = strtok(buf, " ");
            pch = strtok(NULL, "\n");
            config->mine_file = malloc(strlen(pch)+1);
            strcpy(config->mine_file, pch);
        }
        // store MINERs pipes path
        else {
            // append one pipe_pair
            struct pipe_pair* tmp = realloc(config->pipes, line * sizeof(struct pipe_pair));
            if (tmp) {
                config->pipes = tmp;
            }
            else {
                perror("realloc config->pipes");
            }
            pch = strtok(buf, " ");   // ignore MINER:
            pch = strtok(NULL, " ");   // input_pipe
            // printf("%s\n", pch);
            config->pipes[line-1].input_pipe = malloc(strlen(pch)+1);
            strcpy(config->pipes[line-1].input_pipe, pch);
            pch = strtok(NULL, "\n");   // output_pipe
            // printf("%s\n", pch);  
            config->pipes[line-1].output_pipe = malloc(strlen(pch)+1);
            strcpy(config->pipes[line-1].output_pipe, pch);          
        }

        line++;
    }
    config->num_miners = line - 1; //number of miners

    //debug
    // printf("mine path: %s\n", config->mine_file);
    // printf("miner num: %d\n", config->num_miners);
    // for (int i = 0; i < config->num_miners; i++) {
    //     printf("miner%d: in: %s, out: %s\n", i, 
    //         config->pipes[i].input_pipe, config->pipes[i].output_pipe);
    // }

    fclose(f);
    return 0;
}

int assign_jobs(struct server_config config, struct fd_pair client_fds[], 
    char* client_names[], unsigned char mine[], int mine_size, int t)
{
    /* TODO design your own (1) communication protocol (2) job assignment algorithm */
    for (int i = 0; i < config.num_miners; i++)
    {
        /* send mine string to miners */
        // int mine_size = strlen(mine) + 1;
        write(client_fds[i].input_fd, &mine_size, sizeof(int));
        write(client_fds[i].input_fd, mine, mine_size);
        // printf("boss send mine: %s to miner %d, size = %d\n", mine, i, mine_size);

        // send t
        write(client_fds[i].input_fd, &t, sizeof(int));

        // send which part does a miner calculates
        // from: 0, to: 127 (represents 0x00 ~ 0x7f)
        int from, to;
        from = i * (256 / config.num_miners);
        if (i == config.num_miners - 1)
            to = 255;
        else
            to = (i+1) * (256/config.num_miners) - 1;
        write(client_fds[i].input_fd, &from, sizeof(int));
        write(client_fds[i].input_fd, &to, sizeof(int));

        // read miner's name
        int name_size;
        char name_buf[256] = {0};
        read(client_fds[i].output_fd, &name_size, sizeof(int));
        read(client_fds[i].output_fd, name_buf, name_size);
        // printf("miner %d your name is %s, len = %d\n", i, name_buf, strlen(name_buf));

        // printf("%s you're in charge of %d ~ %d\n", name_buf, from, to);
        client_names[i] = malloc(name_size);
        memset(client_names[i], 0, sizeof(client_names[i]));
        strcpy(client_names[i], name_buf);
    }

    return 1;
}

int dump(char dump_path[], unsigned char* cur_best_treasure, int size,
         int cur_best_t, char cur_best_md5[], struct fd_pair* client_fds[], int miner_num)
{
    // printf("dump %d bytes treasure: %s to %s\n", size, cur_best_treasure, dump_path);

    /* initialize data for select() */
    fd_set readset, working_readset;
    fd_set writeset, working_writeset;
    struct timeval timeout;
    struct timeval working_timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    FD_ZERO(&readset);
    FD_ZERO(&writeset);

    int dump_fd = open(dump_path, O_WRONLY | O_CREAT, 0664);
    assert(dump_fd >= 0);

    // if no treasure found, only create file
    if (size == 0)
    {
        return 1;
    }

    FD_SET(STDIN_FILENO, &readset);
    FD_SET(dump_fd, &writeset);

    int written_bytes = 0;
    // int q = 0;
    while (1)
    {
        memcpy(&working_readset, &readset, sizeof(readset));
        memcpy(&working_writeset, &writeset, sizeof(writeset));
        working_timeout = timeout;

        select(dump_fd+1, &working_readset, &working_writeset, NULL, &working_timeout);

        if (FD_ISSET(dump_fd, &working_writeset))
        {
            /* write treasure to dump_path */
            int w = write(dump_fd, (cur_best_treasure + written_bytes), size);
            written_bytes += w;

            // finished dump process
            if (written_bytes == size)
            {
                // if (q == 1)
                //     return 0;
                // else
                //     return 1;
                return 1;
            }
        }
        if (FD_ISSET(STDIN_FILENO, &working_readset))
        {
            /* recieve stdin command */
            char cmd[8] = {0};
            scanf("%s", cmd);
            // printf("recieve stdin command: %s\n", cmd);

            if (strcmp(cmd, "dump") == 0)
            {
                char dump_path2[PATH_MAX] = {0};
                scanf("%s", dump_path2);

                // ignore dump on same file
                if (strcmp(dump_path, dump_path2) == 0)
                    continue;

                dump(dump_path2, cur_best_treasure, size,
                     cur_best_t, cur_best_md5, client_fds, miner_num);
                // if (d == 0)
                    // return 0;
            }

            else if (strcmp(cmd, "status") == 0)
            {
                /* TODO show status */
                if (cur_best_t == 0)
                    printf("best 0 treasure in 0 bytes\n");
                else
                    printf("best %d-treasure %s in %d bytes\n", cur_best_t, cur_best_md5, size);

                // send cmd to client
                for (int i = 0; i < miner_num; i++)
                {
                    int c = 2;
                    write(client_fds[i]->input_fd, &c, sizeof(int));
                }
            }

            else if (strcmp(cmd, "quit") == 0)
            {
                // normally quit
                for (int i = 0; i < miner_num; i++)
                {
                    int c = 3;
                    write(client_fds[i]->input_fd, &c, sizeof(int));
                    close(client_fds[i]->input_fd);                    
                    close(client_fds[i]->output_fd);
                }
                exit(0);
            }
        }

    }

}

int main(int argc, char **argv)
{
    /* sanity check on arguments */
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s CONFIG_FILE\n", argv[0]);
        exit(1);
    }

    /* load config file */
    struct server_config config;
    load_config_file(&config, argv[1]);

    /* open the named pipes */
    struct fd_pair client_fds[config.num_miners];
    // store client's names
    char* client_names[config.num_miners];

    for (int ind = 0; ind < config.num_miners; ind += 1)
    {
        struct fd_pair *fd_ptr = &client_fds[ind];
        struct pipe_pair *pipe_ptr = &config.pipes[ind];

        // fprintf(stderr, "open miner %d input pipe: %s\n", ind, pipe_ptr->input_pipe);
        fd_ptr->input_fd = open(pipe_ptr->input_pipe, O_WRONLY);
        assert (fd_ptr->input_fd >= 0);

        // fprintf(stderr, "open miner %d output pipe: %s\n", ind, pipe_ptr->output_pipe);
        fd_ptr->output_fd = open(pipe_ptr->output_pipe, O_RDONLY);
        // fd_ptr->output_fd = open(pipe_ptr->input_pipe, O_RDONLY); origin code. WRONG??
        assert (fd_ptr->output_fd >= 0);
    }

    // debug: output input and output files and fds:
    // for (int i = 0; i < config.num_miners; i++)
    // {
    //     printf("input file: %s fd: %d, ", config.pipes->input_pipe, client_fds[i].input_fd);
    //     printf("output file: %s fd: %d\n", config.pipes->output_pipe, client_fds[i].output_fd);
    // }

    /* initialize data for select() */
    int maxfd = -1;
    fd_set readset;
    fd_set working_readset;

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    struct timeval working_timeout;

    FD_ZERO(&readset);
    FD_SET(STDIN_FILENO, &readset);

    // TODO add input pipes to readset, setup maxfd
    for (int i = 0; i < config.num_miners; i++)
    {
        FD_SET(client_fds[i].output_fd, &readset);
        if (client_fds[i].output_fd > maxfd)
            maxfd = client_fds[i].output_fd;
    }

    /* assign jobs to clients */
    /* open mine path and get mine */    
    unsigned char mine[BUF_MAX] = {0};   // store mine
    // printf("open mine path: %s\n", config.mine_file);

    int m = open(config.mine_file, O_RDONLY);
    assert(m >= 0);
    int b = read(m, mine, BUF_MAX);
    assert(b >= 0);
    // calculate initial t-treasure
    char ini_md5[33] = {0};
    myMD5(mine, b, ini_md5);
    int t = 1;
    for (int i = 0; i < 33; i++) {
        if (ini_md5[i] == '0')
            t++;
        else
            break;
    }

    assign_jobs(config, client_fds, client_names, mine, b, t);

    // debug clients' names
    // for (int i = 0; i < config.num_miners; i++)
    //     printf("client %d: %s\n", i, client_names[i]);

    /* start listening miners */
    // for status
    int cur_best_t = 0;
    char cur_best_md5[33] = {0};
    // for dump
    unsigned char* cbt = NULL;
    unsigned char* cur_best_treasure = NULL;
    int cur_best_treasure_size = 0;

    if (t > 1) {
        cur_best_t = t-1;
        strcpy(cur_best_md5, ini_md5);
        // cur_best_treasure = mine;
        cur_best_treasure = malloc(sizeof(unsigned char) * b);
        memcpy(cur_best_treasure, mine, b);
        cur_best_treasure_size = b;
    }

    while (1)
    {
        memcpy(&working_readset, &readset, sizeof(readset)); // why we need memcpy() here?
        // memcpy(&working_timeout, &timeout, sizeof(timeout));
        int s = select(maxfd+1, &working_readset, NULL, NULL, &timeout);
        // printf("select %d fds\n", s);

        if (FD_ISSET(STDIN_FILENO, &working_readset))
        {
            // TODO handle user input here 
            char cmd[8] = {0};
            // fgets(cmd, sizeof(cmd), stdin);
            scanf("%s", cmd);
            // printf("recieve stdin command: %s\n", cmd);

            if (strcmp(cmd, "dump") == 0)
            {
                char dump_path[PATH_MAX] = {0};
                scanf("%s", dump_path);
                dump(dump_path, cur_best_treasure, cur_best_treasure_size, 
                    cur_best_t, cur_best_md5, &client_fds, config.num_miners);
            }

            else if (strcmp(cmd, "status") == 0)
            {
                /* TODO show status */
                if (cur_best_t == 0)
                    printf("best 0 treasure in 0 bytes\n");
                else
                    printf("best %d-treasure %s in %d bytes\n", cur_best_t, cur_best_md5, cur_best_treasure_size);

                // send cmd to client
                for (int i = 0; i < config.num_miners; i++)
                {
                    int c = 2;
                    write(client_fds[i].input_fd, &c, sizeof(int));
                }
            }

            else if (strcmp(cmd, "quit") == 0)
            {
                // normally quit
                for (int i = 0; i < config.num_miners; i++)
                {
                    int c = 3;
                    write(client_fds[i].input_fd, &c, sizeof(int));
                    // close file descriptors
                    close(client_fds[i].input_fd);
                    close(client_fds[i].output_fd);
                }
                break;
            }

            else
            {
                fprintf(stderr, "incorrect command: \n", cmd);
            }
        }

        // TODO check if any client send me some message
        
        // handle multiple miners found treasure
        // if treasure has found by a miner, other miners with bigger index
        // don't notify the news of discovering
        int treasure_found = 0;

        for (int i = 0; i < config.num_miners; i++)
        {
            if (FD_ISSET(client_fds[i].output_fd, &working_readset))
            {
                // printf("%s's output pipe is readable\n", client_names[i]);
                /* TODO handle client message */
                /* if anyone found treasure */

                // recieve treasure-found confirm message,
                // c = 1 means someone found a treasure
                int c = 0;
                int b = read(client_fds[i].output_fd, &c, sizeof(int));
                // printf("recieve cmd: %d, read %d bytes\n", c, b);

                if (c != 1) {
                    fprintf(stderr, "recieve abnormal confirm message from %s\n", client_names[i]);
                    exit(1);
                }

                // found_treasure[i] = 1;

                int t, name_size, s;
                char trea_MD5[33] = {0};
                char name[256] = {0};
                read(client_fds[i].output_fd, &t, sizeof(int));
                read(client_fds[i].output_fd, trea_MD5, sizeof(trea_MD5)); // read treasure's MD5
                read(client_fds[i].output_fd, &name_size, sizeof(int)); // read the finder's name
                read(client_fds[i].output_fd, name, name_size);

                // read the treasure string
                read(client_fds[i].output_fd, &s, sizeof(int));
                unsigned char* treasure = malloc(sizeof(unsigned char) * s);
                read(client_fds[i].output_fd, treasure, s);

                if (treasure_found)
                    continue;

                printf("A %d-treasure discovered! %s\n", t, trea_MD5);

                // prepare data for status and dump
                cur_best_t = t;
                strcpy(cur_best_md5, trea_MD5);
                cur_best_treasure_size = s;
                cbt = realloc(cur_best_treasure, sizeof(unsigned char) * s);
                if (cbt != NULL) {
                    cur_best_treasure = cbt;
                    memcpy(cur_best_treasure, treasure, s);
                }
                else {
                    perror("memory reallocation error.");
                    exit(1);
                }
                // printf("current best treasure: %s, size = %d\n", cur_best_treasure, s);

                /* notify all other miners */
                for (int j = 0; j < config.num_miners; j++)
                {
                    if (strcmp(client_names[j], name) == 0) {
                        // printf("you don't have to!!!!!\n");
                        continue;
                    }

                    int cmd = 1, n_size = strlen(name)+1;
                    write(client_fds[j].input_fd, &cmd, sizeof(int));   // write command = 1
                    write(client_fds[j].input_fd, &n_size, sizeof(int));  // write name size
                    write(client_fds[j].input_fd, name, n_size);  // write name of treasure finder
                    write(client_fds[j].input_fd, &t, sizeof(int));  // write num of zeros
                    write(client_fds[j].input_fd, trea_MD5, 33);  // write the treasure MD5

                    // send the treasure string to other miners
                    write(client_fds[j].input_fd, &s, sizeof(int));
                    write(client_fds[j].input_fd, treasure, s);
                }

                // other miners don't have to notify others
                treasure_found = 1;

                free(treasure);
                // break;
            }
        }
    }

    // free memory
    for (int i = 0; i < config.num_miners; i++)
        free(client_names[i]);
    free(config.mine_file);
    free(config.pipes);
    free(cur_best_treasure);
    return 0;
}

/* calculate MD5 of an unsigned char array, and store it at strMD5 */
void myMD5(unsigned char* string, long n, char* strMD5)
{
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5(string, n, digest);
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++)
    {
        char buf[32]; 
        sprintf(buf, "%02x", digest[i]);
        strcat(strMD5, buf);
    }
}