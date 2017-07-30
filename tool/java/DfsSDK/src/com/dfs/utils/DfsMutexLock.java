/**  
 * @Title: JuMutexLock.java

 * @date: 2017年3月9日 下午5:11:55

 * @version: V1.0  

 */
package com.dfs.utils;

import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;

/**
 * @author Administrator
 *
 */
public class DfsMutexLock {
	 //需要声明这个锁  
    private Lock lock; 
    
    public DfsMutexLock() {
		// TODO Auto-generated constructor stub
    	lock = new ReentrantLock(); 
	}
    
    public void lock(){
    	lock.lock();
    }
    
    public void unlock(){
    	lock.unlock();
    }
}
