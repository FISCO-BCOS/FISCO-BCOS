package com.sample;

import java.io.File;
import java.util.Calendar;

import com.dfs.entity.DfsConst;
import com.dfs.fileserver.DfsEngine;
import com.dfs.message.FileInfo;
import com.dfs.message.FileServer;

public class HelloDfs {
	//Note: 修改这些常量与实际运行环境一致
	final static String walletPassword = "1111";//钱包口令
	final static String keyPath = "E:\\keys\\2429feaf-0e46-ddd5-5b72-0594e0a4ab00.json";//钱包文件
	final static String fileContract = "0x9e86c1b4c4d82607298faac27da7d78364434c58";//文件信息合约地址

	final static String fileServerContract = "0x0d76426479e438fe0d261dbb165cd3adcc4a1dd7";//文件服务合约地址
	final static String host = "192.168.8.232";//服务节点主机
	final static int port = 8545;//JSONRPC端口

	public HelloDfs() {
	}

	public static void main(String[] args) {
		System.out.println("The sample process starting...");

		HelloDfs helloDfs = new HelloDfs();
		helloDfs.Hello();

		System.out.println("The sample process is finished !");
	}

	public void Hello() {
		//由于接口是异步的， 这里启动一个线程处理
		Thread thread = new Thread("DfsTestThread") {
			public void run() {
				//1. 初始化SDK
				HelloListener helloListener = new HelloListener();
				DfsEngine engine = new DfsEngine();
				if (DfsConst.DFS_ERROR_OK != engine.init(host, port, helloListener, walletPassword, keyPath, fileContract, fileServerContract)) { //初始化
					System.out.println("init engine failed !!");
					return;
				}

				//2. 添加文件服务节点
				FileServer fileServer = new FileServer();
				fileServer.setGroup("group1");
				fileServer.setId("0c1585a170e75c14564427f2faa8d74850c65765a10ce9aa4fcc55c7cdd60ab7768dd7fa066145d0ebada2214705fca7f464186e61ea163a18e6ebb9c99c7627");
				fileServer.setHost("192.168.8.232");
				fileServer.setPort(8545);
				fileServer.setInfo("the master node");
				fileServer.setOrganization("weiwan");
				fileServer.setEnable(1);
				Calendar calendar = Calendar.getInstance();
				fileServer.setUpdateTime((int)calendar.getTime().getTime() / 1000);
				if (DfsConst.DFS_ERROR_OK != engine.addServer(fileServer)) { //添加文件服务节点
					System.out.println("addServer request failed ! bad parameters");
					return;
				}

				//等待回调add_server_back回调成功
				try {
					Thread.sleep(10000);
				} catch (InterruptedException e) {
					// TODO Auto-generated catch block
					e.printStackTrace();
				}//10s

				//3. 上传文件
				FileInfo fileInfo = new FileInfo();
				fileInfo.setOwner("weiwan");
				fileInfo.setPriviliges("rwx");
				String file = "E:" + File.separatorChar + "test.txt";
				if (DfsConst.DFS_ERROR_OK != engine.uploadFile(fileInfo, file)) { //异步上传文件
					System.out.println("uploadFile request failed ! bad parameters");
					return;
				}

				//等待回调upload_file_back...回调成功
				try {
					Thread.sleep(10000);
				} catch (InterruptedException e) {
					// TODO Auto-generated catch block
					e.printStackTrace();
				}//10s

				//使用文件ID下载文件
				//4. 下载文件
				String store_path = "C://download.txt";
				if (DfsConst.DFS_ERROR_OK != engine.downloadFile(helloListener.fileid, store_path)) { //异步下载文件
					System.out.println("downloadFile request failed ! bad parameters");
					return;
				}

				//等待回调upload_file_back...回调成功
				try {
					Thread.sleep(10000);
				} catch (InterruptedException e) {
					// TODO Auto-generated catch block
					e.printStackTrace();
				}//10s

				System.out.println("Hello Done !");
				return;
			}
		};
		thread.start();

		System.out.println("... waiting for bussiness handle thread exit...");
		try {
			thread.join();
		} catch (InterruptedException e1) {
			// TODO Auto-generated catch block
			e1.printStackTrace();
		}
	}
}
