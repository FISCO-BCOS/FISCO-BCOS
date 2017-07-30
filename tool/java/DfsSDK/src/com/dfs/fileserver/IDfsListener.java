/**  
 * @brief 回调接口类
 * @Title: IJuListener.java

 * @date: 2017年3月2日 下午4:43:28

 * @version: V1.0  

 */
package com.dfs.fileserver;

import java.util.Vector;

import com.dfs.message.FileInfo;
import com.dfs.message.FileServer;


/**
 * @brief 回调接口类
 *
 */
public interface IDfsListener {
	/**
	 * @description 下载请求回调
	 * @param ret 返回值
	 * @param fileinfo 文件信息
	 */
	public void downloadFileCallback(int ret, FileInfo fileinfo);
	
	/**
	 * @description 上传请求回调
	 * @param ret 返回值
	 * @param fileinfo 文件信息
	 */
	public void uploadFileCallback(int ret, FileInfo fileinfo);
	
	/**
	 * @description 删除文件请求回调
	 * @param ret
	 * @param fileinfo
	 */
	public void deleteFileCallback(int ret, FileInfo fileinfo);
	
	/**
	 * @description 列出所有文件信息请求回调
	 * @param ret 返回值
	 * @param fileinfos 文件信息列表
	 */
	public void listFileinfoCallback(int ret, Vector<FileInfo> fileinfoList);
	
	/**
	 * @description 列出所有文件信息请求回调
	 * @param ret 返回值
	 * @param fileinfoList 文件信息列表
	 */
	public void listFileinfoByGroupCallback(int ret, Vector<FileInfo> fileinfoList);
	
	/**
	 * @description 查找文件信息请求回调
	 * @param ret 返回值
	 * @param fileinfo 文件信息
	 */
	public void findFileinfoCallback(int ret, FileInfo fileinfo);
	
	/**
	 * @description 列出服务器请求回调
	 * @param ret 返回值
	 * @param fileServerList 服务器信息列表
	 */
	public void listServersCallback(int ret, Vector<FileServer> fileServerList);
	
	/**
	 * @description 按组列出服务器请求回调
	 * @param ret 返回值
	 * @param fileServerList 服务器信息列表
	 */
	public void listServersByGroupCallback(int ret, Vector<FileServer> fileServerList);
	
	/**
	 * @description 查找服务器请求回调
	 * @param ret 返回值
	 * @param fileServer 服务器信息
	 */
	public void findServerCallback(int ret, FileServer fileServer);
	
	/**
	 * @description 下载请求回调
	 * @param ret 返回值
	 * @param server 文件信息
	 */
	public void addServerCallback(int ret, FileServer server);
	
	/**
	 * @description 删除服务节点请求回调
	 * @param ret 返回值
	 * @param server 服务i节点信息
	 */
	public void deleteServerCallback(int ret, FileServer server);
	
	/**
	 * @description 下载请求回调
	 * @param ret 返回值
	 * @param serverID 服务节点ID
	 */
	public void enableServerCallback(int ret, String serverID);
	
	
	/**
	 * @description 错误请求回调
	 * @param code 错误码
	 * @param error 错误信息
	 */
	public void errorCallback(int code, String error);
	
	
}
