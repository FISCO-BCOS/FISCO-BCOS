/**
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @brief
 *
 * @file Assertions.cpp
 * @author: yujiechen
 * @date 2018-08-24
 */

#include <libutilities/Common.h>
#include <libutilities/vector_ref.h>
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

/// test constructor of vector_ref:
/// case1: empty vector_ref
/// case2: pointing to part data of given object
BOOST_AUTO_TEST_CASE(testConstructor)
{
    // case1: empty vector_ref
    vector_ref<int> ref;
    BOOST_CHECK(ref.data() == nullptr);
    BOOST_CHECK(ref.count() == 0);
    BOOST_CHECK(ref.empty() == true);

    // case2: pointing to part data of given object
    unsigned int size = 10;
    char* test_char = new char[size];
    vector_ref<char> char_ref(test_char, size);
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

/// case3: create a new vector_ref pointing to the data part of a string (given as reference).
/// case4: create a new vector_ref pointing to the data part of a string (given as pointer).
BOOST_AUTO_TEST_CASE(testConstructString)
{
    // test reference
    std::string str = "abcd";
    std::string str2 = "abcd";
    vector_ref<const char> string_ref(str);
    BOOST_CHECK_EQUAL(string_ref.data(), str);
    BOOST_CHECK_EQUAL(string_ref.size(), str.length());
    BOOST_CHECK_EQUAL(str, string_ref.toString());
    BOOST_CHECK((vector_ref<const char>)(&str) == string_ref);
    BOOST_CHECK((vector_ref<const char>)(&str2) != string_ref);
    // test retarget
    string_ref.retarget(str2.c_str(), str2.length());
    BOOST_CHECK((vector_ref<const char>)(&str) != string_ref);
    BOOST_CHECK((vector_ref<const char>)(&str2) == string_ref);

    // test pointer
    std::string* pstr = &str;
    vector_ref<const char> pstring_ref(pstr);
    BOOST_CHECK_EQUAL(pstring_ref.data(), *pstr);
    BOOST_CHECK_EQUAL(pstring_ref.size(), pstr->length());
    BOOST_CHECK_EQUAL(*pstr, pstring_ref.toString());
    BOOST_CHECK((vector_ref<const char>)(pstr) == pstring_ref);
    pstr = &str2;
    BOOST_CHECK((vector_ref<const char>)(pstr) != pstring_ref);
    // to vector
    // test retarget
    pstring_ref.retarget(str2.c_str(), str2.length());
    BOOST_CHECK((vector_ref<const char>)(pstr) == pstring_ref);
    pstr = &str;
    BOOST_CHECK((vector_ref<const char>)(pstr) != pstring_ref);
}

/// case4: create a new vector_ref pointing to the data part of a vector (given as pointer).
/// case5: create a new vector_ref pointing to the data part of a string (given as reference).
BOOST_AUTO_TEST_CASE(testConstructVector)
{
    unsigned int size = 10;
    std::vector<int> int_v1(size);
    std::vector<int> int_v2(size);
    vector_ref<int> ref_v1(&int_v1);
    for (unsigned i = 0; i < size; i++)
    {
        ref_v1[i] = i;
        int_v2[i] = i;
        BOOST_CHECK(ref_v1[i] == (signed)(i));
    }
    // vector to vector_ref
    BOOST_CHECK(vector_ref<int>(&int_v1) == ref_v1);
    // vector_ref to vector
    BOOST_CHECK(ref_v1.toVector() == int_v1);
    BOOST_CHECK(ref_v1.toVector() == int_v2);
}

// test function "contentsEqual"
BOOST_AUTO_TEST_CASE(testContentsEqual)
{
    std::vector<int> v1(10, 19);
    std::vector<int> v2(10, 19);
    vector_ref<int> ref_v1(&v1);
    BOOST_CHECK(vector_ref<int>(&v2) != ref_v1);
    // test empty vec_ref
    vector_ref<int> empty_vec;
    vector_ref<int> empty_vec2;
}

BOOST_AUTO_TEST_CASE(testEmptyCropped)
{
    std::vector<int> v1(1, 19);
    vector_ref<int> origin = ref(v1);
    vector_ref<int> ret = origin.cropped(2, 10);
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
    vector_ref<const char> str_ref(str.c_str());
    // all overlap
    vector_ref<const char> sub_str_ref1(
        str_ref.data() + 1, (size_t)std::min((size_t)str.length() - 2, (size_t)5) / sizeof(char));
    // part overlap
    vector_ref<const char> sub_str_ref2(
        str_ref.data() + 3, (size_t)std::min((size_t)str.length() - 2, (size_t)5) / sizeof(char));
    // std::string tmp_str = "abcd";
    // vector_ref<const char> non_overlap_ref(tmp_str);
    BOOST_CHECK(str_ref.overlapsWith(sub_str_ref1) == true);
    BOOST_CHECK(str_ref.overlapsWith(sub_str_ref2) == true);
    BOOST_CHECK(sub_str_ref1.overlapsWith(sub_str_ref2) == true);

    /// ====test vector Overlap ====
    std::vector<int> v1_int(size);
    vector_ref<int> v1_ref(&v1_int);
    for (unsigned i = 0; i < size; i++)
    {
        v1_ref[i] = i;
    }

    vector_ref<int> v2_ref(&(*v1_int.begin()) + (size_t)std::min(v1_int.size(), (size_t)(1)),
        std::min(v1_int.size(), (size_t)(5)));
    // test overlap
    BOOST_CHECK(v2_ref.overlapsWith(v1_ref));
    std::vector<int> tmp_v;
    // test no-overlap
    BOOST_CHECK(v2_ref.overlapsWith(vector_ref<int>(&tmp_v)) == false);
}

// test function "populate"
BOOST_AUTO_TEST_CASE(testPopulateAndCopyPopulate)
{
    std::string str = "01234567890";
    std::vector<char> src_v(str.begin(), str.end());
    std::vector<char> dst_v(str.begin() + std::min((size_t)str.length(), (size_t)2),
        str.begin() +
            std::max(std::min((size_t)str.length(), (size_t)2), (size_t)str.length() - (size_t)3));

    std::vector<char> tmp_src_v(src_v.begin(), src_v.end());
    std::vector<char> tmp_dst_v(dst_v.begin(), dst_v.end());

    ref(tmp_src_v).copyTo(ref(tmp_dst_v));
    ref(tmp_dst_v).populate(ref(tmp_src_v));
    ref(tmp_dst_v).populate(ref(tmp_dst_v));
}

// test function "testCleanse"
BOOST_AUTO_TEST_CASE(testCleanse) {}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
