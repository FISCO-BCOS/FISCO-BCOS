package com.dfs.message;

import java.util.Vector;

public class CommonResponseData {
	private int total;
	private Vector<String> items;
	
	public CommonResponseData() {
		total = 0;
		items = new Vector<String>();
	}

	/**
	 * @return the total
	 */
	public int getTotal() {
		return total;
	}

	/**
	 * @return the items
	 */
	public Vector<String> getItems() {
		return items;
	}

	/**
	 * @param total the total to set
	 */
	public void setTotal(int total) {
		this.total = total;
	}

	/**
	 * @param items the items to set
	 */
	public void setItems(Vector<String> items) {
		this.items = items;
	}
	
	
}
