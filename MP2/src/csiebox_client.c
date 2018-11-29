#include "csiebox_client.h"

#include "csiebox_common.h"
#include "connect.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


#define MAX_FILE_NAME_LENGTH 101
#define MAX_FILE_DATA_SIZE 4001

static int parse_arg(csiebox_client* client, int argc, char** argv);
static int login(csiebox_client* client);
//traverse dir tree and return the longest path name
int traverse_longestPath(csiebox_client* client, char* dir, int depth);
// int traverse_dir_tree(csiebox_client* client, char* dir, int depth);
int traverse_dir_tree(csiebox_client* client, char* dir);

//read config file, and connect to server
void csiebox_client_init(
  csiebox_client** client, int argc, char** argv) {
  csiebox_client* tmp = (csiebox_client*)malloc(sizeof(csiebox_client));
  if (!tmp) {
    fprintf(stderr, "client malloc fail\n");
    return;
  }
  memset(tmp, 0, sizeof(csiebox_client));
  if (!parse_arg(tmp, argc, argv)) {
    fprintf(stderr, "Usage: %s [config file]\n", argv[0]);
    free(tmp);
    return;
  }
  int fd = client_start(tmp->arg.name, tmp->arg.server);
  if (fd < 0) {
    fprintf(stderr, "connect fail\n");
    free(tmp);
    return;
  }
  tmp->conn_fd = fd;
  //initialize longest_path_name, hlink_inodes and hlink_to
  memset(tmp->arg.longest_path_name, '\0', sizeof(tmp->arg.longest_path_name));

  tmp->arg.hlink_to = NULL;
  tmp->arg.hlink_to = (char**)malloc(300 * sizeof(char*));
  for (int i = 0; i < 300; i++) {
    tmp->arg.hlink_to[i] = NULL;
    tmp->arg.hlink_inodes[i] = -1;
  }

  *client = tmp;
}

//show how to use csiebox_protocol_meta header
//other headers is similar usage
//please check out include/common.h
//using .gitignore for example only for convenience  
int sampleFunction(csiebox_client* client){
  csiebox_protocol_meta req;
  memset(&req, 0, sizeof(req));
  req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
  req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
  req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
  char path[] = "just/a/testing/path";
  req.message.body.pathlen = strlen(path);

  //just show how to use these function
  //Since there is no file at "just/a/testing/path"
  //I use ".gitignore" to replace with
  //In fact, it should be 
  //lstat(path, &req.message.body.stat);
  //md5_file(path, req.message.body.hash);
  lstat(".gitignore", &req.message.body.stat);
  md5_file(".gitignore", req.message.body.hash);


  //send pathlen to server so that server can know how many charachers it should receive
  //Please go to check the samplefunction in server
  if (!send_message(client->conn_fd, &req, sizeof(req))) {
    fprintf(stderr, "send fail\n");
    return 0;
  }

  //send path
  send_message(client->conn_fd, path, strlen(path));

  //receive csiebox_protocol_header from server
  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  if (recv_message(client->conn_fd, &header, sizeof(header))) {
    if (header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
        header.res.op == CSIEBOX_PROTOCOL_OP_SYNC_META &&
        header.res.status == CSIEBOX_PROTOCOL_STATUS_OK) {
      printf("Receive OK from server\n");
      return 1;
    } else {
      return 0;
    }
  }
  return 0;
}

//this is where client sends request, you sould write your code here
int csiebox_client_run(csiebox_client* client) {
  if (!login(client)) {
    fprintf(stderr, "login fail\n");
    return 0;
  }
  fprintf(stderr, "login success\n");
  

  //This is a sample function showing how to send data using defined header in common.h
  //You can remove it after you understand

  // sampleFunction(client);
  
  //====================
  //        TODO
  //====================
  
  //traverse client directory (and determine the longest path name)
  chdir(client->arg.path);
  client->arg.max_depth = 0;
  //traverse for the longestPath first
  if (traverse_longestPath(client, ".", 0) == 0) {
    fprintf(stderr, "traverse longestPath fail\n");
  }

  //traverse for sync files
  if (traverse_dir_tree(client, ".") == 0){
    fprintf(stderr, "traverse fail\n");
    return 0;
  }

  chdir("../bin");
  //free hlink_to
  for (int i = 0; i < 300; i++) {
    if (client->arg.hlink_to[i] != NULL)
      free(client->arg.hlink_to[i]);
  }
  free(client->arg.hlink_to);

  return 1;
}

//traverse directories for finding longestPath
int traverse_longestPath(csiebox_client* client, char* dir, int depth) {
  DIR* dp;
  struct dirent* entry;
  struct stat statbuf;

  // printf("%s\n", dir);
  if ((dp = opendir(dir)) == NULL) {
    fprintf(stderr, "open dir: %s fail in longestPath traverse\n", dir);
    // fprintf(stderr, "opendir fail in longestPath traverse\n");
    return 0;
  }
  while ((entry = readdir(dp)) != NULL) {
    if (strcmp(".", entry->d_name) == 0 || 
        strcmp("..", entry->d_name) == 0) 
      continue;

    char file_path[400] = {'\0'};
    sprintf(file_path, "%s/%s", dir, entry->d_name);
    lstat(file_path, &statbuf);
    //if deepest, write longestPath file
    if (!S_ISLNK(statbuf.st_mode) && depth > client->arg.max_depth) {
      client->arg.max_depth = depth;
      FILE* longestPath = fopen("longestPath.txt", "w");
      fwrite(file_path+2, 1, strlen(file_path)-2, longestPath);
      fclose(longestPath);      
    }
    //encounter directory, traverse recursively
    printf("%s\n", file_path);
    if (S_ISDIR(statbuf.st_mode))
      traverse_longestPath(client, file_path, depth+1);
  }

  closedir(dp);
  return 1;
}

int traverse_dir_tree(csiebox_client* client, char* dir) {
  DIR* dp;
  struct dirent* entry;
  struct stat statbuf;

  if ((dp = opendir(dir)) == NULL) {
    fprintf(stderr, "opendir fail\n");
    return 0;
  }
  
  while ((entry = readdir(dp)) != NULL) {
    //ignore . and ..
    if (strcmp(".", entry->d_name) == 0 || 
        strcmp("..", entry->d_name) == 0) {
      continue;
    }
    char file_path[400] = {'\0'};
    sprintf(file_path, "%s/%s", dir, entry->d_name);
    lstat(file_path, &statbuf);
    int is_hlink = 0; //for hard link

    //synchronize files!!!!!
    //send metadata
    csiebox_protocol_meta req;
    memset(&req, 0, sizeof(req));
    req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
    req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
    req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
    req.message.body.pathlen = strlen(file_path);
    req.message.body.stat = statbuf;
    //prepare header to receive command
    csiebox_protocol_header header;
    memset(&header, 0, sizeof(header));

    //encounter sym link
    int is_symlink = 0;
    if (S_ISLNK(statbuf.st_mode)) {
      // char* linkname = (char*)malloc(statbuf.st_size+1);
      is_symlink = 1;
      char linkname[400] = {0};
      // readlink(file_path, linkname, statbuf.st_size+1);
      int len = readlink(file_path, linkname, sizeof(linkname));
      linkname[len] = '\0';
      //remove first 7 characters (../cdir/)
      // memmove(linkname, linkname+8, strlen(linkname)-7);
      printf("sym: %s -> %s\n", file_path, linkname);
      // printf("link to: %s\n", linkname);
      //send request
      printf("send request\n");
      if (!send_message(client->conn_fd, &req, sizeof(req))) {
        fprintf(stderr, "send fail\n");
        return 0;
      }
      //send path
      printf("send path name\n");
      send_message(client->conn_fd, file_path, strlen(file_path));
      //send linkname
      printf("send linkname\n");
      if (!send_message(client->conn_fd, linkname, 400)) {
        fprintf(stderr, "send linkname fail\n");
      }

      //receive response message from server
      if (recv_message(client->conn_fd, &header, sizeof(header))) {
        if (header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
            header.res.op == CSIEBOX_PROTOCOL_OP_SYNC_META &&
            header.res.status == CSIEBOX_PROTOCOL_STATUS_OK)
            printf("Receive OK from server\n");
        else {
          printf("Recieve response message fail in symlink\n");
          return 0;
        }
      }
      // free(linkname);
      continue; //because symlink can't be counted in longest path
    }

    //encounter directory
    else if (S_ISDIR(statbuf.st_mode)) {
      printf("dir: %s\n", file_path);

      //dir file have no md5
      req.message.body.hash[0] = '\0';
      if (!send_message(client->conn_fd, &req, sizeof(req))) {
        fprintf(stderr, "send fail\n");
        return 0;
      }
      //send path
      send_message(client->conn_fd, file_path, strlen(file_path));

      //receive response message from server
      if (recv_message(client->conn_fd, &header, sizeof(header))) {
        if (header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
            header.res.op == CSIEBOX_PROTOCOL_OP_SYNC_META &&
            header.res.status == CSIEBOX_PROTOCOL_STATUS_OK)
            printf("Receive OK from server\n");
        else {
          printf("Recieve response message fail in dir\n");
          return 0;
        }
      }
      traverse_dir_tree(client, file_path);
    }
    //encounter reg file
    //there's two situation: hard link(st_nlink > 1) and reg file(st_nlink == 1)
    else {
      printf("reg: %s\n", file_path);
      //======= hard link part =======
      //there's two situation: hard link(st_nlink > 1) 
      //and reg file(st_nlink == 1)
      if (statbuf.st_nlink > 1) {
        //if inode have been registered, find the path it links to
        //and send message to server (sync it as hard link)
        //else, register the inode number and its own file path,
        //then sync it as reg file
        int inode = statbuf.st_ino;
        for (int i = 0; i < 300; i++) {
          if (client->arg.hlink_inodes[i] == inode) {
            is_hlink = 1;
            csiebox_protocol_hardlink hardlink;
            hardlink.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
            hardlink.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK;
            hardlink.message.header.req.datalen = sizeof(hardlink) - sizeof(hardlink.message.header);
            hardlink.message.body.srclen = strlen(file_path);
            hardlink.message.body.targetlen = strlen(client->arg.hlink_to[i]);
            //send hardlink header
            if (!send_message(client->conn_fd, &hardlink, sizeof(hardlink))) {
              fprintf(stderr, "send fail\n");
              return 0;
            }
            //send src path and target path
            send_message(client->conn_fd, file_path, strlen(file_path));
            send_message(client->conn_fd, client->arg.hlink_to[i]
                          , strlen(client->arg.hlink_to[i]));

            //send meta data
            if (!send_message(client->conn_fd, &req, sizeof(req))) {
              fprintf(stderr, "send meta data fail in hardlink\n");
              return 0;
            }

            //bug down here
            //receive status message
            csiebox_protocol_header ln_header;
            memset(&ln_header, 0, sizeof(ln_header));
            //printf("ready to recv\n");
            if (recv_message(client->conn_fd, &ln_header, sizeof(ln_header))) {
              // printf("in recv_message\n");
              // printf("0x%02x\n", ln_header.res.magic);
              // printf("0x%02x\n", ln_header.res.op);
              
              // &&ln_header.res.op == CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK
              if (ln_header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES) {
                // printf("in hardlink\n");
                if (ln_header.res.status == CSIEBOX_PROTOCOL_STATUS_OK)
                  printf("Receive OK from server\n");
                else
                  printf("Receive FAIL from server\n");
              }
              else {
                // printf("magic: %s res: %s\n", ln_header.res.magic, CSIEBOX_PROTOCOL_MAGIC_RES);
                // printf("op: %s hardlink: %s\n", ln_header.res.op, CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK);
                fprintf(stderr, "hardlink response error\n");
                return 0;
              }
            }
            else {
              fprintf(stderr, "receive fail\n");
            }
            //if find hardlink target, break the loop
            break;
          }
          else if (client->arg.hlink_inodes[i] == -1) {
            //register inode number
            client->arg.hlink_inodes[i] = inode;
            client->arg.hlink_to[i] = (char*)malloc(101);
            //ex: ./123.txt -> 123.txt
            // snprintf(client->arg.hlink_to[i], strlen(file_path)-1,
            //          file_path+2);
            snprintf(client->arg.hlink_to[i], strlen(file_path)+1,
                     file_path);
            printf("to be hardlink: %s\n", client->arg.hlink_to[i]);
            break;
          }
        }
      }

      //to skip following sync reg file part
      if (is_hlink) {
        continue;
      }
      //======= end hard link part =======

      md5_file(file_path, req.message.body.hash);
      if (!send_message(client->conn_fd, &req, sizeof(req))) {
        fprintf(stderr, "send fail\n");
        return 0;
      }
      //send path
      send_message(client->conn_fd, file_path, strlen(file_path));
      //receive command from server
      if (recv_message(client->conn_fd, &header, sizeof(header))) {
        if (header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
            header.res.op == CSIEBOX_PROTOCOL_OP_SYNC_META) {
          if (header.res.status == CSIEBOX_PROTOCOL_STATUS_OK) {
            printf("Receive OK from server\n");
            // return 1;
          }
          //send file data to server
          else if (header.res.status == CSIEBOX_PROTOCOL_STATUS_MORE) {
            printf("Receive MORE from server\n");
            csiebox_protocol_file file;
            file.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
            file.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
            file.message.header.req.datalen = sizeof(file) - sizeof(file.message.header);
            //read file data
            FILE* f = fopen(file_path, "r");
            fseek(f, 0, SEEK_END);
            int fsize = ftell(f);
            fseek(f, 0, SEEK_SET);
            char* buf = malloc(fsize+1);
            fread(buf, fsize, 1, f);
            fclose(f);
            buf[fsize] = '\0';
            file.message.body.datalen = fsize;
            //send file header
            if (!send_message(client->conn_fd, &file, sizeof(file))) {
              fprintf(stderr, "send fail\n");
              return 0;
            }
            //send data
            send_message(client->conn_fd, buf, strlen(buf));
            printf("send reg data: %s\n", file_path);
          }
          else
            fprintf(stderr, "Receive fail from server\n");
        } 
        else {
          fprintf(stderr, "Receive response message fail from server in regfile\n");
          return 0;
        }
      }
    }

    // //find the deepest path
    // if (!is_symlink && depth >= client->arg.max_depth){
    //   client->arg.max_depth = depth;
    //   // strcpy(client->arg.longest_path_name, file_path);
    //   FILE* longestPath = fopen("longestPath.txt", "w");
    //   fwrite(file_path+2, 1, strlen(file_path)-2, longestPath);
    //   fclose(longestPath);
    // }

  }

  closedir(dp);

  return 1;
}

void csiebox_client_destroy(csiebox_client** client) {
  csiebox_client* tmp = *client;
  *client = 0;
  if (!tmp) {
    return;
  }
  close(tmp->conn_fd);
  free(tmp);
}

//read config file
static int parse_arg(csiebox_client* client, int argc, char** argv) {
  if (argc != 2) {
    return 0;
  }
  FILE* file = fopen(argv[1], "r");
  if (!file) {
    return 0;
  }
  fprintf(stderr, "reading config...\n");
  size_t keysize = 20, valsize = 20;
  char* key = (char*)malloc(sizeof(char) * keysize);
  char* val = (char*)malloc(sizeof(char) * valsize);
  ssize_t keylen, vallen;
  int accept_config_total = 5;
  int accept_config[5] = {0, 0, 0, 0, 0};
  while ((keylen = getdelim(&key, &keysize, '=', file) - 1) > 0) {
    key[keylen] = '\0';
    vallen = getline(&val, &valsize, file) - 1;
    val[vallen] = '\0';
    fprintf(stderr, "config (%d, %s)=(%d, %s)\n", keylen, key, vallen, val);
    if (strcmp("name", key) == 0) {
      if (vallen <= sizeof(client->arg.name)) {
        strncpy(client->arg.name, val, vallen);
        accept_config[0] = 1;
      }
    } else if (strcmp("server", key) == 0) {
      if (vallen <= sizeof(client->arg.server)) {
        strncpy(client->arg.server, val, vallen);
        accept_config[1] = 1;
      }
    } else if (strcmp("user", key) == 0) {
      if (vallen <= sizeof(client->arg.user)) {
        strncpy(client->arg.user, val, vallen);
        accept_config[2] = 1;
      }
    } else if (strcmp("passwd", key) == 0) {
      if (vallen <= sizeof(client->arg.passwd)) {
        strncpy(client->arg.passwd, val, vallen);
        accept_config[3] = 1;
      }
    } else if (strcmp("path", key) == 0) {
      if (vallen <= sizeof(client->arg.path)) {
        strncpy(client->arg.path, val, vallen);
        accept_config[4] = 1;
      }
    }
  }
  free(key);
  free(val);
  fclose(file);
  int i, test = 1;
  for (i = 0; i < accept_config_total; ++i) {
    test = test & accept_config[i];
  }
  if (!test) {
    fprintf(stderr, "config error\n");
    return 0;
  }
  return 1;
}

static int login(csiebox_client* client) {
  csiebox_protocol_login req;
  memset(&req, 0, sizeof(req));
  req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
  req.message.header.req.op = CSIEBOX_PROTOCOL_OP_LOGIN;
  req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
  memcpy(req.message.body.user, client->arg.user, strlen(client->arg.user));
  md5(client->arg.passwd,
      strlen(client->arg.passwd),
      req.message.body.passwd_hash);
  if (!send_message(client->conn_fd, &req, sizeof(req))) {
    fprintf(stderr, "send fail\n");
    return 0;
  }
  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  if (recv_message(client->conn_fd, &header, sizeof(header))) {
    if (header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
        header.res.op == CSIEBOX_PROTOCOL_OP_LOGIN &&
        header.res.status == CSIEBOX_PROTOCOL_STATUS_OK) {
      client->client_id = header.res.client_id;
      return 1;
    } else {
      return 0;
    }
  }
  return 0;
}
