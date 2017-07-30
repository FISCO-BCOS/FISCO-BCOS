/**  
 * @Title: FileInfo.java

 * @date: 2017年3月2日 下午3:53:21

 * @version: V1.0  

 */


package com.dfs.message;

/**
 * @author Administrator
 *
 */
public class FileInfo {
	private    String      	id;/** 文件ID */
	private    String      	container;/** 容器ID（可选） */
	private    String      	filename;/** 文件名称 */
	private    int     		updateTime;/** 更新时间 */
	private    int     		size;/** 文件大小 */
	private    String      	owner;/** 文件owner */
	private    String      	file_hash;/** md5 hash */
	private    String      	src_node;/** 上传源节点ID */
	private    String      	node_group;/** 节点群组ID */
	private		int       	type;/** 文件类型 */
	private    String      	priviliges;/** 文件权限 */
	private    String      	info;/** 文件信息 */
	private 	int			state;/** 文件状态 */
	
	
	public FileInfo() {
	}


	/**
	 * @return the id
	 */
	public String getId() {
		return id;
	}


	/**
	 * @return the container
	 */
	public String getContainer() {
		return container;
	}


	/**
	 * @return the filename
	 */
	public String getFilename() {
		return filename;
	}


	/**
	 * @return the updateTime
	 */
	public int getUpdateTime() {
		return updateTime;
	}


	/**
	 * @return the size
	 */
	public int getSize() {
		return size;
	}


	/**
	 * @return the owner
	 */
	public String getOwner() {
		return owner;
	}


	/**
	 * @return the file_hash
	 */
	public String getFile_hash() {
		return file_hash;
	}


	/**
	 * @return the src_node
	 */
	public String getSrc_node() {
		return src_node;
	}


	/**
	 * @return the node_group
	 */
	public String getNode_group() {
		return node_group;
	}


	/**
	 * @return the type
	 */
	public int getType() {
		return type;
	}


	/**
	 * @return the priviliges
	 */
	public String getPriviliges() {
		return priviliges;
	}


	/**
	 * @return the info
	 */
	public String getInfo() {
		return info;
	}


	/**
	 * @return the state
	 */
	public int getState() {
		return state;
	}


	/**
	 * @param id the id to set
	 */
	public void setId(String id) {
		this.id = id;
	}


	/**
	 * @param container the container to set
	 */
	public void setContainer(String container) {
		this.container = container;
	}


	/**
	 * @param filename the filename to set
	 */
	public void setFilename(String filename) {
		this.filename = filename;
	}


	/**
	 * @param updateTime the updateTime to set
	 */
	public void setUpdateTime(int updateTime) {
		this.updateTime = updateTime;
	}


	/**
	 * @param size the size to set
	 */
	public void setSize(int size) {
		this.size = size;
	}


	/**
	 * @param owner the owner to set
	 */
	public void setOwner(String owner) {
		this.owner = owner;
	}


	/**
	 * @param file_hash the file_hash to set
	 */
	public void setFile_hash(String file_hash) {
		this.file_hash = file_hash;
	}


	/**
	 * @param src_node the src_node to set
	 */
	public void setSrc_node(String src_node) {
		this.src_node = src_node;
	}


	/**
	 * @param node_group the node_group to set
	 */
	public void setNode_group(String node_group) {
		this.node_group = node_group;
	}


	/**
	 * @param type the type to set
	 */
	public void setType(int type) {
		this.type = type;
	}


	/**
	 * @param priviliges the priviliges to set
	 */
	public void setPriviliges(String priviliges) {
		this.priviliges = priviliges;
	}


	/**
	 * @param info the info to set
	 */
	public void setInfo(String info) {
		this.info = info;
	}


	/**
	 * @param state the state to set
	 */
	public void setState(int state) {
		this.state = state;
	}


	/* (non-Javadoc)
	 * @see java.lang.Object#hashCode()
	 */
	@Override
	public int hashCode() {
		final int prime = 31;
		int result = 1;
		result = prime * result + ((container == null) ? 0 : container.hashCode());
		result = prime * result + ((file_hash == null) ? 0 : file_hash.hashCode());
		result = prime * result + ((filename == null) ? 0 : filename.hashCode());
		result = prime * result + ((id == null) ? 0 : id.hashCode());
		result = prime * result + ((info == null) ? 0 : info.hashCode());
		result = prime * result + ((node_group == null) ? 0 : node_group.hashCode());
		result = prime * result + ((owner == null) ? 0 : owner.hashCode());
		result = prime * result + ((priviliges == null) ? 0 : priviliges.hashCode());
		result = prime * result + size;
		result = prime * result + ((src_node == null) ? 0 : src_node.hashCode());
		result = prime * result + state;
		result = prime * result + type;
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
		if (!(obj instanceof FileInfo)) {
			return false;
		}
		FileInfo other = (FileInfo) obj;
		if (container == null) {
			if (other.container != null) {
				return false;
			}
		} else if (!container.equals(other.container)) {
			return false;
		}
		if (file_hash == null) {
			if (other.file_hash != null) {
				return false;
			}
		} else if (!file_hash.equals(other.file_hash)) {
			return false;
		}
		if (filename == null) {
			if (other.filename != null) {
				return false;
			}
		} else if (!filename.equals(other.filename)) {
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
		if (node_group == null) {
			if (other.node_group != null) {
				return false;
			}
		} else if (!node_group.equals(other.node_group)) {
			return false;
		}
		if (owner == null) {
			if (other.owner != null) {
				return false;
			}
		} else if (!owner.equals(other.owner)) {
			return false;
		}
		if (priviliges == null) {
			if (other.priviliges != null) {
				return false;
			}
		} else if (!priviliges.equals(other.priviliges)) {
			return false;
		}
		if (size != other.size) {
			return false;
		}
		if (src_node == null) {
			if (other.src_node != null) {
				return false;
			}
		} else if (!src_node.equals(other.src_node)) {
			return false;
		}
		if (state != other.state) {
			return false;
		}
		if (type != other.type) {
			return false;
		}
		if (updateTime != other.updateTime) {
			return false;
		}
		return true;
	}
}
