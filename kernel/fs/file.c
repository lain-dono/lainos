//
// File descriptors
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "file.h"
#include "spinlock.h"
#include "stat.h"
#include "fcntl.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  fs_node_t file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
fs_node_t*
filealloc(void)
{
  fs_node_t *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      memset(f, 0, sizeof(fs_node_t));
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

fs_node_t*
fileopen(char *path, int omode)
{
  fs_node_t *f;

  if((f = filealloc()) == 0)
    return 0;

  f->type = FD_INODE;
  f->offset = 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if(omode & O_CREATE){
    f->ip = sfs_createi_file(path);
    if(f->ip == 0) {
      fileclose(f);
      return 0;
    }
  } else {
    f->ip = sfs_openi(path, omode);
    if(f->ip == 0) {
      fileclose(f);
      return 0;
    }
  }

  return f;
}

// Increment ref count for file f.
fs_node_t*
filedup(fs_node_t *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(fs_node_t *f)
{
  fs_node_t ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.close) {
    ff.close(&ff);
    return;
  }

  if(ff.type == FD_PIPE)
    pipeclose(ff.pipe, ff.writable);
  else if(ff.type == FD_INODE){
    sfs_closei(ff.ip);
  }
}

// Get metadata about file f.
int
filestat(fs_node_t *f, struct stat *st)
{
  if(f->type == FD_INODE){
    sfs_stati(f->ip, st);
    return 0;
  }
  return -1;
}


uint32_t
sfs_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);

uint32_t
sfs_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);

// Read from file f.
int
fileread(fs_node_t *f, char *addr, int n)
{
  if(f->readable == 0)
    return -1;

  if(f->read) {
    return f->read(f, f->offset, n, (void*)addr);
  }

  if(f->type == FD_PIPE)
    return piperead(f->pipe, addr, n);
  if(f->type == FD_INODE){
    return sfs_read(f, f->offset, n, (void*)addr);
    //if((r = sfs_readi(f->ip, addr, f->offset, n)) > 0)
      //f->offset += r;
  }
  panic("fileread");
}

// Write to file f.
int
filewrite(fs_node_t *f, char *addr, int n)
{
  if(f->writable == 0)
    return -1;

  if(f->write) {
    return f->write(f, f->offset, n, (void*)addr);
  }

  if(f->type == FD_PIPE)
    return pipewrite(f->pipe, addr, n);
  if(f->type == FD_INODE){
    return sfs_write(f, f->offset, n, (void*)addr);
    //if ((r = sfs_writei(f->ip, addr, f->offset, n)) > 0)
      //f->offset += r;
  }
  panic("filewrite");
}

