package com.dfs.message;

public class CommonRespsoneJson {
	private int ret;
	private CommonResponseData data;
	
	public CommonRespsoneJson() {
		ret = 0;
		data = new CommonResponseData();
	}
	
	/**
	 * @return the ret
	 */
	public int getRet() {
		return ret;
	}
	/**
	 * @return the data
	 */
	public CommonResponseData getData() {
		return data;
	}
	/**
	 * @param ret the ret to set
	 */
	public void setRet(int ret) {
		this.ret = ret;
	}
	/**
	 * @param data the data to set
	 */
	public void setData(CommonResponseData data) {
		this.data = data;
	}
	
}
