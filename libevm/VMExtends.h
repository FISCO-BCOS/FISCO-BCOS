/** @file VMExtends.h
 * @author ximi
 * @date 2017.05.24
 */

#pragma once

#include <unordered_map>
#include <functional>
#include <libdevcore/Exceptions.h>

namespace dev
{
namespace eth
{

using ExtendExecutor = std::function<uint32_t(std::string const& _param, char* _ptmem)>;

DEV_SIMPLE_EXCEPTION(ExtendExecutorNotFound);

class ExtendRegistrar
{
public:
	/// Get the executor object for @a _name function or @throw ExtendExecutorNotFound if not found.
	static ExtendExecutor const& executor(std::string const& _name);
	static std::string parseExtend(std::string const& _param, int & _paramOff);

	/// Register an executor. In general just use ETH_REGISTER_EXTEND.
	static ExtendExecutor registerExtend(std::string const& _name, ExtendExecutor const& _exec) { return (get()->m_execs[_name] = _exec); }
	/// Unregister an executor. Shouldn't generally be necessary.
	static void unregisterExtend(std::string const& _name) { get()->m_execs.erase(_name); }

private:
	static ExtendRegistrar* get() { if (!s_this) s_this = new ExtendRegistrar; return s_this; }

	std::unordered_map<std::string, ExtendExecutor> m_execs;
	static ExtendRegistrar* s_this;
};

#define ETH_REGISTER_EXTEND(Name) static uint32_t __eth_registerExtendFunction ## Name(std::string const& _param, char* _ptmem); static ExtendExecutor __eth_registerExtendFactory ## Name = ::dev::eth::ExtendRegistrar::registerExtend(#Name, &__eth_registerExtendFunction ## Name); static uint32_t __eth_registerExtendFunction ## Name

}
}
