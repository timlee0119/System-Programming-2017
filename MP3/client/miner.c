#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <limits.h>
#include <openssl/md5.h>

#define BUF_MAX 4096

char* name;
char* input_pipe;
char* output_pipe;

void myMD5(unsigned char* string, long n, char* strMD5);

// int mining_process2(int t, int from, int to, unsigned char mine[], int mine_size, int input_fd, int output_fd);

int mining_process3(int t, int from, int to, unsigned char mine[], int mine_size, int input_fd, int output_fd);

int waiting_commands(char lastMD5[], int input_fd, int output_fd);

int wait_boss_connect_assign(char* input_pipe, char* output_pipe)
{ 
    /* open pipes */
    int input_fd = open(input_pipe, O_RDONLY);
    assert (input_fd >= 0);
    // printf("my input pipe fd is: %d\n", input_fd);

    int output_fd = open(output_pipe, O_WRONLY);
    assert (output_fd >= 0);    
    // printf("my output pipe fd is: %d\n", output_fd);

    printf("BOSS is mindful.\n");

    /* recieve mine string */
    int mine_size = 0;
    unsigned char mine[BUF_MAX] = {0};
    read(input_fd, &mine_size, sizeof(int));
    read(input_fd, mine, mine_size);
    // get t
    int t = 0;
    read(input_fd, &t, sizeof(int));

    // get appending range
    // ex: from = 0 to = 127 => appending "0x00" ~ "0x7f"
    int from, to;
    read(input_fd, &from, sizeof(int));
    read(input_fd, &to, sizeof(int));

    // tell boss your name
    int name_size = strlen(name)+1;
    write(output_fd, &name_size, sizeof(int));
    write(output_fd, name, name_size);
    // printf("send my name: %s, len = %d\n", name, strlen(name));

    // start mining
    /* TODO */
    mining_process3(t, from, to, mine, mine_size, input_fd, output_fd);

    return 1;
}

int main(int argc, char **argv)
{
    /* parse arguments */
    if (argc != 4)
    {
        fprintf(stderr, "Usage: %s CONFIG_FILE\n", argv[0]);
        exit(1);
    }

    // char *name = argv[1];
    name = argv[1];
    input_pipe = argv[2];
    output_pipe = argv[3];

    /* create named pipes */
    int ret;
    ret = mkfifo(input_pipe, 0644);
    assert (ret == 0);
    // printf("create input_pipe: %s\n", input_pipe);

    ret = mkfifo(output_pipe, 0644);
    assert (ret == 0);
    // printf("create output_pipe: %s\n", output_pipe);

    /* TODO write your own (1) communication protocol (2) computation algorithm */
    /* To prevent from blocked read on input_fd, try select() or non-blocking I/O */
    wait_boss_connect_assign(input_pipe, output_pipe);

    return 0;
}

int mining_process3(int t, int from, int to, unsigned char mine[], int mine_size, int input_fd, int output_fd)
{
    // printf("In mining process 2.\n");

    // debug
    // char ini_md5[33] = {0};
    // myMD5(mine, mine_size, ini_md5);
    // printf("working on mine: %s, MD5 = %s, size = %d\n", mine, ini_md5, mine_size);
    // printf("I'm looking for a %d-treasure.\n", t);
    // printf("I'm in charge of %d ~ %d!\n", from, to);


    /* initialize data for select() */
    fd_set readset;
    fd_set working_readset;
    struct timeval timeout;
    struct timeval working_timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    FD_ZERO(&readset);
    FD_SET(input_fd, &readset);

    // last MD5 for status
    char lastMD5[33] = {0};
    for (int ind1 = from; ind1 <= to; ind1++)
    {
        unsigned char ap_char1 = '\x00' + ind1;
        for (int ind2 = 0; ind2 <= 255; ind2++)
        {
            unsigned char ap_char2 = '\x00' + ind2;
            for (int ind3 = 0; ind3 <= 255; ind3++)
            {
                // printf("ind1: %d, ind2: %d\n", ind1, ind2);
                int is_treasure = 1;

                unsigned char ap_char3 = '\x00' + ind3;
                int cur_mine_size = mine_size+3;

                // current mine = mine + apchar1 + apchar2
                unsigned char* cur_mine = malloc(sizeof(unsigned char)*(cur_mine_size));
                memcpy(cur_mine, mine, mine_size);
                cur_mine[mine_size] = ap_char1;
                cur_mine[mine_size + 1] = ap_char2;
                cur_mine[mine_size + 2] = ap_char3;   

                // calculate current mine MD5
                char cur_md5[33] = {0};
                myMD5(cur_mine, cur_mine_size, cur_md5);
                strcpy(lastMD5, cur_md5);

                // printf("current mine: %s, MD5 = %s, size = %d\n", cur_mine, cur_md5, mine_size+2);

                // judge if it is a t-treasure
                for (int i = 0; i < t; i++) {
                    if (cur_md5[i] != '0')
                        is_treasure = 0;
                }
                is_treasure = is_treasure && (cur_md5[t] != '0');

                // listen to boss, if there's any command, do the command
                // otherwise announce finding treasure if (is_treasure == 1)
                memcpy(&working_readset, &readset, sizeof(readset));
                working_timeout = timeout;
                select(input_fd+1, &working_readset, NULL, NULL, &working_timeout);

                /* three situation: */
                if (FD_ISSET(input_fd, &working_readset))
                {
                    int cmd = 0;
                    int b = read(input_fd, &cmd, sizeof(int));
                    // printf("recieve command: %d from boss (by reading %d bytes)\n", cmd, b);

                    /* 1 => any other miner has found a treasure */
                    if (cmd == 1)
                    {
                        int n_size, zeros;
                        char finder_name[256] = {0};
                        char trea_md5[33] = {0};
                        read(input_fd, &n_size, sizeof(int));
                        read(input_fd, finder_name, n_size);
                        read(input_fd, &zeros, sizeof(int));
                        read(input_fd, trea_md5, 33);
                        printf("%s wins a %d-treasure! %s\n", finder_name, zeros, trea_md5);

                        // read the treasure string
                        int t_size;
                        read(input_fd, &t_size, sizeof(int));
                        unsigned char* treasure = malloc(sizeof(unsigned char) * t_size);
                        read(input_fd, treasure, t_size);
                        // printf("read treasure string from boss: %s, size = %d\n", treasure, t_size);

                        // TODO: restart mining process, working on (t+1)-treasure.

                        // if 3-treasure is found, terminate calculating and jump to waiting process
                        if (zeros >= 3)
                        {
                            fprintf(stderr, "starts waiting.\n");
                            waiting_commands(lastMD5, input_fd, output_fd);
                            free(treasure);
                            return 1;
                        }
                        else
                        {
                            fprintf(stderr, "restart mining process 3\n");
                            mining_process3(zeros+1, from, to, treasure, t_size, input_fd, output_fd);
                            free(treasure);
                            return 1;
                        }
                    }
                    /* 2 => boss recieve status from stdin, we have to print 
                    the MD5 hash that was computed in the most recent step. */ 
                    else if (cmd == 2)
                    {
                        /* TODO */
                        printf("I'm working on %s\n", lastMD5);
                    }
                    /* 3 => boss recieve quit from stdin, we have to quit */
                    else if (cmd == 3)
                    {
                        /* TODO */
                        printf("BOSS is at rest.\n");
                        close(input_fd);
                        close(output_fd);

                        sleep(1);
                        wait_boss_connect_assign(input_pipe, output_pipe);
                    }

                    /* recieve abnormal command */
                    else
                    {
                        fprintf(stderr, "abnormal command when listening to boss\n");
                        exit(1);
                    }
                }

                // if there's no command from boss, announce finding treasure if (is_treasure == 1)
                else if (is_treasure)
                {
                    printf("I win a %d-treasure! %s\n", t, cur_md5);
                    int confirm = 1, name_size = strlen(name) + 1;
                    write(output_fd, &confirm, sizeof(int)); //send confirm message
                    write(output_fd, &t, sizeof(int));
                    write(output_fd, cur_md5, sizeof(cur_md5)); // send the treasure's MD5
                    write(output_fd, &name_size, sizeof(int)); //send my name
                    write(output_fd, name, name_size);

                    // send the treasure string
                    int s = cur_mine_size;
                    write(output_fd, &s, sizeof(int));
                    write(output_fd, cur_mine, s);
                    
                    if (t >= 3)
                    {
                        fprintf(stderr, "starts waiting.\n");
                        waiting_commands(lastMD5, input_fd, output_fd);
                        return 1;
                    }
                    else
                    {
                        fprintf(stderr, "restart mining process 3\n");
                        mining_process3(t+1, from, to, cur_mine, s, input_fd, output_fd);
                    }
                    return 1;
                }

                free(cur_mine);
            }
        }
    }

    fprintf(stderr, "Finished mining_process 3, didn't find anything. Start waiting\n");
    // mining_process3(t, from, to, mine, mine_size, input_fd, output_fd);
    waiting_commands(lastMD5, input_fd, output_fd);

    return 1;
}

int waiting_commands(char lastMD5[], int input_fd, int output_fd)
{
    /* initialize data for select() */
    fd_set readset;
    fd_set working_readset;
    struct timeval timeout;
    struct timeval working_timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    FD_ZERO(&readset);
    FD_SET(input_fd, &readset);

    while(1)
    {
        memcpy(&working_readset, &readset, sizeof(readset));
        working_timeout = timeout;
        select(input_fd+1, &working_readset, NULL, NULL, &working_timeout);

        /* three situation: */
        if (FD_ISSET(input_fd, &working_readset))
        {
            int cmd = 0;
            int b = read(input_fd, &cmd, sizeof(int));
            // printf("recieve command: %d from boss (by reading %d bytes)\n", cmd, b);

            /* 1 => any other miner has found a treasure */
            if (cmd == 1)
            {
                int n_size, zeros;
                char finder_name[256] = {0};
                char trea_md5[33] = {0};
                read(input_fd, &n_size, sizeof(int));
                read(input_fd, finder_name, n_size);
                read(input_fd, &zeros, sizeof(int));
                read(input_fd, trea_md5, 33);
                printf("%s wins a %d-treasure! %s\n", finder_name, zeros, trea_md5);

                // read the treasure string
                int t_size;
                read(input_fd, &t_size, sizeof(int));
                unsigned char* treasure = malloc(sizeof(unsigned char) * t_size);
                read(input_fd, treasure, t_size);
                free(treasure);
                // printf("read treasure string from boss: %s, size = %d\n", treasure, t_size);

            }
            /* 2 => boss recieve status from stdin, we have to print 
            the MD5 hash that was computed in the most recent step. */ 
            else if (cmd == 2)
            {
                printf("I'm working on %s\n", lastMD5);
            }
            /* 3 => boss recieve quit from stdin, we have to quit */
            else if (cmd == 3)
            {
                /* TODO */
                printf("BOSS is at rest.\n");
                close(input_fd);
                close(output_fd);

                sleep(1);
                wait_boss_connect_assign(input_pipe, output_pipe);
            }
            /* recieve abnormal command */
            else
            {
                fprintf(stderr, "abnormal command when listening to boss\n");
                exit(1);
            }

        }        
    }

    return 1;
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

// int mining_process2(int t, int from, int to, unsigned char mine[], int mine_size, int input_fd, int output_fd)
// {
//     // printf("In mining process 2.\n");

//     // debug
//     // char ini_md5[33] = {0};
//     // myMD5(mine, mine_size, ini_md5);
//     // printf("working on mine: %s, MD5 = %s, size = %d\n", mine, ini_md5, mine_size);
//     // printf("I'm looking for a %d-treasure.\n", t);
//     // printf("I'm in charge of %d ~ %d!\n", from, to);


//     /* initialize data for select() */
//     fd_set readset;
//     fd_set working_readset;
//     struct timeval timeout;
//     struct timeval working_timeout;
//     timeout.tv_sec = 0;
//     timeout.tv_usec = 100;
//     FD_ZERO(&readset);
//     FD_SET(input_fd, &readset);

//     // last MD5 for status
//     char lastMD5[33] = {0};

//     for (int ind1 = from; ind1 <= to; ind1++)
//     {
//         unsigned char ap_char1 = '\x00' + ind1;
//         for (int ind2 = 0; ind2 <= 255; ind2++)
//         {
//             // printf("ind1: %d, ind2: %d\n", ind1, ind2);
//             int is_treasure = 1;

//             unsigned char ap_char2 = '\x00' + ind2;

//             // current mine = mine + apchar1 + apchar2
//             unsigned char* cur_mine = malloc(sizeof(unsigned char)*(mine_size+2));
//             memcpy(cur_mine, mine, mine_size);
//             cur_mine[mine_size] = ap_char1;
//             cur_mine[mine_size + 1] = ap_char2;

//             // calculate current mine MD5
//             char cur_md5[33] = {0};
//             myMD5(cur_mine, mine_size+2, cur_md5);
//             strcpy(lastMD5, cur_md5);

//             // printf("current mine: %s, MD5 = %s, size = %d\n", cur_mine, cur_md5, mine_size+2);

//             // judge if it is a t-treasure
//             for (int i = 0; i < t; i++) {
//                 if (cur_md5[i] != '0')
//                     is_treasure = 0;
//             }
//             is_treasure = is_treasure && (cur_md5[t] != '0');

//             // listen to boss, if there's any command, do the command
//             // otherwise announce finding treasure if (is_treasure == 1)
//             memcpy(&working_readset, &readset, sizeof(readset));
//             working_timeout = timeout;
//             select(input_fd+1, &working_readset, NULL, NULL, &working_timeout);

//             /* three situation: */
//             if (FD_ISSET(input_fd, &working_readset))
//             {
//                 int cmd = 0;
//                 int b = read(input_fd, &cmd, sizeof(int));
//                 // printf("recieve command: %d from boss (by reading %d bytes)\n", cmd, b);

//                 /* 1 => any other miner has found a treasure */
//                 if (cmd == 1)
//                 {
//                     int n_size, zeros;
//                     char finder_name[256] = {0};
//                     char trea_md5[33] = {0};
//                     read(input_fd, &n_size, sizeof(int));
//                     read(input_fd, finder_name, n_size);
//                     read(input_fd, &zeros, sizeof(int));
//                     read(input_fd, trea_md5, 33);
//                     printf("%s wins a %d-treasure! %s\n", finder_name, zeros, trea_md5);

//                     // read the treasure string
//                     int t_size;
//                     read(input_fd, &t_size, sizeof(int));
//                     unsigned char* treasure = malloc(sizeof(unsigned char) * t_size);
//                     read(input_fd, treasure, t_size);
//                     // printf("read treasure string from boss: %s, size = %d\n", treasure, t_size);

//                     // TODO: restart mining process, working on (t+1)-treasure.

//                      WARNING!!!!!!!!!!!!!!!!!!!!! NOT FREE MALLOC HERE, COULD CAUSE MEMORY LEAK!!!!!!! 
//                     // if 3-treasure is found, terminate calculating and jump to waiting process
//                     // if (zeros >= 3)
//                     // {
//                     //     // printf("starts waiting.\n");
//                     //     waiting_commands(lastMD5, input_fd, output_fd);
//                     //     return 1;
//                     // }
//                     // else
//                     // {
//                         // printf("restart mining_process 2\n");
//                         mining_process2(zeros+1, from, to, treasure, t_size, input_fd, output_fd);
//                         return 1;
//                     // }
//                 }
//                 /* 2 => boss recieve status from stdin, we have to print 
//                 the MD5 hash that was computed in the most recent step. */ 
//                 else if (cmd == 2)
//                 {
//                     /* TODO */
//                     printf("I'm working on %s\n", lastMD5);
//                 }
//                 /* 3 => boss recieve quit from stdin, we have to quit */
//                 else if (cmd == 3)
//                 {
//                     /* TODO */
//                     printf("BOSS is at rest.\n");
//                     close(input_fd);
//                     close(output_fd);

//                     sleep(1);
//                     wait_boss_connect_assign(input_pipe, output_pipe);
//                 }

//                 /* recieve abnormal command */
//                 else
//                 {
//                     fprintf(stderr, "abnormal command when listening to boss\n");
//                     exit(1);
//                 }
//             }

//             // if there's no command from boss, announce finding treasure if (is_treasure == 1)
//             else if (is_treasure)
//             {
//                 printf("I win a %d-treasure! %s\n", t, cur_md5);
//                 int confirm = 1, name_size = strlen(name) + 1;
//                 write(output_fd, &confirm, sizeof(int)); //send confirm message
//                 write(output_fd, &t, sizeof(int));
//                 write(output_fd, cur_md5, sizeof(cur_md5)); // send the treasure's MD5
//                 write(output_fd, &name_size, sizeof(int)); //send my name
//                 write(output_fd, name, name_size);

//                 // send the treasure string
//                 int s = mine_size+2;
//                 write(output_fd, &s, sizeof(int));
//                 write(output_fd, cur_mine, s);

//                 // get confirm message
//                 // int c = 0;
//                 // read(input_fd, &c, sizeof(int));
                
//                 // if (c == 1) {
//                 // if (t >= 3)
//                 // {
//                 //     // printf("starts waiting.\n");
//                 //     waiting_commands(lastMD5, input_fd, output_fd);
//                 //     return 1;
//                 // }
//                 // else
//                 // {
//                     // printf("restart mining_process 2\n");
//                     mining_process2(t+1, from, to, cur_mine, mine_size+2, input_fd, output_fd);
//                     return 1;
//                 // }
//                 // }
//                 // else {
//                 //     fprintf(stderr, "get abnormal confirm message: %d from boss\n");
//                 //     exit(1);
//                 // }
//             }

//             free(cur_mine);
//         }
//     }

//     fprintf(stderr, "finished mining_process 2, didn't find anything. Start process 3\n");
//     // waiting_commands(lastMD5, input_fd, output_fd);
//     mining_process3(t, from, to, mine, mine_size, input_fd, output_fd);

//     return 1;
// }