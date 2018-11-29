#include "csiebox_server.h"

#include "csiebox_common.h"
#include "connect.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <utime.h>

static int parse_arg(csiebox_server* server, int argc, char** argv);
static void handle_request(csiebox_server* server, int conn_fd);
static int get_account_info(
  csiebox_server* server,  const char* user, csiebox_account_info* info);
static void login(
  csiebox_server* server, int conn_fd, csiebox_protocol_login* login);
static void logout(csiebox_server* server, int conn_fd);
static char* get_user_homedir(
  csiebox_server* server, csiebox_client_info* info);

#define DIR_S_FLAG (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)//permission you can use to create new file
#define REG_S_FLAG (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)//permission you can use to create new directory

//read config file, and start to listen
void csiebox_server_init(
  csiebox_server** server, int argc, char** argv) {
  csiebox_server* tmp = (csiebox_server*)malloc(sizeof(csiebox_server));
  if (!tmp) {
    fprintf(stderr, "server malloc fail\n");
    return;
  }
  memset(tmp, 0, sizeof(csiebox_server));
  if (!parse_arg(tmp, argc, argv)) {
    fprintf(stderr, "Usage: %s [config file]\n", argv[0]);
    free(tmp);
    return;
  }
  int fd = server_start();
  if (fd < 0) {
    fprintf(stderr, "server fail\n");
    free(tmp);
    return;
  }
  tmp->client = (csiebox_client_info**)
      malloc(sizeof(csiebox_client_info*) * getdtablesize());
  if (!tmp->client) {
    fprintf(stderr, "client list malloc fail\n");
    close(fd);
    free(tmp);
    return;
  }
  memset(tmp->client, 0, sizeof(csiebox_client_info*) * getdtablesize());
  tmp->listen_fd = fd;
  *server = tmp;
}

//wait client to connect and handle requests from connected socket fd
int csiebox_server_run(csiebox_server* server) {
  int conn_fd, conn_len;
  struct sockaddr_in addr;
  while (1) {
    memset(&addr, 0, sizeof(addr));
    conn_len = 0;
    // waiting client connect
    conn_fd = accept(
      server->listen_fd, (struct sockaddr*)&addr, (socklen_t*)&conn_len);
    if (conn_fd < 0) {
      if (errno == ENFILE) {
          fprintf(stderr, "out of file descriptor table\n");
          continue;
        } else if (errno == EAGAIN || errno == EINTR) {
          continue;
        } else {
          fprintf(stderr, "accept err\n");
          fprintf(stderr, "code: %s\n", strerror(errno));
          break;
        }
    }
    // handle request from connected socket fd
    handle_request(server, conn_fd);
  }
  return 1;
}

void csiebox_server_destroy(csiebox_server** server) {
  csiebox_server* tmp = *server;
  *server = 0;
  if (!tmp) {
    return;
  }
  close(tmp->listen_fd);
  int i = getdtablesize() - 1;
  for (; i >= 0; --i) {
    if (tmp->client[i]) {
      free(tmp->client[i]);
    }
  }
  free(tmp->client);
  free(tmp);
}

//read config file
static int parse_arg(csiebox_server* server, int argc, char** argv) {
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
  int accept_config_total = 2;
  int accept_config[2] = {0, 0};
  while ((keylen = getdelim(&key, &keysize, '=', file) - 1) > 0) {
    key[keylen] = '\0';
    vallen = getline(&val, &valsize, file) - 1;
    val[vallen] = '\0';
    fprintf(stderr, "config (%d, %s)=(%d, %s)\n", keylen, key, vallen, val);
    if (strcmp("path", key) == 0) {
      if (vallen <= sizeof(server->arg.path)) {
        strncpy(server->arg.path, val, vallen);
        accept_config[0] = 1;
      }
    } else if (strcmp("account_path", key) == 0) {
      if (vallen <= sizeof(server->arg.account_path)) {
        strncpy(server->arg.account_path, val, vallen);
        accept_config[1] = 1;
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

//It is a sample function
//you may remove it after understanding
// int sampleFunction(int conn_fd, csiebox_protocol_meta* meta) {
  
//   printf("In sampleFunction:\n");
//   uint8_t hash[MD5_DIGEST_LENGTH];
//   memset(&hash, 0, sizeof(hash));

//   //cwd: bin
//   md5_file(".gitignore", hash);
//   printf("pathlen: %d\n", meta->message.body.pathlen);

//   printf("%s\n%s\n", hash, meta->message.body.hash);
//   if (memcmp(hash, meta->message.body.hash, sizeof(hash)) == 0) {
//     printf("hashes are equal!\n");
//   }

//   //use the pathlen from client to recv path 
//   char buf[400];
//   memset(buf, 0, sizeof(buf));
//   recv_message(conn_fd, buf, meta->message.body.pathlen);
//   printf("This is the path from client:%s\n", buf);

//   //send OK to client
//   csiebox_protocol_header header;
//   memset(&header, 0, sizeof(header));
//   header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
//   header.res.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
//   header.res.datalen = 0;
//   header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
//   if(!send_message(conn_fd, &header, sizeof(header))){
//     fprintf(stderr, "send fail\n");
//     return 0;
//   }

//   return 1;
// }

int sync_meta(csiebox_server* server, 
              int conn_fd, csiebox_protocol_meta* meta) {
  printf("In sync_meta:\n");
  uint8_t hash[MD5_DIGEST_LENGTH];
  memset(&hash, 0, sizeof(hash));
  //use the pathlen from client to recv path 
  char file_path[400];
  memset(file_path, 0, sizeof(file_path));
  recv_message(conn_fd, file_path, meta->message.body.pathlen);
  printf("pathlen: %d\n", meta->message.body.pathlen);
  printf("This is the path from client:%s\n", file_path);
  // change cwd from bin to sdir/user
  chdir(server->arg.local_dir_name);
  // strcpy(file_path, strcat(server->arg.local_dir_name, file_path));

  //prepare protocol to be sent to client
  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
  header.res.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
  header.res.datalen = 0;

  int more = 0;
  //if it's a symbolic link
  if (S_ISLNK(meta->message.body.stat.st_mode)) {
    //receive linkname
    char linkname[400];
    memset(linkname, 0, sizeof(linkname));
    recv_message(conn_fd, linkname, 400);

    struct stat st;
    lstat(file_path, &st);
    if (!S_ISLNK(st.st_mode)) {
      printf("create symlink: %s -> %s\n", file_path, linkname);
      symlink(linkname, file_path);
    }
    else {
      char* old_link = (char*)malloc(st.st_size+1);
      readlink(file_path, old_link, st.st_size+1);
      //remove first 7 characters (../sdir/)
      // memmove(old_link, old_link+8, strlen(old_link)-7);
      if (strcmp(old_link, linkname) == 0) {
        printf("symlink: %s -> %s already exist\n", file_path, linkname);
      }
      else {
        printf("relink: %s -> %s\n", file_path, linkname);
        unlink(file_path);
        symlink(linkname, file_path);
      }

      free(old_link);
    }
    header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;

  }
  //if it's a directory
  else if (meta->message.body.hash[0] == '\0') {
    struct stat st;
    lstat(file_path, &st);
    //if dirname doesn't exist, mkdir
    if (!S_ISDIR(st.st_mode)) {
      printf("create dir: %s\n", file_path);
      mkdir(file_path, DIR_S_FLAG);

      //sync mode
      chmod(file_path, meta->message.body.stat.st_mode);
    }
    else
      printf("dir: %s already exist\n", file_path);
    header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
  }
  //if it's a regular file
  else {
    FILE* file;
    file = fopen(file_path, "r");
    //if the file already exist and md5 is the same
    // printf("%s\n%s\n", );
    if (file) {
      md5_file(file_path, hash);
      if (memcmp(hash, meta->message.body.hash, sizeof(hash)) == 0) {
        printf("hashes are equal!\n");
        header.res.status = CSIEBOX_PROTOCOL_STATUS_OK; //send OK
        fclose(file);        
      }
      else {
        header.res.status = CSIEBOX_PROTOCOL_STATUS_MORE;
        more = 1;
      }
    }
    else {
      header.res.status = CSIEBOX_PROTOCOL_STATUS_MORE;
      more = 1;
    }
  }
  //send command to client (MORE or OK)
  if(!send_message(conn_fd, &header, sizeof(header))){
    fprintf(stderr, "send fail\n");
    return 0;
  }
  //receive and sync file data
  if (more) {
    char databuf[4001];
    memset(databuf, 0, sizeof(databuf));
    //recieve file header first
    csiebox_protocol_file file;
    recv_message(conn_fd, &file, sizeof(file));
    //recieve file data
    recv_message(conn_fd, databuf, file.message.body.datalen);

    // if (strcmp(file_path, "./.gitignore") == 0) {
      // printf("%s\n", databuf);
    // }
    FILE* f = fopen(file_path, "w");
    fwrite(databuf, 1, file.message.body.datalen, f);
    printf("sync file: %s\n", file_path);

    //sync mode and ctime atime
    chmod(file_path, meta->message.body.stat.st_mode);
    struct utimbuf time;
    time.actime = meta->message.body.stat.st_atime;
    time.modtime = meta->message.body.stat.st_mtime;
    utime(file_path, &time);

    fclose(f);
  }

  //cwd: bin
  // md5_file(".gitignore", hash);
  // printf("pathlen: %d\n", meta->message.body.pathlen);

  // if (memcmp(hash, meta->message.body.hash, sizeof(hash)) == 0) {
  //   printf("hashes are equal!\n");
  // }

  // //use the pathlen from client to recv path 
  // char buf[400];
  // memset(buf, 0, sizeof(buf));
  // recv_message(conn_fd, buf, meta->message.body.pathlen);
  // printf("This is the path from client:%s\n", buf);

  //send OK to client
  // csiebox_protocol_header header;
  // memset(&header, 0, sizeof(header));
  // header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
  // header.res.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
  // header.res.datalen = 0;
  // header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
  chdir("../../bin");
  return 1;
}

//this is where the server handle requests, you should write your code here
static void handle_request(csiebox_server* server, int conn_fd) {
  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  while (recv_message(conn_fd, &header, sizeof(header))) {
    if (header.req.magic != CSIEBOX_PROTOCOL_MAGIC_REQ) {
      continue;
    }
    switch (header.req.op) {
      case CSIEBOX_PROTOCOL_OP_LOGIN:
        fprintf(stderr, "login\n");
        csiebox_protocol_login req;
        if (complete_message_with_header(conn_fd, &header, &req)) {
          login(server, conn_fd, &req);
        }
        break;
      case CSIEBOX_PROTOCOL_OP_SYNC_META:
        fprintf(stderr, "sync meta\n");
        csiebox_protocol_meta meta;
        if (complete_message_with_header(conn_fd, &header, &meta)) {
          
        // This is a sample function showing how to send data using defined header in common.h
        // You can remove it after you understand
        // sampleFunction(conn_fd, &meta);
          sync_meta(server, conn_fd, &meta);
          //====================
          //        TODO
          //====================
        }
        break;
      case CSIEBOX_PROTOCOL_OP_SYNC_FILE:
        fprintf(stderr, "sync file\n");
        csiebox_protocol_file file;
        if (complete_message_with_header(conn_fd, &header, &file)) {
          //====================
          //        TODO
          //====================
        }
        break;
      case CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK:
        fprintf(stderr, "sync hardlink\n");
        csiebox_protocol_hardlink hardlink;
        if (complete_message_with_header(conn_fd, &header, &hardlink)) {
          //====================
          //change dir to ../sdir/user
          chdir(server->arg.local_dir_name);

          char srcname[101] = {0};
          char targetname[101] = {0};
          //prepare status header to be sent to client
          csiebox_protocol_header ln_header;
          memset(&ln_header, 0, sizeof(ln_header));
          ln_header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
          ln_header.res.op = CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK;
          ln_header.res.datalen = 0;

          recv_message(conn_fd, srcname, hardlink.message.body.srclen);
          recv_message(conn_fd, targetname, hardlink.message.body.targetlen);
          printf("recv message in sync hardlink\n");
          printf("going to link: %s -> %s\n", srcname, targetname);

          //get meta data
          csiebox_protocol_meta metadata;
          recv_message(conn_fd, &metadata, sizeof(metadata));

          struct stat src_stbuf;
          struct stat tgt_stbuf;
          lstat(srcname, &src_stbuf);
          lstat(targetname, &tgt_stbuf);

          FILE* f = fopen(srcname, "r");
          //if srcname already exist
          int ln = 1;
          if (f) {
            //if inode number is equal to targetname, 
            //then doesn't need to sync hardlink
            if (src_stbuf.st_ino == tgt_stbuf.st_ino) {
              printf("hardlink: %s -> %s already exist\n", srcname, targetname);
              ln = 0;
              ln_header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
            }
            else {
              printf("unlink: %s and then relink later", srcname);
              unlink(srcname);
              ln_header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
            }
            fclose(f);
          }
          if (ln) {
            if (link(targetname, srcname) == 0) {
              //sync meta data: mode, mtime, atime
              chmod(srcname, metadata.message.body.stat.st_mode);
              struct utimbuf time;
              time.actime = metadata.message.body.stat.st_atime;
              time.modtime = metadata.message.body.stat.st_mtime;
              utime(srcname, &time);

              ln_header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
              printf("create hardlink: %s -> %s\n", srcname, targetname);
            }
            else {
              ln_header.res.status = CSIEBOX_PROTOCOL_STATUS_FAIL;
              fprintf(stderr, "create hlink fail: %s -> %s\n", srcname,targetname);
              fprintf(stderr, "link fail\nerror: %d\n", errno);
            }
          }
          //send status to client
          // printf("sendop: 0x%02x\n", ln_header.res.op);
          if(!send_message(conn_fd, &ln_header, sizeof(ln_header))){
            fprintf(stderr, "send fail\n");
            return 0;
          }

          chdir("../../bin");
          //====================
        }
        break;
      case CSIEBOX_PROTOCOL_OP_SYNC_END:
        fprintf(stderr, "sync end\n");
        csiebox_protocol_header end;
          //====================
          //        TODO
          //====================
        break;
      case CSIEBOX_PROTOCOL_OP_RM:
        fprintf(stderr, "rm\n");
        csiebox_protocol_rm rm;
        if (complete_message_with_header(conn_fd, &header, &rm)) {
          //====================
          //        TODO
          //====================
        }
        break;
      default:
        fprintf(stderr, "unknown op %x\n", header.req.op);
        break;
    }
  }
  fprintf(stderr, "end of connection\n");
  logout(server, conn_fd);
}

//open account file to get account information
static int get_account_info(
  csiebox_server* server,  const char* user, csiebox_account_info* info) {
  FILE* file = fopen(server->arg.account_path, "r");
  if (!file) {
    return 0;
  }
  size_t buflen = 100;
  char* buf = (char*)malloc(sizeof(char) * buflen);
  memset(buf, 0, buflen);
  ssize_t len;
  int ret = 0;
  int line = 0;
  while ((len = getline(&buf, &buflen, file) - 1) > 0) {
    ++line;
    buf[len] = '\0';
    char* u = strtok(buf, ",");
    if (!u) {
      fprintf(stderr, "illegal form in account file, line %d\n", line);
      continue;
    }
    if (strcmp(user, u) == 0) {
      memcpy(info->user, user, strlen(user));
      char* passwd = strtok(NULL, ",");
      if (!passwd) {
        fprintf(stderr, "illegal form in account file, line %d\n", line);
        continue;
      }
      md5(passwd, strlen(passwd), info->passwd_hash);
      ret = 1;
      break;
    }
  }
  free(buf);
  fclose(file);
  return ret;
}

//handle the login request from client
static void login(
  csiebox_server* server, int conn_fd, csiebox_protocol_login* login) {
  int succ = 1;
  csiebox_client_info* info =
    (csiebox_client_info*)malloc(sizeof(csiebox_client_info));
  memset(info, 0, sizeof(csiebox_client_info));
  if (!get_account_info(server, login->message.body.user, &(info->account))) {
    fprintf(stderr, "cannot find account\n");
    succ = 0;
  }
  if (succ &&
      memcmp(login->message.body.passwd_hash,
             info->account.passwd_hash,
             MD5_DIGEST_LENGTH) != 0) {
    fprintf(stderr, "passwd miss match\n");
    succ = 0;
  }

  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
  header.res.op = CSIEBOX_PROTOCOL_OP_LOGIN;
  header.res.datalen = 0;
  if (succ) {
    if (server->client[conn_fd]) {
      free(server->client[conn_fd]);
    }
    info->conn_fd = conn_fd;
    server->client[conn_fd] = info;
    header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
    header.res.client_id = info->conn_fd;
    char* homedir = get_user_homedir(server, info);
    mkdir(homedir, DIR_S_FLAG);
    // chdir(homedir);
    strcpy(server->arg.local_dir_name, homedir);
    free(homedir);
  } else {
    header.res.status = CSIEBOX_PROTOCOL_STATUS_FAIL;
    free(info);
  }
  send_message(conn_fd, &header, sizeof(header));  //to client.c -12 line
}

static void logout(csiebox_server* server, int conn_fd) {
  free(server->client[conn_fd]);
  server->client[conn_fd] = 0;
  close(conn_fd);
}

static char* get_user_homedir(
  csiebox_server* server, csiebox_client_info* info) {
  char* ret = (char*)malloc(sizeof(char) * PATH_MAX);
  memset(ret, 0, PATH_MAX);
  sprintf(ret, "%s/%s", server->arg.path, info->account.user);
  return ret;
}

