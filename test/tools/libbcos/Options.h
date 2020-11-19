/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @brief Class for handling test-fisco-bcos custom options
 *
 * @file Options.h
 * @author yujiechen
 * @date 2018-08-28
 */

#pragma once
#include <libutilities/Exceptions.h>
#include <libexecutive/VMFactory.h>
namespace bcos
{
namespace test
{
DERIVE_BCOS_EXCEPTION(InvalidOption);

class Options
{
public:
    std::string testpath;  ///< Custom test folder path

    /// Test selection
    /// @{
    bool all = false;  ///< Running every test, including time consuming ones.
    /// @}
    /// vm related setting
    std::string vm_name = "interpreter";
    std::vector<std::pair<std::string, std::string>> evmc_options;

    /// The first time used, options are parsed with argc, argv
    /// static Options const& get(int argc = 0, const char** argv = 0);
    static Options const& get();
    static Options const& get(int argc, char** argv);

private:
    void setVmOptions(int argc, char** argv);
    // Options(int argc = 0, const char** argv = 0);
    Options(int argc = 0, char** argv = 0);
    Options(Options const&) = delete;
};

}  // namespace test
}  // namespace bcos
