/**  
 * @Title: FileOpResponse.java

 * @Description: 文件操作响应

 * @date: 2017年3月1日 下午6:12:48

 * @version: V1.0  

 */

package com.dfs.message;


public class FileOpResponse {
	private int ret;
	private int code;
	private String info;
	private String hash;
	
	
	public FileOpResponse() {
		ret = 0;
		code = 0;
		info = "";
	}
	
	/**
	 * @return the ret
	 */
	public int getRet() {
		return ret;
	}
	/**
	 * @return the code
	 */
	public int getCode() {
		return code;
	}
	/**
	 * @return the info
	 */
	public String getInfo() {
		return info;
	}
	/**
	 * @param ret the ret to set
	 */
	public void setRet(int ret) {
		this.ret = ret;
	}
	/**
	 * @param code the code to set
	 */
	public void setCode(int code) {
		this.code = code;
	}
	/**
	 * @param info the info to set
	 */
	public void setInfo(String info) {
		this.info = info;
	}

	public String getHash() {
		// TODO Auto-generated method stub
		return this.hash;
	}
	
	public void setHash(String hash){
		this.hash = hash;
	}
}
