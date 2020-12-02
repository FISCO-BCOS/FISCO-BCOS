/**
 *  Copyright (C) 2020 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  @file RefDataContainer.cpp
 *  @date 2020-12-1
 */

#include <libutilities/Common.h>
#include <libutilities/RefDataContainer.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <iostream>
#include <vector>


using namespace bcos;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(VectorRefTests, TestOutputHelperFixture)

/// test constructor of RefDataContainer:
/// case1: empty RefDataContainer
/// case2: pointing to part data of given object
BOOST_AUTO_TEST_CASE(testConstructor)
{
    // case1: empty RefDataContainer
    RefDataContainer<int> ref;
    BOOST_CHECK(ref.data() == nullptr);
    BOOST_CHECK(ref.count() == 0);
    BOOST_CHECK(ref.empty() == true);

    // case2: pointing to part data of given object
    unsigned int size = 10;
    char* test_char = new char[size];
    RefDataContainer<char> char_ref(test_char, size);
    BOOST_CHECK(char_ref.empty() == false);
    BOOST_CHECK(char_ref.data() == test_char);
    BOOST_CHECK(char_ref.count() == size);
    for (unsigned int i = 0; i < size - 1; i++)
    {
        test_char[i] = '0' + i;
        BOOST_CHECK(char_ref[i] == test_char[i]);
    }
    if (size > 4)
    {
        char_ref[3] = 'a';
        BOOST_CHECK(test_char[3] == 'a');
    }
    delete[] test_char;
}

/// case3: create a new RefDataContainer pointing to the data part of a string (given as reference).
/// case4: create a new RefDataContainer pointing to the data part of a string (given as pointer).
BOOST_AUTO_TEST_CASE(testConstructString)
{
    // test reference
    std::string str = "abcd";
    std::string str2 = "abcd";
    RefDataContainer<const char> string_ref(str);
    BOOST_CHECK_EQUAL(string_ref.data(), str);
    BOOST_CHECK_EQUAL(string_ref.size(), str.length());
    BOOST_CHECK_EQUAL(str, string_ref.toString());
    BOOST_CHECK((RefDataContainer<const char>)(&str) == string_ref);
    BOOST_CHECK((RefDataContainer<const char>)(&str2) != string_ref);
    // test retarget
    string_ref.retarget(str2.c_str(), str2.length());
    BOOST_CHECK((RefDataContainer<const char>)(&str) != string_ref);
    BOOST_CHECK((RefDataContainer<const char>)(&str2) == string_ref);

    // test pointer
    std::string* pstr = &str;
    RefDataContainer<const char> pstring_ref(pstr);
    BOOST_CHECK_EQUAL(pstring_ref.data(), *pstr);
    BOOST_CHECK_EQUAL(pstring_ref.size(), pstr->length());
    BOOST_CHECK_EQUAL(*pstr, pstring_ref.toString());
    BOOST_CHECK((RefDataContainer<const char>)(pstr) == pstring_ref);
    pstr = &str2;
    BOOST_CHECK((RefDataContainer<const char>)(pstr) != pstring_ref);
    // to vector
    // test retarget
    pstring_ref.retarget(str2.c_str(), str2.length());
    BOOST_CHECK((RefDataContainer<const char>)(pstr) == pstring_ref);
    pstr = &str;
    BOOST_CHECK((RefDataContainer<const char>)(pstr) != pstring_ref);
}

/// case4: create a new RefDataContainer pointing to the data part of a vector (given as pointer).
/// case5: create a new RefDataContainer pointing to the data part of a string (given as reference).
BOOST_AUTO_TEST_CASE(testConstructVector)
{
    unsigned int size = 10;
    std::vector<int> int_v1(size);
    std::vector<int> int_v2(size);
    RefDataContainer<int> ref_v1(&int_v1);
    for (unsigned i = 0; i < size; i++)
    {
        ref_v1[i] = i;
        int_v2[i] = i;
        BOOST_CHECK(ref_v1[i] == (signed)(i));
    }
    // vector to RefDataContainer
    BOOST_CHECK(RefDataContainer<int>(&int_v1) == ref_v1);
}

BOOST_AUTO_TEST_CASE(testContentsEqual)
{
    std::vector<int> v1(10, 19);
    std::vector<int> v2(10, 19);
    RefDataContainer<int> ref_v1(&v1);
    BOOST_CHECK(RefDataContainer<int>(&v2) != ref_v1);
}

BOOST_AUTO_TEST_CASE(testEmptyCropped)
{
    std::vector<int> v1(1, 19);
    RefDataContainer<int> origin = ref(v1);
    RefDataContainer<int> ret = origin.getCroppedData(2, 10);
    BOOST_CHECK(ret.empty() == true);
}

// test function "Overlap"
BOOST_AUTO_TEST_CASE(testOverlap)
{
    /// ====test string Overlap ====
    unsigned int size = 10;
    std::string str(size, 0);
    for (unsigned i = 0; i < size; i++)
    {
        str[i] = '0' + i;
    }
    RefDataContainer<const char> str_ref(str.c_str());
    // all overlap
    RefDataContainer<const char> sub_str_ref1(
        str_ref.data() + 1, (size_t)std::min((size_t)str.length() - 2, (size_t)5) / sizeof(char));
    // part overlap
    RefDataContainer<const char> sub_str_ref2(
        str_ref.data() + 3, (size_t)std::min((size_t)str.length() - 2, (size_t)5) / sizeof(char));
    // std::string tmp_str = "abcd";
    // RefDataContainer<const char> non_overlap_ref(tmp_str);
    BOOST_CHECK(str_ref.dataOverlap(sub_str_ref1) == true);
    BOOST_CHECK(str_ref.dataOverlap(sub_str_ref2) == true);
    BOOST_CHECK(sub_str_ref1.dataOverlap(sub_str_ref2) == true);

    /// ====test vector Overlap ====
    std::vector<int> v1_int(size);
    RefDataContainer<int> v1_ref(&v1_int);
    for (unsigned i = 0; i < size; i++)
    {
        v1_ref[i] = i;
    }

    RefDataContainer<int> v2_ref(&(*v1_int.begin()) + (size_t)std::min(v1_int.size(), (size_t)(1)),
        std::min(v1_int.size(), (size_t)(5)));
    // test overlap
    BOOST_CHECK(v2_ref.dataOverlap(v1_ref));
    std::vector<int> tmp_v;
    // test no-overlap
    BOOST_CHECK(v2_ref.dataOverlap(RefDataContainer<int>(&tmp_v)) == false);
}
BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
