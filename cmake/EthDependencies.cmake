#------------------------------------------------------------------------------
# all dependencies that are not directly included in the FISCO-BCOS distribution are defined here
# for this to work, download the dependency via the cmake script in extdep or install them manually!
# ------------------------------------------------------------------------------
# This file is part of FISCO-BCOS.
#
# FISCO-BCOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# FISCO-BCOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
#
# (c) 2016-2018 fisco-dev contributors.
#------------------------------------------------------------------------------

function(eth_show_dependency DEP NAME)
    get_property(DISPLAYED GLOBAL PROPERTY ETH_${DEP}_DISPLAYED)
    if (NOT DISPLAYED)
        set_property(GLOBAL PROPERTY ETH_${DEP}_DISPLAYED TRUE)
        message(STATUS "${NAME} headers: ${${DEP}_INCLUDE_DIRS}")
        message(STATUS "${NAME} lib   : ${${DEP}_LIBRARIES}")
        if (NOT("${${DEP}_DLLS}" STREQUAL ""))
            message(STATUS "${NAME} dll   : ${${DEP}_DLLS}")
        endif() 
    endif()
endfunction()