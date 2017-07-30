#pragma once

#include "DfsBase.h"


namespace dev
{

namespace rpc
{

namespace fs
{

class DfsFileRecorder
{
public:
	DfsFileRecorder();
	~DfsFileRecorder();
	

public:
	//write the directory, save to the "fileRecrod.dat"
	static int writeRecord(const DfsFileTask& task);

	
};

}
}
}
