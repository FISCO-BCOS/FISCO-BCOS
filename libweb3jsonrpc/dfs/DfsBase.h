#pragma once

#include <string>
#include <stdio.h>
#include "microhttpd.h"
#include <libdevcore/Log.h>

#define dfs_debug (LOG(DEBUG))
#define dfs_warn  (LOG(ERROR))
#define dfs_trace  (LOG(TRACE))
#define dfs_error (LOG(ERROR))


using std::string;

namespace dev
{

namespace rpc
{

namespace fs
{

typedef struct DfsHttpInfo
{
	void *cls;
	struct MHD_Connection *connection;
    char *url;
    char *method;
    char *version;
    char *upload_data;
    size_t *upload_data_size;
    void **ptr;
} DfsHttpInfo;

typedef struct DfsUrlInfo {
    string  method;
	string  module;//must be "fs" now
	string 	version;
	string 	container_id;
	string 	file_id;
} DfsUrlInfo;

typedef struct DfsFileInfo {
	string      id;
    string      container;
    string      filename;
    string      filehash;
    string      owner;
    string      src_node;//server node id
    string      node_group;
    int         Type;
    string      priviliges;
    string      info;
    int         store_flag;

    DfsFileInfo() : \
    id(""),container(""),filename(""),filehash(""),\
    owner(""),src_node(""),node_group(""),\
    Type(0),priviliges(""),info(""),store_flag(0)
    {};

} DfsFileInfo;


typedef struct DfsFileTask {
    enum TaskOperation
    {
        UPLOAD_START = 0,
        UPLOAD = 1,
        DELETE = 2,
        MODIFY = 3,
        DOWNLOAD_START = 4,
        DOWNLOAD = 5,
        DOWNLOAD_FAIL = 6
    };

    string      id;
    string      container;
    string      filename;
    string      filehash;
    string      owner;
    string      src_node;//server node id
    string      node_group;
    string      host;
    int         port;
    string      directory;
    int         operation;//TaskOperation
} DfsFileTask;

typedef struct DfsDownRecord {
    string      id;
    string      container;
    string      filename;
    string      directory;
    string      src_node;//server node id
    string      node_group;
} DfsDownRecord;

typedef struct DfsConnectionInfo
{
    struct MHD_PostProcessor *postprocessor;
    string method;
    string version;
    FILE *fp;
    string fp_path;
    int   recv_bytes;
    string file_id;
    string filename;
    string container;

    char *data;
    int  length;

    DfsConnectionInfo(){
        postprocessor = NULL;
        method = "";
        version = "";
        fp = NULL;
        fp_path = "";
        recv_bytes = 0;
        file_id = "";
        filename = "";
        container = "";
        data = NULL;
        length = 0;
    };
} DfsConnectionInfo;

}
}
}