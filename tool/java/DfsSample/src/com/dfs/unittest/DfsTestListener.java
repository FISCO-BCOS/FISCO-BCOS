/**  
 * @Title: JuTestListener.java

 * @date: 2017年3月7日 下午6:19:36

 * @version: V1.0  

 */
package com.dfs.unittest;

import java.util.Calendar;
import java.util.Vector;
import java.util.concurrent.locks.ReentrantLock;
import com.dfs.fileserver.IDfsListener;
import com.dfs.message.FileInfo;
import com.dfs.message.FileServer;

/**
 * @author Administrator
 *
 */
public class DfsTestListener implements IDfsListener{
	public DfsEngineTest client;
	public Vector<FileServer>  fileservers;
	public volatile int upload_file_count = 0;
	public ReentrantLock lock  = new ReentrantLock();
	
	public long Timeout = 0;
	
	public DfsTestListener() {
		Timeout = Calendar.getInstance().getTimeInMillis();
	}
	
	public void updateTimeout() {
		Timeout = Calendar.getInstance().getTimeInMillis();
	}
	
	@Override
	public void downloadFileCallback(int ret, FileInfo fileinfo) {
		// TODO Auto-generated method stub
		Calendar calendar = Calendar.getInstance();
		long costTime = calendar.getTimeInMillis() - Timeout;
		Timeout = Calendar.getInstance().getTimeInMillis();		
		System.out.println("download_file_back, the ret: " + ret + ", info: " + fileinfo.getId() + ", cost time: " + costTime);
	}

	@Override
	public void uploadFileCallback(int ret, FileInfo fileinfo) {
		long endTime = Calendar.getInstance().getTimeInMillis();
		long costTime = endTime - Timeout;
		System.out.println("upload_file_back, the ret: " + ret + ", info: " + fileinfo.getId() + ", upload id: " + (upload_file_count+1) + ", cost time: " + costTime + ", start: " + Timeout + ", end: " +  endTime);
		Timeout = Calendar.getInstance().getTimeInMillis();
		lock.lock();
		++upload_file_count;
		lock.unlock();
	}

	@Override
	public void deleteFileCallback(int ret, FileInfo fileinfo) {
		// TODO Auto-generated method stub
		System.out.println("delete_file_back, the ret: " + ret + ", info: " + fileinfo.getId());
	}

	@Override
	public void listFileinfoCallback(int ret, Vector<FileInfo> fileinfos) {
		// TODO Auto-generated method stub
		System.out.println("list_fileinfo_back, the ret: " + ret + ", has : " + fileinfos.size() + " fileinfo elements: " + fileinfos);
	}

	@Override
	public void findFileinfoCallback(int ret, FileInfo fileinfo) {
		// TODO Auto-generated method stub
		System.out.println("find_fileinfo_back, the ret: " + ret + ", info: " + fileinfo.getId());
	}

	@Override
	public void listServersCallback(int ret, Vector<FileServer> fileservers) {
		// TODO Auto-generated method stub
		System.out.println("list_servers_back, the ret: " + ret + ", has : " + fileservers.size() + " fileserver elements");
		this.fileservers = fileservers;
	}

	@Override
	public void findServerCallback(int ret, FileServer fileserver) {
		// TODO Auto-generated method stub
		System.out.println("find_server_back, the ret: " + ret + ", info: " + fileserver.getId());
	}

	@Override
	public void addServerCallback(int ret, FileServer fileinfo) {
		// TODO Auto-generated method stub
		System.out.println("add_server_back, the ret: " + ret + ", info: " + fileinfo.getId());
	}

	@Override
	public void errorCallback(int code, String error) {
		// TODO Auto-generated method stub
		System.out.println("#### error_back, the ret: " + code + ", info: " + error);
	}

	@Override
	public void deleteServerCallback(int ret, FileServer fileinfo) {
		// TODO Auto-generated method stub
		System.out.println("delete_server_back, the ret: " + ret + ", info: " + fileinfo.getId());
	}

	@Override
	public void listFileinfoByGroupCallback(int ret, Vector<FileInfo> fileinfos) {
		System.out.println("list_fileinfo_by_group_back, the ret: " + ret + ", has : " + fileinfos.size() + " fileinfo elements");
	}

	@Override
	public void listServersByGroupCallback(int ret, Vector<FileServer> fileservers) {
		System.out.println("list_servers_by_group_back, the ret: " + ret + ", has : " + fileservers.size() + " fileserver elements");
	}
	
	public Vector<FileServer> getServersByGroup(String group) {
		Vector<FileServer> servers = new Vector<FileServer>();
		for (FileServer fileServer : fileservers) {
			if (fileServer.getGroup() == group) {
				servers.add(fileServer);
			}
		}
		
		return servers;
	}

	@Override
	public void enableServerCallback(int ret, String serverID) {
		// TODO Auto-generated method stub
		System.out.println("enable_server_back, the ret: " + ret + ", server : " + serverID);
	}
	
}
