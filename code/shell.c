#include "disk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h> 

char* gets(char *buf, int max) //read command char from stdin
{
  int i, cc;
  char c;
  for(i=0; i+1 < max; ){
    cc = read(0, &c, 1);
    if(cc < 1)
      break;
    buf[i++] = c;
    if(c == '\n' || c == '\r'){
      break;
    }
  }
  buf[i] = '\0';
  return buf;
}

int getcmd(char *buf, int nbuf) //read a command line
{
  printf("EXT2 FILE SYS $");
  fflush(stdout);
  memset(buf, 0, nbuf);
  gets(buf, nbuf);
  if(buf[0] == 0) // EOF
    return -1;
  return 0;
}

int read_data_block(unsigned int datablock_num, char* buf){ //read a data block  
    char disk_block_buf[DEVICE_BLOCK_SIZE];  //one data block include two disk blocks
    int flag = 0;
    flag += disk_read_block(datablock_num*2,disk_block_buf); //read the first disk block
    memcpy(buf,disk_block_buf,DEVICE_BLOCK_SIZE);
    flag += disk_read_block(datablock_num*2+1,disk_block_buf);//read the second disk block
    memcpy(buf+DEVICE_BLOCK_SIZE,disk_block_buf,DEVICE_BLOCK_SIZE);
    return (flag==0)?0:-1; //if succeed return 0
}

int write_data_block(unsigned int datablock_num, char* buf){ //write a data block
    char disk_block_buf[DEVICE_BLOCK_SIZE];
    int flag = 0;
    memcpy(disk_block_buf,buf,DEVICE_BLOCK_SIZE);  //divide data block into 2 part
    flag += disk_write_block(datablock_num*2,disk_block_buf);//write the first disk block
    memcpy(disk_block_buf,buf+DEVICE_BLOCK_SIZE,DEVICE_BLOCK_SIZE);
    flag += disk_write_block(datablock_num*2+1,disk_block_buf); //write the second disk block
    return (flag==0)?0:-1; //if succeed return 0
}

void reset_disk(){  //reset file system
    close_disk();
    open_disk();
    create_disk();//reset all to zero
    char disk_buf[DEVICE_BLOCK_SIZE*2]; //data_block buf
    if(read_data_block(0,disk_buf)==0){ //start reset super block
      sp_block *super_block_buf = (sp_block *)disk_buf; //super block struct
      super_block_buf->magic_num = MAGIC_NUM; //write magic num
      super_block_buf->dir_inode_count = 1; //at first the root dir inode is exist
      super_block_buf->free_block_count = 4059; //2^12-1-1-32-3
      super_block_buf->free_inode_count = 1023; //at first the root dir inode is exist
      memset(super_block_buf->empty_map,0xffffffff,sizeof(super_block_buf->empty_map)); //fill empty map
      super_block_buf->inode_map[0] = 0x00000001; //root dir inode is used
      super_block_buf->block_map[0] = 0xffffffff;
      super_block_buf->block_map[1] = 0x0000001f; //34 data blocks are used for system and 3 are used for root dir
      write_data_block(0,disk_buf);  //reset super block
    }
    else
    {
      printf("reset super block fail\n");
      exit(0);
    }
    struct inode inode_arr[32]; //each data block include 32 inode
    for(int i = 0; i < 32;i++){
      inode_arr[i].size = 3; //every inode include 3 data blocks
    }
    uint32_t data_block_num = 34;
    for (int i = 2; i < 34; i++) //start reset inode array
    {
      for(int j = 0;j < 32;j++,data_block_num+=3){
        if(i == 2 && j == 0){  //init root dir
          inode_arr[j].file_type = TYPE_DIR;
          inode_arr[j].link = 1;
        }
        else{
          inode_arr[j].file_type = 0;
          inode_arr[j].link = 0;
        }
        inode_arr[j].block_point[0] = data_block_num;  //give 3 data blocks to every inode
        inode_arr[j].block_point[1] = data_block_num+1;
        inode_arr[j].block_point[2] = data_block_num+2;
        inode_arr[j].block_point[3] = 0;
        inode_arr[j].block_point[4] = 0;
        inode_arr[j].block_point[5] = 0;
      }
      if(write_data_block(i,(char *)inode_arr)!= 0) //set inodes in data block i
      {
        printf("cannot set node array block %d\n",i);
        exit(0);
      }
    }
}

void read_super(){  //read super block
    char disk_buf[DEVICE_BLOCK_SIZE*2]; //data_block buf
    if(read_data_block(0,disk_buf)==0){
      sp_block *super_block_buf = (sp_block *)disk_buf;
      printf("succesfully read super block\n");
      if (super_block_buf->magic_num != MAGIC_NUM)
      {
        //magic num is broken
        //you should reset the file system
        printf("magic num is broken:%x,resetting file system...\n",super_block_buf->magic_num);
        reset_disk();
        printf("reset finish!\n");
      }
      read_data_block(0,disk_buf);
      super_block_buf = (sp_block *)disk_buf;
      printf("magic num:%x\n",super_block_buf->magic_num);
      my_super_block = *super_block_buf;
    }
    else
    {
      printf("cannot read super block\n");
      exit(0);
    }
}

void split_command(char *buf, char * word_list[512]){ //split command line into words
  char *p = buf;
  int i = 0;
  word_list[0] = 0;
  while(*p != '\n' && *p != '\r'){
    if(*p == ' '){
      *p = '\0';
    }
    p++;
  }
  *p = '\0';
  char *q = buf;
  while (q < p)
  {
    word_list[i] = q;
    i++;
    while(*q != '\0' && q < p){
      q++;
    }
    while(*q == '\0' && q < p){
      q++;
    }
  }
  word_list[i] = 0;
}

int split_path(char *path, char *path_list[MAX_PATH_DEPTH]){ //split path into dirs(and file name)
  char *p = path;
  int i = 0;
  path_list[0] = 0;
  while (*p != '\0')
  {
    if(*p == '/'){
      *p = '\0';
      path_list[i] = p+1;
      i++;
      if(i >= 20){
        printf("path overflow!\n");
        return -1;
      }
    }
    p++;
  }
  path_list[i] = 0;
  return 0;
}

uint32_t find_inode_db_index(uint32_t inode_num){  //inode num to data block num
  return 2+inode_num/32;  //inode is in data block NO.(2+inode_num/32)
}

uint32_t find_inode_db_offset(uint32_t inode_num){  //inode num offset in data block
  return inode_num % 32;
}

struct inode find_inode_from_disk(uint32_t inode_id){ //find inode NO.inode_id from disk
  char datablock_buf[DEVICE_BLOCK_SIZE*2];
  read_data_block(find_inode_db_index(inode_id),datablock_buf);  //read the data block which include inode NO.inode_id
  struct inode inode_buf[32];
  memcpy(inode_buf,datablock_buf,sizeof(datablock_buf));
  return inode_buf[find_inode_db_offset(inode_id)]; //pick out the inode NO.inode_id
}

struct dir_item find_diritem_from_db(uint32_t block_num,char target_name[121],uint8_t target_type){ //find dir_item from data block
  char datablock_buf[DEVICE_BLOCK_SIZE*2];
  read_data_block(block_num,datablock_buf);
  struct dir_item dir_buf[8];
  memcpy(dir_buf,datablock_buf,sizeof(datablock_buf));
  for (int i = 0; i < 8; i++) //a data block include 8 dir_item
  {
    if(dir_buf[i].type == target_type && strcmp(target_name,dir_buf[i].name)==0 && dir_buf[i].valid==1) //if dir is match
      return dir_buf[i]; //return dir_item
  }
  struct dir_item dir_default;
  dir_default.valid = 0;
  return dir_default; //if cannot find, return a dir_item with valid = 0
}

int find_empty_diritem_from_db(uint32_t block_num){ //find an empty dir_item in data block NO.block_num
  char datablock_buf[DEVICE_BLOCK_SIZE*2];
  read_data_block(block_num,datablock_buf);
  struct dir_item dir_buf[8];
  memcpy(dir_buf,datablock_buf,sizeof(datablock_buf));
  for(int i = 0;i < 8;i++){
    if(dir_buf[i].valid==0)
      return i; //if a dir_item is empty,return its index
  }
  return -1; //if no dir_item is empty,return -1
}

uint32_t find_empty_inode(){ //find an empty inode
  //inode map in super block:32bit * 32
  if(my_super_block.free_inode_count==0) return 0; //no empty inode
  for(int i = 0;i < 32;i++){
    if(my_super_block.inode_map[i] == 0xffffffff) continue;
    uint32_t checknum = 0x1;
    for(int j = 0;j < 32;j++){
      if((my_super_block.inode_map[i] & (checknum << j))==0){
        my_super_block.inode_map[i] |= (checknum << j); //update inode map
        my_super_block.free_inode_count--;
        my_super_block.free_block_count-=3;
        return i*32+j;
      }
    }
  }
  return 0;
}

uint32_t insert_new_diritem(char *name, uint8_t type, uint32_t block_num, int offset){ //insert a new dir_item in data block NO.block_num
  char datablock_buf[DEVICE_BLOCK_SIZE*2];
  read_data_block(block_num,datablock_buf);
  struct dir_item dir_buf[8];
  memcpy(dir_buf,datablock_buf,sizeof(datablock_buf));
  dir_buf[offset].valid = 1;
  strcpy(dir_buf[offset].name,name);
  dir_buf[offset].type = type;
  //then find an empty inode for it
  if((dir_buf[offset].inode_id = find_empty_inode()) == 0){
    printf("no empty inode for new dir\n");
    return 0;
  }
  memcpy(datablock_buf,dir_buf,sizeof(datablock_buf));
  write_data_block(block_num,datablock_buf); //update data block with new dir item
  //printf("insert new dir %s in block %d\n",name,block_num); //test
  return dir_buf[offset].inode_id; //return new inode id
}

uint32_t build_new_dir_or_file(char *name,uint8_t type,struct inode cur_inode){ //build a new dir/file under current dir
  //find an unused dir_item in current inode
  //find an unused inode in inode array
  //link new dir_item to new inode
  for(int i = 0;i < cur_inode.size;i++){
    int dir_offset = find_empty_diritem_from_db(cur_inode.block_point[i]);
    if(dir_offset != -1){
      uint32_t new_inode_id = insert_new_diritem(name,type,cur_inode.block_point[i],dir_offset);
      //if(new_inode_id > 0) printf("new: %s inode: %d\n",name,new_inode_id); //test
      return new_inode_id;
    }
  }
  printf("no empty dir_item in current dir,build failed\n");
  return 0;
}

uint32_t touch(char * path_list[MAX_PATH_DEPTH]){ //touch
  uint32_t cur_inode_id = 0; //root dir's inode
    int i = 0;
    for(i = 0;path_list[i] != NULL;i++){
        int dir_item_isfind = 0; //is dir_item exist in cur_inode?
        struct inode cur_inode = find_inode_from_disk(cur_inode_id); //find current inode from disk
        struct dir_item cur_dir_item;
        for(int j = 0;j < cur_inode.size;j++){  //is this file/dir exist?
          //printf("search in block %d...\n",cur_inode.block_point[j]); //test
          if(path_list[i+1] == NULL) //path_list[i] is a file name
            cur_dir_item = find_diritem_from_db(cur_inode.block_point[j],path_list[i],TYPE_FILE);
          else   //path_list[i] is a dir name
            cur_dir_item = find_diritem_from_db(cur_inode.block_point[j],path_list[i],TYPE_DIR);
          if(cur_dir_item.valid==1){
            dir_item_isfind = 1;
            break;
          }
        }
        if(dir_item_isfind == 1){  //the dir/file is exist
          printf("%s is already exist\n",path_list[i]); //test
          cur_inode_id = cur_dir_item.inode_id; //update current inode id to next inode id
        }
        else{
          printf("%s is not exist,build a new one...\n",path_list[i]); //test
          if(path_list[i+1] == NULL) //path_list[i] is a file name
            cur_inode_id = build_new_dir_or_file(path_list[i],TYPE_FILE,cur_inode);
          else //path_list[i] is a dir name
            cur_inode_id = build_new_dir_or_file(path_list[i],TYPE_DIR,cur_inode);
          if(cur_inode_id==0){
            printf("build failed\n");
            return 0;
          }
        }
    }
  return cur_inode_id; //return new file's inode id
}

void mkdir(char * path_list[MAX_PATH_DEPTH]){  //mkdir
    uint32_t cur_inode_id = 0; //root dir's inode
    int i = 0;
    for(i = 0;path_list[i] != NULL;i++){
        int dir_item_isfind = 0; //is dir_item exist in cur_inode?
        struct inode cur_inode = find_inode_from_disk(cur_inode_id); //find current inode from disk
        struct dir_item cur_dir_item;
        for(int j = 0;j < cur_inode.size;j++){
          //printf("search in block %d...\n",cur_inode.block_point[j]); //test
          cur_dir_item = find_diritem_from_db(cur_inode.block_point[j],path_list[i],TYPE_DIR);
          if(cur_dir_item.valid==1){
            dir_item_isfind = 1;
            break;
          }
        }
        if(dir_item_isfind == 1){
          //the dir is exist
          printf("%s is already exist\n",path_list[i]); //test
          cur_inode_id = cur_dir_item.inode_id; //update current inode id to next inode id
        }
        else{
          //printf("%s is not exist,build a new one...\n",path_list[i]); //test
          cur_inode_id = build_new_dir_or_file(path_list[i],TYPE_DIR,cur_inode);
          if(cur_inode_id==0){
            printf("build failed\n");
            return;
          }
        }
    }
}

void print_dir_item(uint32_t inode_id){ //print all dir item in inode
  printf("********************\n");
  printf("name\t\ttype\n");
  struct inode target_inode = find_inode_from_disk(inode_id);
  struct dir_item dir_buf[8];
  char db_buf[DEVICE_BLOCK_SIZE*2];
  for(int i = 0;i < target_inode.size;i++){  //read all blocks in inode
    if(read_data_block(target_inode.block_point[i],db_buf)!=0){
      printf("cannot read block %d\n",target_inode.block_point[i]);
      return;
    }
    memcpy(dir_buf,db_buf,sizeof(db_buf));
    for(int j = 0;j < 8;j++){ //print every dir item in every block
      if(dir_buf[j].valid == 1) 
        printf("%s\t\t%s\n",dir_buf[j].name,(dir_buf[j].type==TYPE_DIR)?"dir":(dir_buf[j].type==TYPE_FILE)?"file":"");
    }
  }
  printf("********************\n");
}

void ls(char * path_list[MAX_PATH_DEPTH]){  //ls
    uint32_t cur_inode_id = 0; //root dir's inode
    int i = 0;
    for(i = 0;path_list[i] != NULL;i++){ 
        int dir_item_isfind = 0; //is dir_item exist in cur_inode?
        struct inode cur_inode = find_inode_from_disk(cur_inode_id); //find current inode from disk
        struct dir_item cur_dir_item;
        for(int j = 0;j < cur_inode.size;j++){
          //printf("search in block %d...\n",cur_inode.block_point[j]); //test
          cur_dir_item = find_diritem_from_db(cur_inode.block_point[j],path_list[i],TYPE_DIR);
          if(cur_dir_item.valid==1){
            dir_item_isfind = 1;
            break;
          }
        }
        if(dir_item_isfind == 1){
          //the dir is exist
          cur_inode_id = cur_dir_item.inode_id; //update current inode id to next inode id
          //printf("%s is already exist,inode = %d\n",path_list[i],cur_inode_id); //test
        }
        else{
          printf("%s is not exist\n",path_list[i]);
          return;
        }
      }
      print_dir_item(cur_inode_id);
}

void copy_file(uint32_t inode_src_id, uint32_t inode_des_id){ //copy inode src to des
  struct inode inode_src = find_inode_from_disk(inode_src_id);
  struct inode inode_des = find_inode_from_disk(inode_des_id);
  char buf[DEVICE_BLOCK_SIZE*2];
  for(int i = 0;i < inode_src.size;i++){
    read_data_block(inode_src.block_point[i],buf);
    write_data_block(inode_des.block_point[i],buf);
  }
}

void cp(char * path_list_src[MAX_PATH_DEPTH], char * path_list_des[MAX_PATH_DEPTH]){
    uint32_t cur_inode_id = 0; //root dir's inode
    int i = 0;
    for(i = 0;path_list_src[i] != NULL;i++){  //find path_src
        int dir_item_isfind = 0; //is dir_item exist in cur_inode?
        struct inode cur_inode = find_inode_from_disk(cur_inode_id); //find current inode from disk
        struct dir_item cur_dir_item;
        for(int j = 0;j < cur_inode.size;j++){
          //printf("search in block %d...\n",cur_inode.block_point[j]); //test
          if(path_list_src[i+1]==NULL) //path_list_src[i] is a file name
            cur_dir_item = find_diritem_from_db(cur_inode.block_point[j],path_list_src[i],TYPE_FILE);
          else //path_list_src[i] is a dir name
            cur_dir_item = find_diritem_from_db(cur_inode.block_point[j],path_list_src[i],TYPE_DIR);
          if(cur_dir_item.valid==1){
            dir_item_isfind = 1;
            break;
          }
        }
        if(dir_item_isfind == 1){
          //the dir is exist
          cur_inode_id = cur_dir_item.inode_id; //update current inode id to next inode id
          //printf("%s is already exist,inode = %d\n",path_list_src[i],cur_inode_id); //test
        }
        else{
          printf("%s is not exist\n",path_list_src[i]);
          return;
        }
      }
      uint32_t new_inode_id = touch(path_list_des);  //build path_des
      if(new_inode_id != 0)
        copy_file(cur_inode_id,new_inode_id);
      else
        printf("copy failed\n");
}

void update_super_block(){  //save super block before leaving
  char super_block_buf[DEVICE_BLOCK_SIZE*2];
  memcpy(super_block_buf,&my_super_block,sizeof(super_block_buf));
  if(write_data_block(0,super_block_buf)==0)
    printf("save super block\n");
  else printf("cannot save super block\n");
}

int main(int argc, char* argv[]){
    char buf[512]; //读入命令的缓冲区
    if(open_disk()==0) 
      printf("disk open\n"); //打开磁盘
    else {
        printf("cannot open file\n");
        return 0;
    }
    read_super(); //read super block
    while(getcmd(buf, sizeof(buf)) >= 0){ //shell begin
        char * command_words[512];
        split_command(buf,command_words);
        if(strcmp(buf,"ls")==0){ //$ls path //list all dir and file in path
            //列出目录下的内容
            char * path_list[MAX_PATH_DEPTH];
            if(command_words[1] == NULL){ //if path is null,list root dir
              path_list[0] = NULL;
              ls(path_list);
            }
            else if(split_path(command_words[1],path_list)==0)
              ls(path_list);
        }
        else if(strcmp(buf,"mkdir")==0){ //$mkdir path //make a new dir
            char * path_list[MAX_PATH_DEPTH];
            if(command_words[1] == NULL) //if path is null
              printf("error:please enter a path\n");
            else if(split_path(command_words[1],path_list)==0)
              mkdir(path_list);
        }
        else if(strcmp(buf,"touch")==0){ //$touch path //make a new file
            char * path_list[MAX_PATH_DEPTH];
            if(command_words[1] == NULL) //if path is null
              printf("error:please enter a path\n");
            else if(split_path(command_words[1],path_list)==0)
              touch(path_list); 
        }
        else if(strcmp(buf,"cp")==0){  //$cp path_srt path_des //copy file from path_srt to path_des
            char * path_list_src[MAX_PATH_DEPTH];
            char * path_list_des[MAX_PATH_DEPTH];
            if(command_words[1] == NULL || command_words[2] == NULL) //if path is null
              printf("error:please enter two paths\n");
            else if(split_path(command_words[1],path_list_src)==0 && split_path(command_words[2],path_list_des)==0)
              cp(path_list_src,path_list_des);
        }
        else if(strcmp(buf,"shutdown")==0){ //$shutdown //close file system
            update_super_block();
            printf("Bye Bye\n");
            close_disk();
            exit(0);
        }
        else
            printf("unknown command\n");
        memset(buf,'\0',sizeof(buf));  //clear command buf
    }
    return 0;
}