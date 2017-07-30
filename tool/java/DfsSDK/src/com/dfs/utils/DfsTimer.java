package com.dfs.utils;

import java.util.Calendar;


public class DfsTimer {
	private long startTime;
	private long endTime;
	
	public DfsTimer() {
		startTime = Calendar.getInstance().getTimeInMillis();
		endTime = startTime;
	}

	public void start() {
		startTime = Calendar.getInstance().getTimeInMillis();
	}
	
	public long stop(){
		endTime = Calendar.getInstance().getTimeInMillis();
		System.out.println("cost time: " + (endTime - startTime));
		return (endTime - startTime);
	}
}
