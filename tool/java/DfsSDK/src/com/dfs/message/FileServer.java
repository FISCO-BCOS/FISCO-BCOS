/**  
 * @Title: FileServer.java

 * @date: 2017年3月2日 下午4:17:52

 * @version: V1.0  

 */
package com.dfs.message;


public class FileServer {
	private    	String      	id;/** 服务节点ID */
    private    	String      	host;/** 主机地址 */
    private    	int     		port;/** 主机RPC端口 */
    private    	int     		updateTime;/** 更新时间 */
    private		String      	organization;/** 组织名 */
    private    	String      	position;/** 位置 */
    private    	String      	group;/** 节点群组ID */
    private    	String      	info;/** 信息 */
    private		int				enable;/** 是否开启 */ 
    
    public FileServer() {
		super();
		// TODO Auto-generated constructor stub
	}
    
	/**
	 * @return the id
	 */
	public String getId() {
		return id;
	}
	/**
	 * @return the host
	 */
	public String getHost() {
		return host;
	}
	/**
	 * @return the port
	 */
	public int getPort() {
		return port;
	}
	/**
	 * @return the updateTime
	 */
	public int getUpdateTime() {
		return updateTime;
	}
	/**
	 * @return the organization
	 */
	public String getOrganization() {
		return organization;
	}
	/**
	 * @return the position
	 */
	public String getPosition() {
		return position;
	}
	/**
	 * @return the group
	 */
	public String getGroup() {
		return group;
	}
	/**
	 * @return the info
	 */
	public String getInfo() {
		return info;
	}
	/**
	 * @param id the id to set
	 */
	public void setId(String id) {
		this.id = id;
	}
	/**
	 * @param host the host to set
	 */
	public void setHost(String host) {
		this.host = host;
	}
	/**
	 * @param port the port to set
	 */
	public void setPort(int port) {
		this.port = port;
	}
	/**
	 * @param updateTime the updateTime to set
	 */
	public void setUpdateTime(int updateTime) {
		this.updateTime = updateTime;
	}
	/**
	 * @param organization the organization to set
	 */
	public void setOrganization(String organization) {
		this.organization = organization;
	}
	/**
	 * @param position the position to set
	 */
	public void setPosition(String position) {
		this.position = position;
	}
	/**
	 * @param group the group to set
	 */
	public void setGroup(String group) {
		this.group = group;
	}
	/**
	 * @param info the info to set
	 */
	public void setInfo(String info) {
		this.info = info;
	}
	
	/* (non-Javadoc)
	 * @see java.lang.Object#hashCode()
	 */
	@Override
	public int hashCode() {
		final int prime = 31;
		int result = 1;
		result = prime * result + ((group == null) ? 0 : group.hashCode());
		result = prime * result + ((host == null) ? 0 : host.hashCode());
		result = prime * result + ((id == null) ? 0 : id.hashCode());
		result = prime * result + ((info == null) ? 0 : info.hashCode());
		result = prime * result + ((organization == null) ? 0 : organization.hashCode());
		result = prime * result + port;
		result = prime * result + ((position == null) ? 0 : position.hashCode());
		result = prime * result + updateTime;
		return result;
	}

	/* (non-Javadoc)
	 * @see java.lang.Object#equals(java.lang.Object)
	 */
	@Override
	public boolean equals(Object obj) {
		if (this == obj) {
			return true;
		}
		if (obj == null) {
			return false;
		}
		if (!(obj instanceof FileServer)) {
			return false;
		}
		FileServer other = (FileServer) obj;
		if (group == null) {
			if (other.group != null) {
				return false;
			}
		} else if (!group.equals(other.group)) {
			return false;
		}
		if (host == null) {
			if (other.host != null) {
				return false;
			}
		} else if (!host.equals(other.host)) {
			return false;
		}
		if (id == null) {
			if (other.id != null) {
				return false;
			}
		} else if (!id.equals(other.id)) {
			return false;
		}
		if (info == null) {
			if (other.info != null) {
				return false;
			}
		} else if (!info.equals(other.info)) {
			return false;
		}
		if (organization == null) {
			if (other.organization != null) {
				return false;
			}
		} else if (!organization.equals(other.organization)) {
			return false;
		}
		if (port != other.port) {
			return false;
		}
		if (position == null) {
			if (other.position != null) {
				return false;
			}
		} else if (!position.equals(other.position)) {
			return false;
		}
		if (updateTime != other.updateTime) {
			return false;
		}
		
		if (enable != other.enable) {
			return false;
		}
		
		return true;
	}

	/**
	 * @return the enable
	 */
	public int getEnable() {
		return enable;
	}

	/**
	 * @param enable the enable to set
	 */
	public void setEnable(int enable) {
		this.enable = enable;
	}
}
