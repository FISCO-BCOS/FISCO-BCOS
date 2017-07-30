#include "DfsUploadHandler.h"

#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <microhttpd.h>
#include <json/json.h>

#include "DfsMd5.h"
#include "DfsCommon.h"
#include "DfsConst.h"
#include "DfsJsonUtils.h"
#include "DfsFileServer.h"
#include "DfsFileRecorder.h"
#include "DfsMd5.h"



#define POSTBUFFERSIZE  1024
#define MAXCLIENTS      100


using namespace dev::rpc::fs;



static unsigned int nr_of_uploading_clients = 0;

static int
iterate_post (void *coninfo_cls,
              enum MHD_ValueKind kind,
              const char *key,
              const char *filename,
              const char *content_type,
              const char *transfer_encoding,
              const char *data,
              uint64_t off,
              size_t size);

void DfsUploadHandler::handle_request_completed(void *cls,
                   struct MHD_Connection *connection,
                   void **con_cls,
                   enum MHD_RequestTerminationCode toe)
{
    (void)cls;
    (void)toe;
    DfsConnectionInfo *con_info = (DfsConnectionInfo*)*con_cls;

    if (NULL == con_info)
        return;

    if (0 == strcmp(con_info->method.data(), JZ_HTTP_METHOD_POST))
    {
        if (NULL != con_info->postprocessor)
        {
            MHD_destroy_post_processor (con_info->postprocessor);
            nr_of_uploading_clients--;
        }

        if (con_info->fp)
        {
          fclose(con_info->fp);
          con_info->fp = NULL;
          
          string strFile;
          string strFileBak;
          string strFileDir;
          string strFirst;
          string strSecond;
          DfsFileServer::getInstance()->createDirectoryStringByContainer(con_info->container, strFileDir, strFirst, strSecond);
          createDfsFileNames(strFileDir, con_info->file_id, con_info->filename, strFile, strFileBak);
          
          //file copy from temp to real store path
          dfs_debug << "***** file copy, src: " << con_info->fp_path.data() <<", dst: " <<strFile.data() <<"\n";
          FileCpy(con_info->fp_path, strFile);
          FileRm(con_info->fp_path);
        }

        if (con_info->data)
        {
          delete []con_info->data;
          con_info->data = NULL;
        }
    }

    if (con_info->recv_bytes == 0)
    {
      dfs_debug << "upload interupt, response error, may already exist fileid: " << con_info->file_id.data();
      string strjson = DfsJsonUtils::createCommonRsp(MHD_HTTP_BAD_REQUEST, \
      -1, "request handle error");
      send_page (connection,
                        strjson.data(),
                        MHD_HTTP_BAD_REQUEST);
    }
    else
    {
      //检查文件是否存在
      string strUpDir;
      createDirectoryByContainer(DfsFileServer::getInstance()->m_StoreRootPath, con_info->container, strUpDir);

      dfs_debug << "## upload has completed ! recv bytes:  " << con_info->recv_bytes;
      DfsFileTask objTask;
      objTask.id = con_info->file_id;
      objTask.filename = con_info->filename;
      objTask.operation = DfsFileTask::UPLOAD;
      objTask.directory = strUpDir;
      if (0 != DfsFileRecorder::writeRecord(objTask))
      {
        dfs_warn << "## write file update record failed !!";
      }
    } 
}


DfsUploadHandler::DfsUploadHandler(const string &version, const string &method) :
    IUrlHandler(version, method)
{
}

DfsUploadHandler::~DfsUploadHandler()
{
}

int DfsUploadHandler::handle(DfsHttpInfo *http_info, DfsUrlInfo *url_info)
{
  string strjson;
  if (0 != strcmp(http_info->method, MHD_HTTP_METHOD_POST) \
    || url_info->container_id.empty() || url_info->file_id.empty()) 
  {
    dfs_warn << "bad method (" << http_info->method <<") or url: " << http_info->url << ", should be with container and fileId\n";
    strjson = DfsJsonUtils::createCommonRsp(MHD_HTTP_BAD_REQUEST, \
      MHD_HTTP_BAD_REQUEST, "bad method or url request without fileid");
    
    //consume data
    *(http_info->upload_data_size) = 0;

    return send_page (http_info->connection,
                      strjson.data(),
                      MHD_HTTP_BAD_REQUEST);
  }

  dfs_debug << "connection request comes... url=" << http_info->url << ", upload_data_size=" << *(http_info->upload_data_size);
  
  MHD_Connection* connection = http_info->connection;

  DfsConnectionInfo *con_info = NULL;

  if (NULL == *(http_info->ptr))//first time a request is null for ptr
  {
    if (nr_of_uploading_clients > MAXCLIENTS)
    {
      strjson = DfsJsonUtils::createCommonRsp(MHD_HTTP_SERVICE_UNAVAILABLE, \
        -1, "too many upload request");
      return send_page (connection,
                          strjson.data(),
                          MHD_HTTP_SERVICE_UNAVAILABLE);
    }

    con_info = new DfsConnectionInfo;
    con_info->method = JZ_HTTP_METHOD_POST;
    con_info->version = JZ_MODULE_VERSION_V1;
    con_info->fp = NULL;
    con_info->fp_path = "";
    con_info->postprocessor = MHD_create_post_processor(connection,
                                       POSTBUFFERSIZE,
                                       &iterate_post,
                                       (void *) con_info);
    if (NULL == con_info->postprocessor)
    {
      dfs_warn << "cannot create the postprocessor, method= " << http_info->method;
      delete (con_info);
      strjson = DfsJsonUtils::createCommonRsp(MHD_HTTP_BAD_REQUEST, \
      -1, "request handle error");
      return send_page (connection,
                        strjson.data(),
                        MHD_HTTP_BAD_REQUEST);
    }

    nr_of_uploading_clients++;
    con_info->recv_bytes = 0;
    con_info->file_id = url_info->file_id;
    con_info->container = url_info->container_id;
    con_info->data = NULL;
    con_info->length = 0;

    *(http_info->ptr) = (void *) con_info;

    //consume data
    *(http_info->upload_data_size) = 0;

    return MHD_YES;
  }

  
  con_info = (DfsConnectionInfo*)(*(http_info->ptr));

  if (0 != *(http_info->upload_data_size))
  {
    dfs_warn << "********* do post process *******\n";
    //do post process
    if (MHD_post_process (con_info->postprocessor,
                          http_info->upload_data,
                          *(http_info->upload_data_size)) != MHD_YES)
    {
      dfs_warn << "cannot DO POST process " << http_info->url;
      strjson = DfsJsonUtils::createCommonRsp(MHD_HTTP_BAD_REQUEST, -1, "Error processing POST data");
      return send_page (connection,
                        strjson.data(),
                        MHD_HTTP_BAD_REQUEST);
    }
    *(http_info->upload_data_size) = 0;

    return MHD_YES;
  }
  
  dfs_debug << "a request of upload file just done, url: " << http_info->url;
  //get the uploaded file md5 hash
  string strHash;
  string strFile;
  string strFileBak;
  string strFileDir;
  string strFirst;
  string strSecond;
  DfsFileServer::getInstance()->createDirectoryStringByContainer(con_info->container, strFileDir, strFirst, strSecond);
  createDfsFileNames(strFileDir, con_info->file_id, con_info->filename, strFile, strFileBak);
  if (0 != ms_md5(strFile, strHash))
  {
    dfs_warn << "file: " << strFile.data() << " md5 failed, inner error";
    strjson = DfsJsonUtils::createCommonRsp(MHD_HTTP_BAD_REQUEST, -1, "the upload has error with file");
    return IUrlHandler::send_page(http_info->connection, strjson.data(), MHD_HTTP_BAD_REQUEST);
  }

  dfs_debug << "upload file OK ! fileid: " << con_info->file_id << ", hash: " << strHash;
  strjson = DfsJsonUtils::createCommonRsp(MHD_HTTP_OK, 0, "the upload has been completed", strHash);
  return IUrlHandler::send_page(http_info->connection, strjson.data(), MHD_HTTP_OK);
}


static int
iterate_post (void *coninfo_cls,
              enum MHD_ValueKind kind,
              const char *key,
              const char *filename,
              const char *content_type,
              const char *transfer_encoding,
              const char *data,
              uint64_t off,
              size_t size)
{
  (void)kind;
  (void)content_type;
  (void)transfer_encoding;
  (void)off;

    //TODO: (void)transfer_encoding; 
    DfsConnectionInfo *con_info = (DfsConnectionInfo*)coninfo_cls;
    
    dfs_debug << "*****== recv file: " << filename << ", key: " << key << ", size: " << size;
   
    if (filename == NULL)
    {
      //NOTE: if we upload file with file info  [not used now]
      if (strcmp(key, JZ_UPLOAD_FILE_INFO_FIELD) == 0)//get the json
      {
        dfs_debug << "*** recv fileinfo json: "<< data;
        con_info->data = new char[size+1];
        con_info->length = size;
        memcpy(con_info->data, data, size);
        return MHD_YES;
      }
      else
      {
        dfs_debug << "****** ERROR: the filename is null\n";
        return MHD_NO;
      }
    }

    con_info->filename = filename;
    if (!con_info->fp)
    {
        string strFile;
        string strFileBak;
        string strFileDir;
        string strFirst;
        string strSecond;
        DfsFileServer::getInstance()->createDirectoryStringByContainer(con_info->container, strFileDir, strFirst, strSecond);
        createDfsFileNames(strFileDir, con_info->file_id, con_info->filename, strFile, strFileBak);
        if (ChkFileExists(strFile))
        {
          dfs_debug << "*** file already exists, file: " << strFile.c_str();
          con_info->recv_bytes = 0;
          return MHD_NO;
          
          /*
          //当前文件存在则中断文件上传
          printf("**** file exist : %s\n", strFile.data());
          printf("== alter file or dup upload file id: %s, name: %s, but ok \n", \
                con_info->file_id.data(), con_info->filename.data());
          //mv file
          FileMv(strFile, strFileBak);
                     
          time_t theTime = time(NULL);
          theTime += 2;
          char szTemp[256];
          snprintf(szTemp, 256, "%ld", theTime);
          strFileBak = strFile;
          strFileBak += "_";
          strFileBak += szTemp;*/

        }

        //temp download the file
        con_info->fp_path = DfsFileServer::getInstance()->m_TempStorePath;
        con_info->fp_path += "/";
        con_info->fp_path += con_info->file_id;
        con_info->fp_path += "_";
        con_info->fp_path += con_info->filename;
        dfs_debug << "to save files: " << strFile.c_str() << ", temp: " << con_info->fp_path.c_str();
        con_info->fp = fopen (strFile.c_str(), "wb");
        if (!con_info->fp)
        {
          dfs_debug << "upload file, to save temp file failed, file: " << strFileBak.c_str();
          return MHD_NO;
        }

        //记录文件开始上传时间
        string strUpDir;
        createDirectoryByContainer(DfsFileServer::getInstance()->m_StoreRootPath, con_info->container, strUpDir);

        DfsFileTask objTask;
        objTask.id = con_info->file_id;
        objTask.filename = con_info->filename;
        objTask.operation = DfsFileTask::UPLOAD_START;
        objTask.directory = strUpDir;
        if (0 != DfsFileRecorder::writeRecord(objTask))
        {
          dfs_warn << "## write file update record failed !!";
        }
    }

    if (size > 0)
    {
      size_t ret = 0;
      if (!(ret = fwrite(data, sizeof(char), size, con_info->fp)))
      {
          dfs_warn << "write to disk failed, fileid: " << con_info->file_id.data();
          return MHD_NO;
      }

      con_info->recv_bytes += size;
    }
    else
    {
      dfs_debug << "****** post iterator, size: " << (int)size;
    }

    return MHD_YES;
}


