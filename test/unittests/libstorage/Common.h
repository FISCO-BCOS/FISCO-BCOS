/*
 * Common.h
 *
 *  Created on: 2018年6月1日
 *      Author: 65193
 */

#pragma once

#ifndef BOOST_TEST
#define BOOST_TEST(exp) BOOST_CHECK_EQUAL((exp), true)
#endif

#define BOOST_TEST_TRUE(exp) BOOST_CHECK_EQUAL((exp), true)
