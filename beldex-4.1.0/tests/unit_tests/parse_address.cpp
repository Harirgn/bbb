// Copyright (c) 2021, The Oxen Project
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// 
// Parts of this file are originally copyright (c) 2012-2013 The Cryptonote developers

#include "gtest/gtest.h"

#include "cryptonote_basic/cryptonote_format_utils.h"

using namespace cryptonote;

TEST(ADDRESS, empty) { EXPECT_FALSE(cryptonote::is_valid_address("", cryptonote::MAINNET)); }

TEST(ADDRESS, short_mainnet_address) { EXPECT_FALSE(cryptonote::is_valid_address("bxda4NxJymoYHRNRh7FjmDCZ912cFsKH7QjJprjNRyXY5Wh2unz1aKq1PPnxKyQBMu5ForNfRkioDGNSi7mqwpnr1KYXQRHfK", cryptonote::MAINNET)); }
TEST(ADDRESS, long_mainnet_address) { EXPECT_FALSE(cryptonote::is_valid_address("bxda4NxJymoYHRNRh7FjmDCZ912cFsKH7QjJprjNRyXY5Wh2unz1aKq1PPnxKyQBMu5ForNfRkioDGNSi7mqwpnr1KYXQRHfK", cryptonote::MAINNET)); }

TEST(ADDRESS, valid_test_address) { EXPECT_TRUE(cryptonote::is_valid_address("9uDxTecU9r1LuSk12HpU43huCrfR4e7LM6zbvesbHDzCWxzcK9tYKhY2RM9ovD5NrLikxkBa8iakYMcbfC5pZuTc2Vnf1K7", cryptonote::TESTNET)); }
TEST(ADDRESS, valid_mainnet_address) { EXPECT_TRUE(cryptonote::is_valid_address("bxda4NxJymoYHRNRh7FjmDCZ912cFsKH7QjJprjNRyXY5Wh2unz1aKq1PPnxKyQBMu5ForNfRkioDGNSi7mqwpnr1KYXQRHfK", cryptonote::MAINNET)); }
