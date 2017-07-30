/**  
 * @Title: JuError.java

 * @date: 2017年3月7日 上午10:25:51

 * @version: V1.0  

 */
package com.dfs.entity;


/**
 * @author Administrator
 *
 */
public class DfsConst {
	public static final String 	HTTP_STARTER = "http://";
	public static final int 	HTTP_JSONRPC_PORT_DEFAULT = 6789;
	public static final String	HTTP_FS_MODULE	= "fs";
	public static final String	HTTP_FS_VERSION	= "v1";
	public static final String	HTTP_FS_CONTAINER_ID_DEFAULT = "files";
	
	public static final int		JU_REQUEST_TIMEOUT = 10;
	//error
	public static final int DFS_ERROR_OK = 0;
	public static final int DFS_ERROR_BAD = -1;
	
	public static final int DFS_ERROR_SUCCESS = 200;
	public static final int DFS_ERROR_FAIL = 400;
	public static final int DFS_ERROR_FILE_NOT_FIND = 404;
	public static final int DFS_ERROR_NOT_INITIALIZED = 405;
	public static final int DFS_ERROR_BAD_PARAMETER = 406;
	public static final int DFS_ERROR_NULL_OBJECT = 407;
	public static final int DFS_ERROR_NETWORK_EXCEPTION = 408;
	
	
}
