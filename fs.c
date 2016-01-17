#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#define DATA_BLOCK_SIZE 2048
#define MAX_DATABLOCKS_COUNT 1024
#define MAX_FILES_COUNT 32
#define FILENAME_LENGTH 128
#define PATH_TO_LLFSFILE "currentPath"
#define INITIAL_EMPTY_NUM -2

typedef struct fileDes {
    char name[FILENAME_LENGTH];
    int isFolder;
    int isFill;
    int startingDataBlock;
    int dataSize;
} fileDes;

int nextBlockDict[MAX_DATABLOCKS_COUNT];
fileDes fds[MAX_FILES_COUNT];

void init() {
    int i = 0;

    int num = sizeof(fds) / sizeof(fds[0]);
    while (i < num) {
        memset(fds[i].name, '\0', FILENAME_LENGTH);
        fds[i].isFolder = 0;
        fds[i].isFill = 0;
        fds[i].startingDataBlock = -1;
        fds[i].dataSize = 0;
        i++;
    }

    int j = 0;
    while (j < MAX_DATABLOCKS_COUNT) {
        nextBlockDict[j] = INITIAL_EMPTY_NUM;
        j++;
    }
}

int getEmptyFD() {
    int i = 0;
    int n = sizeof(fds) / sizeof(fds[0]);
    while (i < n) {
        if (!fds[i].isFill)
            return i;
        i++;
    }
    return -1;
}

int getEmptyDataBlock() {
    int i = 0;
    while (i < MAX_DATABLOCKS_COUNT) {
        if (nextBlockDict[i] == INITIAL_EMPTY_NUM)
            return i;
        i++;
    }
    return -1;
}

int writefileDes(int num) {
    FILE *f = fopen(PATH_TO_LLFSFILE, "r+");
    fseek(f, num * sizeof(fileDes), SEEK_SET);
    fwrite(&fds[num], sizeof(fileDes), 1, f);
    fclose(f);
    return 0;
}

int writeDataBlocks(fileDes* fd) {
    int i = fd->startingDataBlock;
    FILE *f = fopen(PATH_TO_LLFSFILE, "r+");

    while (i != -1) {
        fseek(f, sizeof(fileDes) * MAX_FILES_COUNT + sizeof(int) * i, SEEK_SET);
        fwrite(&nextBlockDict[i], sizeof(int), 1, f);
        i = nextBlockDict[i];
    }

    fclose(f);
    return 0;
}


int writeData(fileDes* fd, const char* data, int size, int offset) {
    if (size == 0) return 0;
    FILE *f = fopen(PATH_TO_LLFSFILE, "r+");

    int i = fd->startingDataBlock, j = offset / DATA_BLOCK_SIZE;
    while (j-- > 0) i = nextBlockDict[i];

    int count = 0;
    int recalcOffset = offset % DATA_BLOCK_SIZE;
    int border = size;

    int headers_offset = sizeof(fileDes) * MAX_FILES_COUNT + sizeof(int) * MAX_DATABLOCKS_COUNT;

    while (border > 0) {
        if (recalcOffset + border > DATA_BLOCK_SIZE)
            count = DATA_BLOCK_SIZE - recalcOffset;
        else
            count = border;

        fseek(f, headers_offset + i * DATA_BLOCK_SIZE + recalcOffset, SEEK_SET);
        fwrite(data + size - border, 1, count, f);

        if (recalcOffset + count == DATA_BLOCK_SIZE) {
            if (nextBlockDict[i] >= 0) {
                i = nextBlockDict[i];
            }
            else {
                int fdNum = getEmptyDataBlock();
                nextBlockDict[i] = fdNum;
                nextBlockDict[fdNum] = -1;
                i = fdNum;
            }
        }

        border = border - count;
        recalcOffset = 0;
    }

    fd->dataSize = size + offset;

    writeDataBlocks(fd);
    fclose(f);
    return size;
}

int readData(fileDes* fd, char** data) {
    if (fd == NULL) return -1;
    FILE *f = fopen(PATH_TO_LLFSFILE, "r");
    char *buf = NULL;
    int size = fd->dataSize;
    buf = (char *)malloc(size);

    int i = fd->startingDataBlock;
    int j = 0;
    int count;

    int headers_offset = sizeof(fileDes) * MAX_FILES_COUNT + sizeof(int) * MAX_DATABLOCKS_COUNT;
    while (i != -1) {
        int left = size - j * DATA_BLOCK_SIZE;
        if (left >= DATA_BLOCK_SIZE)
            count = DATA_BLOCK_SIZE;
        else
            count = left;
        fseek(f, headers_offset + i * DATA_BLOCK_SIZE, SEEK_SET);
        fread(buf + j * DATA_BLOCK_SIZE, 1, count, f);
        i = nextBlockDict[i];
        j++;
    }
    fclose(f);
    *data = buf;
    return size;

}

int getFDNum(char* data, char* name, int size) {
    int i = 0;
    int n = size / sizeof(int);
    while (i < n) {
        if (strcmp(fds[((int *)data)[i]].name, name) == 0)
            return ((int *)data)[i];
        i++;
    }
    return -1;
}

int getfileDes(const char* path, fileDes** fd) {
    char *fpath = (char*)malloc(strlen(path));
    strcpy(fpath, path);

    if (fpath && strcmp("/", fpath) == 0) {
        *fd = &fds[0];
        return 0;
    }

    fileDes *m = NULL;
    char *p;

    p = fpath;
    if (*p++ == '/')
        m = &fds[0];
    else return -1;
    char *data, *s;
    char name[FILENAME_LENGTH];
    memset(name, '\0', FILENAME_LENGTH);

    int fdNum = -1, size;
    while (p - fpath < strlen(fpath)) {
        if (m->dataSize == 0)
            return -1;
        size = readData(m, &data);
        s = p;
        p = strchr(p, '/');
        if (p != NULL) {
            p = p + 1;
            strncpy(name, s, p - s - 1);
        }
        else {
            strncpy(name, s, fpath + strlen(fpath) - s);
            p = fpath + strlen(fpath);
        }
        fdNum = getFDNum(data, name, size);
        if (fdNum == -1) return -1;
        m = &fds[fdNum];
        memset(name, '\0', FILENAME_LENGTH);
        free(data);
    }

    *fd = m;
    return fdNum;
}

int addFile(char* name, int size, int isFolder) {
    fileDes *fd = NULL;
    int fd_num = getEmptyFD();
    if (fd_num == -1) return -1;
    fd = &fds[fd_num];
    int start = getEmptyDataBlock();
    if (start == -1) return -1;

    strcpy(fd->name, name);
    fd->startingDataBlock = start;
    fd->dataSize = size;
    fd->isFolder = isFolder;
    fd->isFill = 1;

    nextBlockDict[start] = -1;
    writefileDes(fd_num);
    writeDataBlocks(fd);
    return fd_num;
}

char* getDirName(const char* path) {
    char* directory;
    char* p = NULL;
    char* ptr = (char*)malloc(sizeof(path));
    strcpy(ptr, path);

    while (p = *ptr == '/' ? ptr : p, *ptr++ != '\0');

    if ((p - path) != 0) {
        directory = (char*)malloc(sizeof(char) * (p - path));
        strncpy(directory, path, p - path);
        directory[p - path] = '\0';
    }
    else {
        directory = (char*)malloc(sizeof(char) * 2);
        strcpy(directory, "/\0");
    }
    return directory;
}

int deleteFile(const char* path) {
    fileDes *fileNode, *dirNode;
    char* data;
    char* preparedData;
    char *dir = getDirName(path);

    int dirNodeNum = getfileDes(dir, &dirNode);
    int fNodeNum = getfileDes(path, &fileNode);
    int size = readData(dirNode, &data);

    preparedData = (char *)malloc(size - sizeof(int));
    int i = 0, j = 0;
    while (i < size / sizeof(int)) {
        if (((int *)data)[i] != fNodeNum)
            ((int *) preparedData)[j++] = ((int *)data)[i];
        i++;
    }

    writeData(dirNode, preparedData, size, 0);
    dirNode->dataSize = size - sizeof(int);
    writefileDes(dirNodeNum);

    free(data);
    free(dir);

    return 0;
}

static void* llInit(struct fuse_conn_info* fi) {
    FILE* llfsFile = fopen(PATH_TO_LLFSFILE, "w+");

    char* buf = (char*)malloc(sizeof(fileDes));
    memset(buf, '\0', sizeof(fileDes));
    int i = 0;
    while (i < MAX_FILES_COUNT) {
        fwrite(buf, sizeof(fileDes), 1, llfsFile);
        i++;
    }
    free(buf);

    buf = (char*)malloc(sizeof(int));
    memset(buf, '\0', sizeof(int));
    i = 0;
    while (i < MAX_DATABLOCKS_COUNT) {
        fwrite(buf, sizeof(int), 1, llfsFile);
        i++;
    }
    free(buf);

    buf = (char*)malloc(DATA_BLOCK_SIZE);
    memset(buf, '\0', DATA_BLOCK_SIZE);
    i = 0;
    while (i < MAX_DATABLOCKS_COUNT) {
        fwrite(buf, DATA_BLOCK_SIZE, 1, llfsFile);
        i++;
    }
    free(buf);

    init();
    addFile("/", 0, 1);
    fclose(llfsFile);
    return 0;
}

static int llGetattr(const char* path, struct stat* stbuf) {
    int res = 0;

    fileDes *fd;
    if (getfileDes(path, &fd) == -1)
        res = -ENOENT;

    memset(stbuf, 0, sizeof(struct stat));

    if(fd->isFolder) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    }
    else {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = fd->dataSize;
    }
    stbuf->st_mode = stbuf->st_mode | 0777;

    return res;
}

static int llCreate(const char* path, mode_t mode, struct fuse_file_info* finfo) {
    fileDes *fd;
    char* dir;
    char* name;
    char* data;

    name = strrchr(path, '/');
    if (name == NULL) {
        strcpy(name, path);
        dir = (char *)malloc(2);
    } else {
        name++;
        int len = strlen(path) - strlen(name) ;
        dir = (char *)malloc(len + 1);
        strncpy(dir, path, len);
        dir[len] = '\0';
    }
    int nodeNum = getfileDes(dir, &fd);
    int size = readData(fd, &data);

    char* preparedData = (char*)malloc(size + sizeof(int));
    memcpy(preparedData, data, size);

    int fdNum = addFile(name, 0, 0);
    ((int*) preparedData)[size/sizeof(int)] = fdNum;

    writeData(fd, preparedData, size + sizeof(int), 0);
    fd->dataSize = size + sizeof(int);
    writefileDes(nodeNum);

    free(preparedData);
    free(dir);
    return 0;
}

static int llReaddir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi) {
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    fileDes *fd;
    int nodeNum = getfileDes(path, &fd);
    if (nodeNum == -1) return -ENOENT;

    char *data;
    int size = readData(fd, &data);

    int i = 0, n = size/sizeof(int);
    while (i < n) {
        filler(buf, fds[((int*)data)[i]].name, NULL, 0);
        i++;
    }

    return 0;
}

static int llRead(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {

    if (size == 0) return 0;

    fileDes *fd;
    int nodeNum = getfileDes(path, &fd);
    if (nodeNum == -1) return -ENOENT;

    int s = fd->dataSize;
    if (offset > s) return 0;
    if (size < s) s = size;

    FILE *f = fopen(PATH_TO_LLFSFILE, "r");

    int i = fd->startingDataBlock, j = offset / DATA_BLOCK_SIZE;
    while (j-- > 0) i = nextBlockDict[i];

    int left = s;
    int count = 0;
    int off = offset % DATA_BLOCK_SIZE;

    char *data = (char *)malloc(s);

    int headers_offset = sizeof(fileDes) * MAX_FILES_COUNT + sizeof(int) * MAX_DATABLOCKS_COUNT;

    while (left > 0) {
        if (off + left > DATA_BLOCK_SIZE)
            count = DATA_BLOCK_SIZE - off;
        else
            count = left;

        fseek(f, headers_offset + i * DATA_BLOCK_SIZE + off, SEEK_SET);
        fread(data + s - left, 1, count, f);

        if (off + count == DATA_BLOCK_SIZE) {
            i = nextBlockDict[i];
        }

        left = left - count;
        off = 0;
    }

    memcpy(buf, data, s);
    fclose(f);
    return s;
}

static int llMkdir(const char* path, mode_t mode) {
    fileDes *fd;
    char* dir;
    char* name;
    char* data;

    name = strrchr(path, '/');
    if (name == NULL) {
        strcpy(name, path);
        dir = (char *)malloc(2);
    } else {
        name++;
        int len = strlen(path) - strlen(name) ;
        dir = (char *)malloc(len + 1);
        strncpy(dir, path, len);
        dir[len] = '\0';
    }
    int nodeNum = getfileDes(dir, &fd);
    int size = readData(fd, &data);

    char* preparedData = (char*)malloc(size + sizeof(int));
    memcpy(preparedData, data, size);

    int fdNum = addFile(name, 0, 1);
    ((int*) preparedData)[size/sizeof(int)] = fdNum;

    writeData(fd, preparedData, size + sizeof(int), 0);
    fd->dataSize = size + sizeof(int);
    writefileDes(nodeNum);

    free(preparedData);
    free(dir);
    return 0;
}

static int llOpen(const char* path, struct fuse_file_info* fi)
{
    fileDes *fd;
    getfileDes(path, &fd);
    //TODO
    return 0;
}

static int llWrite(const char* path, const char* buf, size_t nbytes, off_t offset, struct fuse_file_info* fi) {
    fileDes *fd;
    int nodeNum = getfileDes(path, &fd);
    if (nodeNum == -1) return -ENOENT;
    int s = writeData(fd, buf, nbytes, offset);
    if (s != -1) {
        writefileDes(nodeNum);
        return s;
    }
    return -1;
}

static int llUnlink(const char* path) {
    int res = deleteFile(path);
    if (res != 0) return -1;
    return 0;
}

static struct fuse_operations fs_oper = {
        .init		= llInit,
        .getattr	= llGetattr,
        .readdir	= llReaddir,
        .create		= llCreate,
        .open		= llOpen,
        .read		= llRead,
        .write		= llWrite,
        .unlink		= llUnlink,
        .mkdir      = llMkdir,
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &fs_oper, NULL);
}
