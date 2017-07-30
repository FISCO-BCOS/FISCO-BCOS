package com.sample;

import java.util.Vector;

import com.dfs.fileserver.IDfsListener;
import com.dfs.message.FileInfo;
import com.dfs.message.FileServer;

public class HelloListener implements IDfsListener {
	public String fileid;
	
	public HelloListener() {
		fileid = "";
	}
	
	@Override
	public void downloadFileCallback(int ret, FileInfo fileinfo) {
		System.out.println("downloadFileCallback");
	}

	@Override
	public void uploadFileCallback(int ret, FileInfo fileinfo) {
		System.out.println("uploadFileCallback");
		fileid = fileinfo.getId();
	}

	@Override
	public void deleteFileCallback(int ret, FileInfo fileinfo) {
		System.out.println("deleteFileCallback");
	}

	@Override
	public void listFileinfoCallback(int ret, Vector<FileInfo> fileinfos) {
		System.out.println("listFileinfoCallback");
	}

	@Override
	public void listFileinfoByGroupCallback(int ret, Vector<FileInfo> fileinfos) {
		System.out.println("listFileinfoByGroupCallback");
	}

	@Override
	public void findFileinfoCallback(int ret, FileInfo fileinfo) {
		System.out.println("findFileinfoCallback");
	}

	@Override
	public void listServersCallback(int ret, Vector<FileServer> fileservers) {
		System.out.println("list_servers_back");
	}

	@Override
	public void listServersByGroupCallback(int ret, Vector<FileServer> fileservers) {
		System.out.println("list_servers_by_group_back");
	}

	@Override
	public void findServerCallback(int ret, FileServer fileserver) {
		System.out.println("find_server_back");
	}

	@Override
	public void addServerCallback(int ret, FileServer server) {
		System.out.println("add_server_back");
	}

	@Override
	public void deleteServerCallback(int ret, FileServer server) {
		System.out.println("delete_server_back");
	}

	@Override
	public void enableServerCallback(int ret, String serverID) {
		System.out.println("enable_server_back");
	}

	@Override
	public void errorCallback(int code, String error) {
		System.out.println("error_back");
	}

}
