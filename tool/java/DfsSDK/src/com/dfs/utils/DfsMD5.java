/**  
 * @Title: JuMD5.java

 * @date: 2017年3月8日 下午3:09:43

 * @version: V1.0  

 */
package com.dfs.utils;

import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;

/**
 * @author Administrator
 *
 */


import org.apache.commons.codec.digest.DigestUtils;
import org.apache.commons.io.IOUtils; 

public class DfsMD5 { 
  
    //静态方法，便于作为工具类  
    public static String getMd5(String file) {  
    	FileInputStream fis = null;
		try {
			fis = new FileInputStream(file);
		} catch (FileNotFoundException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}
		
        String md5 = null;
		try {
			md5 = DigestUtils.md5Hex(IOUtils.toByteArray(fis));
		} catch (IOException e) {
			// TODO Auto-generated catch block
			e.printStackTrace();
		}    
        IOUtils.closeQuietly(fis);    
        //System.out.println("MD5:"+md5);   
        
        return md5;
    }
  
}  
