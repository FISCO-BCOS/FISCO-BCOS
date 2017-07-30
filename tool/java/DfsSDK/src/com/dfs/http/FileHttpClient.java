/**  

 * @Title: FileHttpClient.java

 * @date: 2017年3月7日 上午10:04:29

 * @version: V1.0  

 */
package com.dfs.http;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.UnsupportedEncodingException;
import java.nio.charset.Charset;

import org.apache.http.HttpEntity;
import org.apache.http.client.methods.CloseableHttpResponse;
import org.apache.http.client.methods.HttpDelete;
import org.apache.http.client.methods.HttpGet;
import org.apache.http.client.methods.HttpPost;
import org.apache.http.client.methods.HttpPut;
import org.apache.http.entity.StringEntity;
import org.apache.http.entity.mime.MultipartEntityBuilder;
import org.apache.http.entity.mime.content.FileBody;
import org.apache.http.impl.client.CloseableHttpClient;
import org.apache.http.impl.client.HttpClients;
import org.apache.http.util.EntityUtils;

import com.alibaba.fastjson.JSON;
import com.dfs.entity.DfsConst;
import com.dfs.message.FileInfo;
import com.dfs.message.FileOpResponse;
import com.dfs.message.FileServer;

/**
 * @author Administrator
 *
 */
public class FileHttpClient {
	private String	m_Host;
	private int 	m_Port;
	private boolean	m_Init = false;
	
	private	int 	m_Ret;
	private int		m_Code;
	private String		m_Info;
	
	public FileHttpClient()
	{
		m_Host = "";
		m_Port = 0;
		m_Init = false;
		m_Ret = DfsConst.DFS_ERROR_OK;
		m_Code = DfsConst.DFS_ERROR_SUCCESS;
		m_Info = "";
	}
	
	public int init(String host, int port) {
		this.m_Host = host;
		this.m_Port = port;
		m_Init = true;
		return 0;
	}
	
	public int download(FileServer fileServer, String fileid, String store_path)
	{
		resetState();
		
		if (!m_Init)
		{
			m_Ret = DfsConst.DFS_ERROR_NOT_INITIALIZED;
			return DfsConst.DFS_ERROR_NOT_INITIALIZED;
		}

		//url: http://host:port/fs/v1/files/fileid
		String url = fillUrl(fileServer, DfsConst.HTTP_FS_CONTAINER_ID_DEFAULT, fileid);
		int ret = DfsConst.DFS_ERROR_OK;
		// 把一个普通参数和文件上传给下面这个地址 是一个servlet
		CloseableHttpClient httpClient = HttpClients.createDefault();
        OutputStream out = null;
        InputStream in = null;
        
        try {
            HttpGet httpGet = new HttpGet(url);
            CloseableHttpResponse httpResponse = httpClient.execute(httpGet);
            HttpEntity entity = httpResponse.getEntity();
            in = entity.getContent();

            long length = entity.getContentLength();
            if (length <= 0) {
                System.out.println("下载文件不存在！");
                m_Code = DfsConst.DFS_ERROR_FILE_NOT_FIND;
                m_Ret = DfsConst.DFS_ERROR_FILE_NOT_FIND;
                m_Info = "download file not exists";
                httpResponse.close();
                httpClient.close();
                
                return DfsConst.DFS_ERROR_FILE_NOT_FIND;
            }

            File file = new File(store_path);
            if(!file.exists()){
                file.createNewFile();
            }
            
            out = new FileOutputStream(file);  
            byte[] buffer = new byte[4096];
            int readLength = 0;
            while ((readLength=in.read(buffer)) > 0) {
                byte[] bytes = new byte[readLength];
                System.arraycopy(buffer, 0, bytes, 0, readLength);
                out.write(bytes);
            }
            
            out.flush();
            
        } catch (Exception e) {
        	ret = DfsConst.DFS_ERROR_FAIL;
        	m_Ret = ret;
        	m_Code = ret;
        	m_Info = "download file failed!";
            e.printStackTrace();
        }finally{
            try {
                if(in != null){
                    in.close();
                }
                
                if(out != null){
                    out.close();
                }
            } catch (IOException e) {
            	ret = DfsConst.DFS_ERROR_FAIL;
            	m_Ret = ret;
            	m_Code = ret;
                e.printStackTrace();
            }
        }
		
		return ret;
	}
	
	public int upload(FileServer fileServer, FileInfo fileinfo, String store_path, StringBuffer remoteHash) {
		resetState();
		
		if (!m_Init)
		{
			m_Ret = DfsConst.DFS_ERROR_NOT_INITIALIZED;
			return DfsConst.DFS_ERROR_NOT_INITIALIZED;
		}
		
		if (fileinfo.getId().length() <= 0 || 
				fileinfo.getContainer() != DfsConst.HTTP_FS_CONTAINER_ID_DEFAULT || 
				fileinfo.getSrc_node().length() <= 0) {
			m_Ret = DfsConst.DFS_ERROR_BAD_PARAMETER;
			m_Code = DfsConst.DFS_ERROR_FAIL;
			return DfsConst.DFS_ERROR_BAD_PARAMETER;
		}
		
		File f = new File(store_path);
		if(!f.exists() || f.isDirectory()) { 
		    // do something
			m_Ret = DfsConst.DFS_ERROR_FILE_NOT_FIND;
			m_Code = DfsConst.DFS_ERROR_FAIL;
			return DfsConst.DFS_ERROR_FILE_NOT_FIND;
		}
		
		//url: http://host:port/fs/v1/files/fileid
		String url = fillUrl(fileServer, DfsConst.HTTP_FS_CONTAINER_ID_DEFAULT, fileinfo.getId());
		System.out.println("the url: " + url);
		String strJson = new String();
		CloseableHttpClient httpClient = null;
        CloseableHttpResponse response = null;
        try {
            httpClient = HttpClients.createDefault();
            
            //构造一个post对象
            HttpPost httpPost = new HttpPost(url);
            
            // 把文件转换成流对象FileBody
            FileBody inFile = new FileBody(new File(store_path));
            HttpEntity reqEntity = MultipartEntityBuilder.create().addPart("file", inFile).build();
            httpPost.setEntity(reqEntity);

            // 发起请求 并返回请求的响应
            response = httpClient.execute(httpPost);

            // 获取响应对象
            HttpEntity resEntity = response.getEntity();
            if (resEntity != null) {
            	strJson = EntityUtils.toString(resEntity, Charset.forName("UTF-8"));
            	//parse json
                FileOpResponse objFileOpRsp = JSON.parseObject(strJson, FileOpResponse.class);
                if (objFileOpRsp.getRet() != DfsConst.DFS_ERROR_OK 
                		&& objFileOpRsp.getRet() != DfsConst.DFS_ERROR_SUCCESS) {
                	// 销毁
                    EntityUtils.consume(resEntity);
                    m_Ret = DfsConst.DFS_ERROR_FAIL;
                    m_Code = DfsConst.DFS_ERROR_FAIL;
                	return DfsConst.DFS_ERROR_FAIL;
                }
                
                remoteHash.append(objFileOpRsp.getHash());
            }
            
            // 销毁
            EntityUtils.consume(resEntity);
        }catch (Exception e){
            e.printStackTrace();
            m_Ret = DfsConst.DFS_ERROR_FAIL;
            m_Info = "bad request or service unavaiable !";
        }finally {
            try {
                if(response != null){
                    response.close();
                }
            
                if(httpClient != null){
                    httpClient.close();
                }
            } catch (IOException e) {
                e.printStackTrace();
            	m_Ret = DfsConst.DFS_ERROR_FAIL;
            	m_Code = DfsConst.DFS_ERROR_FAIL;
                return DfsConst.DFS_ERROR_FAIL;
            }
        }

		return m_Ret;
	}
	
	public int deleteFile(FileServer fileServer, String fileid)
	{
		resetState();
		
		if (!m_Init)
		{
			m_Ret = DfsConst.DFS_ERROR_NOT_INITIALIZED;
        	m_Code = DfsConst.DFS_ERROR_NOT_INITIALIZED;
			return DfsConst.DFS_ERROR_NOT_INITIALIZED;
		}
		
		//url: http://host:port/fs/v1/files/fileid
		String url = fillUrl(DfsConst.HTTP_FS_CONTAINER_ID_DEFAULT, fileid);
		StringBuffer rspBuffer = new StringBuffer();
		
		CloseableHttpClient httpClient = null;
        CloseableHttpResponse response = null;
       
        httpClient = HttpClients.createDefault();
        
        // 把一个普通参数和文件上传给下面这个地址 是一个servlet
        HttpDelete httpDelete = new HttpDelete(url);
        try {
			response = httpClient.execute(httpDelete);
			BufferedReader br = new BufferedReader(new InputStreamReader((response.getEntity().getContent())));
			
			String output;
			System.out.println("Output from Server .... \n");
			while ((output = br.readLine()) != null) {
				rspBuffer.append(output);
			}
			
			br.close();
			
		} catch (IOException e) {
			e.printStackTrace();
			m_Ret = DfsConst.DFS_ERROR_NETWORK_EXCEPTION;
        	m_Code = DfsConst.DFS_ERROR_NETWORK_EXCEPTION;
			
		} finally {
			try {
				if (response != null)
					response.close();
				
				if (httpClient != null)
					httpClient.close();
			} catch (IOException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
				m_Ret = DfsConst.DFS_ERROR_FAIL;
            	m_Code = DfsConst.DFS_ERROR_FAIL;
                return DfsConst.DFS_ERROR_FAIL;
			}
			
		}
        
        //parse json
        String strJson = rspBuffer.toString();
        FileOpResponse objFileOpRsp = JSON.parseObject(strJson, FileOpResponse.class);
        if (objFileOpRsp.getRet() != DfsConst.DFS_ERROR_OK 
        		&& objFileOpRsp.getRet() != DfsConst.DFS_ERROR_SUCCESS) {
        	m_Ret = DfsConst.DFS_ERROR_FAIL;
        	m_Code = DfsConst.DFS_ERROR_FAIL;
            return DfsConst.DFS_ERROR_FAIL;
        }
        
        return m_Ret;
	}
	
	public int modify(FileServer fileServer, String fileid, String filename)
	{
		resetState();
		
		if (!m_Init)
			return DfsConst.DFS_ERROR_NOT_INITIALIZED;
		
		if (fileid.isEmpty() || filename.isEmpty())
			return DfsConst.DFS_ERROR_BAD_PARAMETER;
		
		//url: http://host:port/fs/v1/files/fileid
		String url = fillUrl(fileServer, DfsConst.HTTP_FS_CONTAINER_ID_DEFAULT, fileid);
		StringBuffer rspBuffer = new StringBuffer();
		
		CloseableHttpClient httpClient = null;
        CloseableHttpResponse response = null;
       
        httpClient = HttpClients.createDefault();
        
        // 把一个普通参数和文件上传给下面这个地址 是一个servlet
        HttpPut httpPut = new HttpPut(url);
        FileInfo objFileinfo = new FileInfo();
        objFileinfo.setId(fileid);
        objFileinfo.setFilename(filename);
        String json = JSON.toJSONString(objFileinfo);
        StringEntity putString = null;
		try {
			putString = new StringEntity(json);
		} catch (UnsupportedEncodingException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
			m_Ret = DfsConst.DFS_ERROR_FAIL;
        	m_Code = DfsConst.DFS_ERROR_FAIL;
            return DfsConst.DFS_ERROR_FAIL;
		}
        httpPut.setEntity(putString);
        httpPut.setHeader("Content-type", "application/json");

        try {
			response = httpClient.execute(httpPut);
			BufferedReader br = new BufferedReader(new InputStreamReader((response.getEntity().getContent())));
			
			String output;
			System.out.println("Output from Server .... \n");
			while ((output = br.readLine()) != null) {
				rspBuffer.append(output);
			}
			br.close();
		} catch (Exception e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
			m_Ret = DfsConst.DFS_ERROR_FAIL;
        	m_Code = DfsConst.DFS_ERROR_FAIL;
		} finally {
			try {
				if (response != null)
					response.close();
				if (httpClient != null)
					httpClient.close();
			} catch (IOException e) {
				// TODO Auto-generated catch block
				e.printStackTrace();
				m_Ret = DfsConst.DFS_ERROR_FAIL;
            	m_Code = DfsConst.DFS_ERROR_FAIL;
                return DfsConst.DFS_ERROR_FAIL;
			}
		}
            
        return m_Ret;
	}
	
	private String fillUrl(String container, String fileid) {
		String url = new String();
		url = DfsConst.HTTP_STARTER;
		url += m_Host;
		url += ":";
		url += m_Port;
		url += "/";
		url += DfsConst.HTTP_FS_MODULE;
		url += "/";
		url += DfsConst.HTTP_FS_VERSION;
		url += "/";
		url += container;
		url += "/";
		url += fileid;
		
		return url;
	}
	
	private String fillUrl(FileServer fileServer, String container, String fileid) {
		String url = new String();
		url = DfsConst.HTTP_STARTER;
		url += fileServer.getHost();
		url += ":";
		url += fileServer.getPort();
		url += "/";
		url += DfsConst.HTTP_FS_MODULE;
		url += "/";
		url += DfsConst.HTTP_FS_VERSION;
		url += "/";
		url += container;
		url += "/";
		url += fileid;
		
		return url;
	}
	
	private void resetState() {
		m_Ret = DfsConst.DFS_ERROR_OK;
		m_Code = DfsConst.DFS_ERROR_OK;
		m_Info = "";
	}

	/**
	 * @return the m_Host
	 */
	public String getHost() {
		return m_Host;
	}

	/**
	 * @return the m_Port
	 */
	public int getPort() {
		return m_Port;
	}

	/**
	 * @return the m_Init
	 */
	public boolean isInit() {
		return m_Init;
	}

	/**
	 * @return the m_Ret
	 */
	public int getRet() {
		return m_Ret;
	}

	/**
	 * @return the m_Code
	 */
	public int getCode() {
		return m_Code;
	}

	/**
	 * @return the m_Info
	 */
	public String getInfo() {
		return m_Info;
	}

	/**
	 * @param m_Host the m_Host to set
	 */
	public void setHost(String m_Host) {
		this.m_Host = m_Host;
	}

	/**
	 * @param m_Port the m_Port to set
	 */
	public void setPort(int m_Port) {
		this.m_Port = m_Port;
	}

	/**
	 * @param m_Init the m_Init to set
	 */
	public void setInit(boolean m_Init) {
		this.m_Init = m_Init;
	}

	/**
	 * @param m_Ret the m_Ret to set
	 */
	public void setRet(int m_Ret) {
		this.m_Ret = m_Ret;
	}

	/**
	 * @param m_Code the m_Code to set
	 */
	public void setCode(int m_Code) {
		this.m_Code = m_Code;
	}

	/**
	 * @param m_Info the m_Info to set
	 */
	public void setInfo(String m_Info) {
		this.m_Info = m_Info;
	}
}
