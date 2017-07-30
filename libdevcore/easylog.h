#ifndef EASYLOG_H
#define EASYLOG_H

#include "easylogging++.h"
#include <map>
#include <string>
using namespace std;

enum EWarningType
{
	GasUsedWarning=0,
	ConvergenceWarning=1,
	OnChainTimeWarning,
	PackageTimeWarning,
	ChangeViewWarning,
};

const map<EWarningType, string> WarningMap = {
	{GasUsedWarning, "GasUsedWarning"},
	{ConvergenceWarning, "ConvergenceWarning"},
	{OnChainTimeWarning, "OnChainTimeWarning"},
	{PackageTimeWarning, "PackageTimeWarning"},
	{ChangeViewWarning, "ChangeViewWarning"},
};

const int COnChainTimeLimit = 1000; //上链耗时超1000ms告警
const int CGasUsedLimit = 90; //gas 超过阈值的90%告警
const int CConvergenceLimit = 1000; //共识耗时超过1000ms告警
const int CPackageTimeLimit = 2000; //打包耗时超过2000ms告警


#define MY_CUSTOM_LOGGER(LEVEL) CLOG(LEVEL, "default", "fileLogger")
#undef LOG
#define LOG(LEVEL) CLOG(LEVEL, "default", "fileLogger")
#define LOGCOMWARNING LOG(WARNING)<<"common|"
#endif