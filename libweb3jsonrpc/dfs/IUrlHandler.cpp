#include "IUrlHandler.h"
#include "microhttpd.h"
#include <string.h>
#include "DfsCommon.h"
#include <stdio.h>

using namespace dev::rpc::fs;


int IUrlHandler::send_page(struct MHD_Connection *connection,
           const char *json,
           int status_code)
{
  int ret;
  struct MHD_Response *response;

  response = MHD_create_response_from_buffer(strlen (json),
                                     (void *) json,
                                     MHD_RESPMEM_MUST_COPY);
  if (!response)
  {
 	dfs_warn << "cannot create valid response !";
    return MHD_NO;
  }

  MHD_add_response_header(response,
                           MHD_HTTP_HEADER_CONTENT_TYPE,
                           "application/json");
  ret = MHD_queue_response(connection,
                            status_code,
                            response);
  MHD_destroy_response(response);

  return ret;
}


bool IUrlHandler::findShortestAsFile(const string& dir, const string& file_id, string& full_file)
{
	//find the file
	vector<string> files;
	vector<string> filesFound;
	listAllFiles(dir, files);
	for (std::vector<string>::iterator file = files.begin(); file != files.end(); ++file)
	{
		size_t pos = file->find(file_id);
		if (pos != string::npos && pos == 0)//begin with fileid
		{
			filesFound.push_back(*file);
		}
	}

	if (filesFound.empty())
	{
		dfs_warn << "file id: " << file_id.data() << " not found \n";
		return false;
	}

	string strFileFound = filesFound.front();
	size_t length = filesFound.front().size();
	for (std::vector<string>::iterator f = filesFound.begin(); f != filesFound.end(); ++f)
	{
		if (length > f->size())//get the shortest one
		{
			length = f->size();
			strFileFound = *f;
		}
	}

	full_file = dir;
	full_file += "/";
	full_file += strFileFound;
	return true;
}
