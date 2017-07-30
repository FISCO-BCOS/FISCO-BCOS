#include "DfsFileServer.h"
#include <stdio.h>
#include <string.h>
#include "DfsConst.h"
#include "DfsCommon.h"
#include "DfsJsonUtils.h"
#include "DfsUploadHandler.h"
#include "DfsDownloadHandler.h"
#include "DfsDeleteFileHandler.h"
#include "DfsModifyFileHandler.h"
#include "DfsMd5.h"
#include <unistd.h>



using namespace dev::rpc::fs;


DfsFileServer::DfsFileServer()
{
    m_StoreRootPath = "";
    m_NodeGroup = "";
    m_NodeId = "";
    m_Inited = false;
}

DfsFileServer::~DfsFileServer()
{
    if (m_Inited)
    {
        destory();
    }
}

static DfsFileServer *pInstance = NULL;
DfsFileServer *DfsFileServer::getInstance()
{
    if (pInstance == NULL)
    {
        static DfsFileServer objInstance;
        pInstance = &objInstance;
    }

    return pInstance;
}

void DfsFileServer::destory()
{
    m_DfsFileInfoScanner.stop();

    map<string, map<string, IUrlHandler *> >::iterator iter = m_HandlerMap.begin();
    for (; iter != m_HandlerMap.end(); ++iter)
    {
        map<string, IUrlHandler*>::iterator iter_handler = iter->second.begin();
        for (; iter_handler != iter->second.end(); ++iter_handler)
        {
            delete iter_handler->second;
        }
    }
    
    m_HandlerMap.clear();
    m_Containers.clear();
    m_Inited = false;
}

//initiate file storage repository tree
int DfsFileServer::init(const string& store_root, const string& group, const string& node, dev::eth::Client* ethClient)
{
    if (m_Inited)
    {
        LOG(ERROR) << "dfs already inited !";
        return 0;
    }

    if (store_root.empty() || group.empty() || node.empty())
    {
        LOG(ERROR) << "dfs init param invalid, null param exists !";
        return -1;
    }

    //relative path
    char szDir[4096] = {0};
    if (NULL == getcwd(szDir, 4096))
    {
    	LOG(ERROR) << "cannot getcwd, system error ";
		return -1;
    }
	
    if (store_root.empty() || group.empty() || node.empty())
    {
        LOG(ERROR) << "dfs storagePath, group, node is invalid !";
        return -1;
    }

    if (store_root[0] == '.' && store_root[1] == '/')
    {
        m_StoreRootPath = szDir;
        m_StoreRootPath += "/";
        m_StoreRootPath += store_root.substr(2);
    }
    else if (store_root[0] == '/')
    {
        m_StoreRootPath = store_root;
    }
    else
    {
        m_StoreRootPath = szDir;
        m_StoreRootPath += "/";
        m_StoreRootPath += store_root;
    }

    if (m_StoreRootPath[m_StoreRootPath.size()-1] == '/')
    {
        m_StoreRootPath.erase(m_StoreRootPath.size()-1);//caus we will add "/" later
    }
    
    m_NodeGroup = group;
    m_NodeId = node;

    m_Containers.clear();
    m_Containers.push_back(JZ_CONTAINER_FILES);

    dfs_debug << "****#### hey, init the fileserver! base: " << m_StoreRootPath.data() <<" group: " <<m_NodeGroup.data() <<" node: " <<m_NodeId.data();

    //initiate file storage repository tree
    init_storage(m_StoreRootPath);

    //register handlers
    registerHandler(JZ_MODULE_VERSION_V1, JZ_HTTP_METHOD_POST, new DfsUploadHandler(JZ_MODULE_VERSION_V1, JZ_HTTP_METHOD_POST));
    registerHandler(JZ_MODULE_VERSION_V1, JZ_HTTP_METHOD_GET, new DfsDownloadHandler(JZ_MODULE_VERSION_V1, JZ_HTTP_METHOD_GET));
    registerHandler(JZ_MODULE_VERSION_V1, JZ_HTTP_METHOD_DELETE, new DfsDeleteFileHandler(JZ_MODULE_VERSION_V1, JZ_HTTP_METHOD_DELETE));
    registerHandler(JZ_MODULE_VERSION_V1, JZ_HTTP_METHOD_PUT, new DfsModifyFileHandler(JZ_MODULE_VERSION_V1, JZ_HTTP_METHOD_PUT));

    init_caller_client(ethClient);

    //init scanner
    m_DfsFileInfoScanner.init(m_Containers, ethClient);

    m_Inited = true;
    return 0;
}

//register URL handler
int DfsFileServer::registerHandler(const string &version, const string &method, IUrlHandler *handler)
{
    map<string, map<string, IUrlHandler *> >::iterator iter = m_HandlerMap.find(version);
    if (iter == m_HandlerMap.end())
    {
        map<string, IUrlHandler*> URL_Handlers;
        m_HandlerMap.insert(std::pair<string, map<string, IUrlHandler*> >(version, URL_Handlers));

        iter = m_HandlerMap.find(version);
    }

    map<string, IUrlHandler*>::iterator iter_handler = iter->second.find(method);
    if (iter_handler != iter->second.end())
    {
        LOG(ERROR) << "http handler already exists, method: " <<method.data() << "version: " <<version.data();
        return -1;//already exists
    }

    iter->second.insert(std::pair<string, IUrlHandler*>(method, handler));
    return 0;
}

//filter ilegal request of file process request
int DfsFileServer::filter(const string &url, const string &method)
{
    DfsUrlInfo url_info;

    //parse the url line
    //module, version, container_id, file_id
    if (0 != parserURL(url, url_info))
    {
        return -1;
    }

    if (m_HandlerMap.find(url_info.version) == m_HandlerMap.end())
    {
        LOG(ERROR) << "cannot find the version:" <<url_info.version.data();
        return -1;
    }

    if (m_HandlerMap[url_info.version].find(method) == m_HandlerMap[url_info.version].end())
    {
        LOG(ERROR) << "not support the method: " <<method.data();
        return -1;
    }

    return 0;
}

//parse url
int DfsFileServer::parserURL(const string &url, DfsUrlInfo &url_info)
{
    string version;
    string module;
    string container_id;
    string file_id;

    string strUrl = url;
    if (strUrl[0] == '/')
    {
        strUrl = strUrl.substr(1);
    }

    //fs
    size_t pos = strUrl.find(JZ_MODULE_FS);
    if (0 != pos)
    {
        //LOG(ERROR) << "cannot find fs module, not file process request \n";
        return -1;
    }

    strUrl = strUrl.substr(strlen(JZ_MODULE_FS));
    if (strUrl.size() > 1 && strUrl[0] == '/')
    {
        strUrl = strUrl.substr(1);
    }
    else
    {
        LOG(ERROR) << "url not enough for version\n";
        return -1;
    }

    //version
    if (strUrl.find("/") == string::npos)
    {
        LOG(ERROR) << "url not enough, just has version\n";
        return -1;
    }
    version = strUrl.substr(0, strUrl.find("/"));
    if (m_HandlerMap.find(version) == m_HandlerMap.end())
    {
        LOG(ERROR) << "version:" << version.data() <<" not register, not support !\n";
        return -1;
    }

    //container_id [optional]
    strUrl = strUrl.substr(strUrl.find("/") + 1);
    if (strUrl.find("/") == string::npos)
    {
        url_info.module = module;
        url_info.version = version;
        LOG(ERROR) << "not find container: "<< url_info.container_id.data();
        return 0;
    }

    if (strUrl.find("/") == string::npos)
    {
        url_info.module = module;
        url_info.version = version;
        container_id = strUrl;
        url_info.container_id = container_id;
        return 0;
    }
    else
    {
        container_id = strUrl.substr(0, strUrl.find("/"));
    }


    //file_id [optional]
    file_id = strUrl.substr(strUrl.find("/") + 1);

    url_info.module = module;
    url_info.version = version;
    url_info.file_id = file_id;
    url_info.container_id = container_id;
    return 0;
}

//find the specfic handler
IUrlHandler * DfsFileServer::getHandler(const string &version, const string &method)
{
	if (m_HandlerMap.find(version) == m_HandlerMap.end())
    {
        LOG(ERROR) << "cannot find the version: " << version.data();
        return NULL;
    }

    if (m_HandlerMap[version].find(method) == m_HandlerMap[version].end())
    {
        LOG(ERROR) << "not support the method: " << method.data();
        return NULL;
    }

	return m_HandlerMap[version][method];
}

bool DfsFileServer::checkServiceAvailable()
{
    //check service enable state
    bool enable = false;
    if (0 != checkServiceNode(m_NodeId, enable))
    {
        LOG(ERROR) << "check service enable state failed ! nodeid : " << m_NodeId;
        return false;
    }

	return enable;
}

//handle upload, download, revise file, delete file
int DfsFileServer::handle(DfsHttpInfo *http_info)//http_info有内部handler释放
{
	//check if file service is enable for the server
	if (!checkServiceAvailable())
	{
		LOG(ERROR) << "###!!! file service of the nodeid: " << m_NodeId << " is disable !";
        string strJson = DfsJsonUtils::createCommonRsp(MHD_HTTP_SERVICE_UNAVAILABLE, MHD_HTTP_SERVICE_UNAVAILABLE, "service not available");
        return IUrlHandler::send_page(http_info->connection, strJson.data(), MHD_HTTP_SERVICE_UNAVAILABLE);
	}
	
    DfsUrlInfo url_info;
    if (0 != parserURL(string(http_info->url), url_info))
    {
        LOG(ERROR) << "bad request, url: " <<http_info->url <<" method:" <<http_info->method << "\n";

        string strJson = DfsJsonUtils::createCommonRsp(MHD_HTTP_BAD_REQUEST, MHD_HTTP_BAD_REQUEST, "bad request, check url or parameters");
        return IUrlHandler::send_page(http_info->connection, strJson.data(), MHD_HTTP_BAD_REQUEST);
    }

    map<string, IUrlHandler*>::iterator iterFind = m_HandlerMap[url_info.version].find(http_info->method);
    if (iterFind == m_HandlerMap[url_info.version].end())
    {
        LOG(ERROR) << "cannot find the handle for method: " <<http_info->method;

        string strJson = DfsJsonUtils::createCommonRsp(MHD_HTTP_METHOD_NOT_ALLOWED, MHD_HTTP_METHOD_NOT_ALLOWED, "bad method or request");
        return IUrlHandler::send_page(http_info->connection, strJson.data(), MHD_HTTP_METHOD_NOT_ALLOWED);
    }

    return iterFind->second->handle(http_info, &url_info);
}


//iniatate the storage
int DfsFileServer::init_storage(const string &store_root)
{
    dfs_debug << "##@@@ init_storage ###";
    m_TempStorePath = store_root;
    m_TempStorePath += "/";
    m_TempStorePath += "temp";

    if(!ChkDirExists(store_root.c_str()))
    {
        //create store_root directory
        if (!createFileDir(store_root))
        {
            LOG(ERROR) << "create base directory: " << store_root.data() << " failed !\n";
            return -1;
        }

        //create temp file store path
        if (!createFileDir(m_TempStorePath))
        {
            LOG(ERROR) << "create Temp file store directory: " << store_root.data() << " failed !\n";
            return -1;
        }
    }
    else 
    {
        if(ChkDirExists(m_TempStorePath.c_str()))
        {
            //clean all temp files
            std::vector<string> vecFiles;
            listAllFiles(m_TempStorePath, vecFiles, true);
            for (auto &file : vecFiles)
            {
                FileRm(file);
            }
            
        }
        else
        {
            //create temp file store path
            if (!createFileDir(m_TempStorePath))
            {
                LOG(ERROR) << "create Temp file store directory: " << store_root.data() << " failed !\n";
                return -1;
            }
        }
    }

    //calculate the result of "files"
    for (std::vector<string>::const_iterator container = m_Containers.begin(); container != m_Containers.end(); ++container)
    {
        dfs_debug << "####  create directory for container_id: " << container->data() <<"\n";
        string strDirFiles;
        string firstDir;
        string secondDir;
        createDirectoryStringByContainer(*container, strDirFiles, firstDir, secondDir);

        dfs_debug << "####  create directory for container_id: " << container->data() <<",  full directory: " << strDirFiles.data();

        //first
        if (!ChkDirExists(firstDir))
        {
            if (!createFileDir(firstDir))
            {
                LOG(ERROR) << "create directory: " << firstDir.c_str() <<" failed\n";
                return -1;
            }

            dfs_debug << "create first storage files directory: " << firstDir.data();
        }

        //second///////////////
        if (!ChkDirExists(secondDir))
        {
            if (!createFileDir(secondDir))
            {
                LOG(ERROR) << "create directory: " << secondDir.c_str() <<" failed\n";
                return -1;
            }

            dfs_debug << "create second storage files directory: " << secondDir.data();
        }

        if (!ChkDirExists(strDirFiles))
        {
            if (!createFileDir(strDirFiles))
            {
                LOG(ERROR) << "create directory: " << strDirFiles.c_str() <<" failed\n";
                return -1;
            }

            dfs_debug << "create storage files directory: " << strDirFiles.data();
        }
    }
    

    return 0;
}


//create directory string by container id
void DfsFileServer::createDirectoryStringByContainer(const string &container, string &directory, string& first, string& second)
{
    directory.clear();
    if (container.empty())
    {
        LOG(ERROR) << "the container is null \n";
        return;
    }

    MD5_CTX ctx;
    unsigned char szResult[16] = {0};
    memset(szResult, 0, 16);
    ms_MD5_Init(&ctx);
    ms_MD5_Update(&ctx, container.data(), container.size());
    ms_MD5_Final(szResult, &ctx);
    unsigned char lowPart = szResult[0];
    unsigned char hignPart = szResult[8];

    char szDirAppend[256] = {0};
    char szFirst[256] = {0};
    char szSecond[256] = {0};
    memset(szDirAppend, 0, 256);
    snprintf(szDirAppend, 256, "%02X/%02X/%s", lowPart, hignPart, container.data());
    snprintf(szFirst, 256, "%02X", lowPart);
    snprintf(szSecond, 256, "%02X/%02X", lowPart, hignPart);


    directory = m_StoreRootPath;
    directory += "/";
    directory += szDirAppend;

    first = m_StoreRootPath;
    first += "/";
    first += szFirst;

    second = m_StoreRootPath;
    second += "/";
    second += szSecond;
}


void DfsFileServer::request_completed (void *cls,
        struct MHD_Connection *connection,
        void **con_cls,
        enum MHD_RequestTerminationCode toe)
{
    DfsConnectionInfo *con_info = (DfsConnectionInfo*)(*con_cls);
    if (NULL == con_info)
    {
    	return;
    }

    IUrlHandler *pHandler = DfsFileServer::getInstance()->getHandler(con_info->version, con_info->method);
	if (pHandler == NULL)
	{
		LOG(ERROR) << "cannot get dfs request handler !";
		return;
	}
	
    pHandler->handle_request_completed(cls, connection, con_cls, toe);

    delete con_info;
    *con_cls = NULL;
}
