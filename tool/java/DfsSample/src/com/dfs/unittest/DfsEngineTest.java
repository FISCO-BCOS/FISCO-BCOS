/**
 * @Title: DfsEngineTest.java
 */

package com.dfs.unittest;

import java.io.File;
import java.util.Calendar;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import com.dfs.entity.DfsConst;
import com.dfs.fileserver.DfsEngine;
import com.dfs.fileserver.IDfsListener;
import com.dfs.message.FileInfo;
import com.dfs.message.FileServer;

/**
 * @author Administrator
 *
 */
public class DfsEngineTest extends DfsEngine {
	private int threadCount = 0;
	private IDfsListener TestListener;
	private static int rpcPort = 8545;
	private static String connHost = "192.168.8.219";
	private boolean isInited = false;

	//FileInfoManager contract address
	private static String fileContract = "0x317523b03a8ed12a4994342d5dd8216f10483533";
	//FileServerManager contract address
	private static String serverContract = "0x997736ea4fbedcbed96cbdc15955a1468886abb4";

	private String walletPassword = "1111";
	private static String walletPath = "E:\\keys\\2429feaf-0e46-ddd5-5b72-0594e0a4ab00.json";

	public DfsEngineTest() {
		TestListener = new DfsTestListener();
		DfsTestListener childListener = (DfsTestListener)TestListener;
		childListener.client = this;

		if (DfsConst.DFS_ERROR_OK != init(connHost, rpcPort, TestListener, walletPassword, walletPath, fileContract, serverContract)) {
			System.err.println("init DfsEngine failed ! check input parameters and server node !");
			isInited = false;
		}
		isInited = true;
	}

	/**
	 * @description 初始化
	 * @param host 主机地址
	 * @param port JSONRPC端口
	 * @param listener 监听回调接口
	 * @param walletPasswd 加密口令
	 * @param walletFile 钱包文件
	 * @param fileContract 文件信息合约
	 * @param serverContract 文件服务合约
	 * @return 0 ：成功    其他：失败
	 */
	public int init(String host, int port, IDfsListener listener, String walletPasswd, String walletFile, String fileContract, String serverContract) {
		if (DfsConst.DFS_ERROR_OK != super.init(host, port, listener, walletPasswd, walletFile, fileContract, serverContract)) {
			System.err.println("init DfsEngine failed !");
			return DfsConst.DFS_ERROR_FAIL;
		}

		// we now add a server if in need. If we have already added one, this code can be comment
		//{{ add servers here !!!!!
		FileServer fileServer = new FileServer();
		fileServer.setGroup("group1");
		fileServer.setId("24cc3b974e9d8ee369f53fd018b840a735f12ce5e58af7f80d1644f6ed94631625b0aafb3fac778f639fa966f1bdc7387a438d71015c4f53f3c802ed4c127703");
		fileServer.setHost(connHost);
		fileServer.setPort(rpcPort);
		fileServer.setInfo("the master node");
		fileServer.setOrganization("group1");
		fileServer.setEnable(1);
		Calendar calendar = Calendar.getInstance();
		fileServer.setUpdateTime((int)calendar.getTime().getTime() / 1000);
		StringBuffer errorInfo = new StringBuffer();
		if (DfsConst.DFS_ERROR_OK != addServerSync(fileServer, errorInfo)) {
			System.out.println("add serve failed: " + errorInfo.toString());
		}

		fileServer.setGroup("group1");
		fileServer.setId("bedade1701411a12fd073538af0852911df990a288390ae53daae1c5ca681988bcf73621e7d0f0672b269d03e13ce4150798fdf9db25c4bf55b923b8757859e0");
		fileServer.setHost("192.168.8.200");
		fileServer.setPort(rpcPort);
		fileServer.setInfo("the slaver node");
		fileServer.setOrganization("group1");
		fileServer.setEnable(1);
		fileServer.setUpdateTime((int)calendar.getTime().getTime() / 1000);
		errorInfo = new StringBuffer();
		if (DfsConst.DFS_ERROR_OK != addServerSync(fileServer, errorInfo)) {
			System.out.println("add serve failed: " + errorInfo.toString());
		}
		//}} add servers here !!!!!

		return DfsConst.DFS_ERROR_OK;
	}

	/**
	 * @throws java.lang.Exception
	 */
	@Before
	public void setUp() throws Exception {
		// to ensure the child thread run successfully, the code below should not be removed
		threadCount = Thread.activeCount();
		System.out.println("The active thread before test: " + threadCount);
	}

	/**
	 * @throws java.lang.Exception
	 */
	@After
	public void tearDown() throws Exception {
		// to ensure the child thread run successfully, the code below should not be removed
		while (Thread.activeCount() > threadCount) {
			//wait
			Thread.sleep(1000);
		}

		threadCount = Thread.activeCount();
		System.out.println("The active thread after test: " + threadCount);
	}

//	/**
//	 * Test method for {@link com.dfs.fileserver.DfsEngine#download(java.lang.String, java.lang.String)}.
//	 */
//	@Test
//	public void testDownload() {
//		String store_path = "E://test_download-be1942a0c692ae747aa37ca46266a17fa62297b6e7ac6d11fb75a888b9a9237a.log";
//		DfsTestListener childListener = (DfsTestListener)TestListener;
//		childListener.updateTimeout();
//		downloadFile("be1942a0c692ae747aa37ca46266a17fa62297b6e7ac6d11fb75a888b9a9237a", store_path);
//	}
//
//	/**
//	 * Test method for {@link com.dfs.fileserver.DfsEngine#upload(com.dfs.message.FileInfo, java.lang.String)}.
//	 * @throws InterruptedException
//	 */
//	@Test
//	public void testUpload_1M() throws InterruptedException {
//		FileInfo fileInfo = new FileInfo();
//		fileInfo.setOwner("group1");
//		fileInfo.setPriviliges("rwx");
//		String file = "E:"+ File.separatorChar + "test1m.log";
//
//		for (int i = 1; i <= 1; i++) {
//			System.out.println("**** upload file ... ");
//			DfsTestListener childListener = (DfsTestListener)TestListener;
//			childListener.updateTimeout();
//			int ret = uploadFile(fileInfo, file);
//			if (DfsConst.DFS_ERROR_OK != ret){
//				System.out.println("**** upload file failed, check the parameters, ret: " + ret);
//			}
//
//			DfsTestListener testListener = (DfsTestListener)TestListener;
//			while (testListener.upload_file_count != i) {
//				Thread.sleep(1000);
//			}
//
//			System.out.println("upload file id: " + fileInfo.getId() + " succcess !");
//		}
//
//		System.out.println("upload all files success !");
//	}

	/**
	 * Test method for {@link com.dfs.fileserver.DfsEngine#upload(com.ju.message.FileInfo, java.lang.String)}.
	 * @throws InterruptedException
	 */
	@Test
	public void testUpload_10M() throws InterruptedException {
		if (!isInited) {
			System.err.println("init failed, should not upload files !");
		}

		FileInfo fileInfo = new FileInfo();
		fileInfo.setOwner("group1");
		fileInfo.setPriviliges("rwx");
		String file = "E:" + File.separatorChar + "test10m.log";

		for (int i = 1; i <= 1; i++) {
			boolean state = true;
			System.out.println("**** upload file ... ");
			DfsTestListener testListener = (DfsTestListener)TestListener;
			testListener.updateTimeout();
			int ret = uploadFile(fileInfo, file);
			if (DfsConst.DFS_ERROR_OK != ret) {
				System.out.println("**** upload file failed, check the parameters, ret: " + ret);
				state = false;
			}

			while (testListener.upload_file_count != i && state) {
				Thread.sleep(1000);
			}

			System.out.println("upload file id: " + fileInfo.getId() + " succcess !");
		}

		System.out.println("upload all files success !");
	}

//	/**
//	 * Test method for {@link com.dfs.fileserver.DfsEngine#upload(com.ju.message.FileInfo, java.lang.String)}.
//	 * @throws InterruptedException
//	 */
//	@Test
//	public void testUpload_100M() throws InterruptedException {
//		FileServer server = getTheFileServer();
//		FileInfo fileInfo = new FileInfo();
//		fileInfo.setOwner("group1");
//		fileInfo.setPriviliges("rwx");
//		String file = "E:"+ File.separatorChar + "test100m.log";
//
//		for (int i = 1; i <= 10; i++) {
//			System.out.println("**** upload file ... ");
//			int ret = uploadFile(fileInfo, file);
//			if (DfsFileConst.JU_ERROR_OK != ret){
//				System.out.println("**** upload file failed, check the parameters, ret: " + ret);
//			}
//
//			DfsTestListener testListener = (DfsTestListener)TestListener;
//			while (testListener.upload_file_count != i) {
//				Thread.sleep(1000);
//			}
//
//			System.out.println("upload file id: " + fileInfo.getId() + " succcess !");
//		}
//
//		System.out.println("upload all files success !");
//	}
//
//
//	/**
//	 * Test method for {@link com.dfs.fileserver.DfsEngine#delete(java.lang.String)}.
//	 */
//	@Test
//	public void testDeleteFile() {
//		deleteFile("03aa3e823cb7899b7b589ea242cdc2f26717d5fc");//
//	}
//
//	/**
//	 * Test method for {@link com.dfs.fileserver.DfsEngine#listFiles()}.
//	 */
//	@Test
//	public void testListFiles() {
//		listFiles();
//	}

//	/**
//	 * Test method for {@link com.dfs.fileserver.DfsEngine#findFile(java.lang.String)}.
//	 */
//	@Test
//	public void testFindFile() {
//		findFile("d9aab95fba4a1cff09161f258f5e5f439290a9d353f9775243c54a0242889769");
//	}
//
//
//	/**
//	 * Test method for {@link com.dfs.fileserver.DfsEngine#listServerByGroup(java.lang.String)}.
//	 */
//	@Test
//	public void testAddMasterServer() {
//		FileServer fileServer = new FileServer();
//		fileServer.setGroup("group1");
//		fileServer.setId("03aa3e823cb7899b7b589ea242cdc2f26717d5fc");
//		fileServer.setHost("192.168.8.200");
//		fileServer.setPort(rpcPort);
//		fileServer.setInfo("the master node");
//		fileServer.setOrganization("file server");
//		fileServer.setEnable(1);
//		Calendar calendar = Calendar.getInstance();
//		fileServer.setUpdateTime((int)calendar.getTime().getTime()/1000);
//
//		addServer(fileServer);
//		System.out.println("-------------------------");
//		//assertEquals(DfsFileConst.JU_ERROR_OK, ret);
//	}
//
//	/**
//	 * Test method for {@link com.dfs.fileserver.DfsEngine#listServerByGroup(java.lang.String)}.
//	 */
//	@Test
//	public void testAddSlaverServer() {
//		FileServer fileServer = new FileServer();
//		fileServer.setGroup("group1");
//		fileServer.setId("66d708b2e46aee031f96ab89cea8f886526817b0");
//		fileServer.setHost("192.168.8.212");
//		fileServer.setPort(6789);
//		fileServer.setInfo("the slaver node");
//		fileServer.setOrganization("group1");
//		fileServer.setEnable(1);
//		Calendar calendar = Calendar.getInstance();
//		fileServer.setUpdateTime((int)calendar.getTime().getTime()/1000);
//		addServer(fileServer);
//
//	}

//
//	/**
//	 * Test method for {@link com.dfs.fileserver.DfsEngine#listServerByGroup(java.lang.String)}.
//	 */
//	@Test
//	public void testDeleteServer() {
//		deleteServer("03aa3e823cb7899b7b589ea242cdc2f26717d5fc");
//	}

//	/**
//	 * Test method for {@link com.dfs.fileserver.DfsEngine#listServer()}.
//	 */
//	@Test
//	public void testListServer() {
//		listServers();
//	}
//
	/**
	 * Test method for {@link com.dfs.fileserver.DfsEngine#listServerByGroup(java.lang.String)}.
	 */
	@Test
	public void testListServerByGroup() {
		listServerByGroup("group1");
	}

//	public FileServer getTheFileServer() {
//		final FileServer defaultServer = new FileServer();
//		defaultServer.setPort(port);
//		defaultServer.setHost(host);
//		defaultServer.setId("node001");
//		defaultServer.setGroup("group1");
//		if (getFileServers().size() <= 0){
//			return defaultServer;
//		}
//
//		Vector<FileServer> vecFiles = getFileServers();
//		for (FileServer file : vecFiles) {
//			if (host.equals(file.getHost()) == true)
//				return file;
//		}
//
//		return defaultServer;
//	}

}
