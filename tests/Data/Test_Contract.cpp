/*
 * Copyright (c) 2018 Zilliqa
 * This source code is being disclosed to you solely for the purpose of your
 * participation in testing Zilliqa. You may view, compile and run the code for
 * that purpose and pursuant to the protocols and algorithms that are programmed
 * into, and intended by, the code. You may not do anything else with the code
 * without express permission from Zilliqa Research Pte. Ltd., including
 * modifying or publishing the code (or any part of it), and developing or
 * forming another public or private blockchain network. This source code is
 * provided 'as is' and no warranties are given as to title or non-infringement,
 * merchantability or fitness for purpose and, to the extent permitted by law,
 * all liability for your use of the code is disclaimed. Some programs in this
 * code are governed by the GNU General Public License v3.0 (available at
 * https://www.gnu.org/licenses/gpl-3.0.en.html) ('GPLv3'). The programs that
 * are governed by GPLv3.0 are those programs that are located in the folders
 * src/depends and tests/depends and which include a reference to GPLv3 in their
 * program files.
 */

#include <array>
#include <regex>
#include <string>
#include <vector>

#include <openssl/rand.h>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>

#include "common/Constants.h"
#include "depends/common/CommonIO.h"
#include "libCrypto/Schnorr.h"
#include "libCrypto/Sha2.h"
#include "libData/AccountData/Account.h"
#include "libData/AccountData/AccountStore.h"
#include "libData/AccountData/Transaction.h"
#include "libData/AccountData/TransactionReceipt.h"
#include "libPersistence/ContractStorage.h"
#include "libUtils/DataConversion.h"
#include "libUtils/Logger.h"
#include "libUtils/TimeUtils.h"

#include "ScillaTestUtil.h"

#define BOOST_TEST_MODULE contracttest
#define BOOST_TEST_DYN_LINK
#include <boost/test/unit_test.hpp>

using namespace boost::multiprecision;
using namespace std;

BOOST_AUTO_TEST_SUITE(contracttest)

PrivKey priv1(
    DataConversion::HexStrToUint8Vec(
        "1658F915F3F9AE35E6B471B7670F53AD1A5BE15D7331EC7FD5E503F21D3450C8"),
    0),
    priv2(
        DataConversion::HexStrToUint8Vec(
            "0FC87BC5ACF5D1243DE7301972B9649EE31688F291F781396B0F67AD98A88147"),
        0),
    priv3(
        DataConversion::HexStrToUint8Vec(
            "0AB52CF5D3F9A1E730243DB96419729EE31688F29B0F67AD98A881471F781396"),
        0);

// Create Transaction to create contract
BOOST_AUTO_TEST_CASE(testCrowdfunding) {
  KeyPair owner(priv1, {priv1}), donor1(priv2, {priv2}), donor2(priv3, {priv3});
  Address ownerAddr, donor1Addr, donor2Addr, contrAddr;
  uint64_t nonce = 0;

  INIT_STDOUT_LOGGER();

  LOG_MARKER();

  if (SCILLA_ROOT.empty()) {
    LOG_GENERAL(WARNING, "SCILLA_ROOT not set to run Test_Contract");
    return;
  }

  AccountStore::GetInstance().Init();

  ownerAddr = Account::GetAddressFromPublicKey(owner.second);
  donor1Addr = Account::GetAddressFromPublicKey(donor1.second);
  donor2Addr = Account::GetAddressFromPublicKey(donor2.second);

  AccountStore::GetInstance().AddAccount(ownerAddr, {2000000, nonce});
  AccountStore::GetInstance().AddAccount(donor1Addr, {2000000, nonce});
  AccountStore::GetInstance().AddAccount(donor2Addr, {2000000, nonce});

  contrAddr = Account::GetAddressForContract(ownerAddr, nonce);
  LOG_GENERAL(INFO, "CrowdFunding Address: " << contrAddr);

  // Deploying the contract can use data from the 1st Scilla test.
  ScillaTestUtil::ScillaTest t1;
  if (!ScillaTestUtil::GetScillaTest(t1, "crowdfunding", 1)) {
    LOG_GENERAL(WARNING, "Unable to fetch test crowdfunding_1.");
    return;
  }

  // Replace owner address in init.json.
  for (auto& it : t1.init) {
    if (it["vname"] == "owner") {
      it["value"] = "0x" + ownerAddr.hex();
    }
  }
  // and remove _creation_block (automatic insertion later).
  ScillaTestUtil::RemoveCreationBlockFromInit(t1.init);

  uint64_t bnum = ScillaTestUtil::GetBlockNumberFromJson(t1.blockchain);

  // Transaction to deploy contract.
  std::string initStr = JSONUtils::convertJsontoStr(t1.init);
  bytes data(initStr.begin(), initStr.end());
  Transaction tx0(1, nonce, NullAddress, owner, 0, PRECISION_MIN_VALUE, 5000,
                  t1.code, data);
  TransactionReceipt tr0;
  AccountStore::GetInstance().UpdateAccounts(bnum, 1, true, tx0, tr0);
  Account* account = AccountStore::GetInstance().GetAccount(contrAddr);
  // We should now have a new account.
  BOOST_CHECK_MESSAGE(account != nullptr,
                      "Error with creation of contract account");
  nonce++;

  /* ------------------------------------------------------------------- */

  // Execute message_1, the Donate transaction.
  bytes dataDonate;
  uint64_t amount = ScillaTestUtil::PrepareMessageData(t1.message, dataDonate);

  Transaction tx1(1, nonce, contrAddr, donor1, amount, PRECISION_MIN_VALUE,
                  5000, {}, dataDonate);
  TransactionReceipt tr1;
  if (AccountStore::GetInstance().UpdateAccounts(bnum, 1, true, tx1, tr1)) {
    nonce++;
  }

  uint128_t contrBal = AccountStore::GetInstance().GetBalance(contrAddr);
  uint128_t oBal = ScillaTestUtil::GetBalanceFromOutput();

  LOG_GENERAL(INFO, "[Call1] Owner balance: "
                        << AccountStore::GetInstance().GetBalance(ownerAddr));
  LOG_GENERAL(INFO, "[Call1] Donor1 balance: "
                        << AccountStore::GetInstance().GetBalance(donor1Addr));
  LOG_GENERAL(INFO, "[Call1] Donor2 balance: "
                        << AccountStore::GetInstance().GetBalance(donor2Addr));
  LOG_GENERAL(INFO, "[Call1] Contract balance (scilla): " << contrBal);
  LOG_GENERAL(INFO, "[Call1] Contract balance (blockchain): " << oBal);
  BOOST_CHECK_MESSAGE(contrBal == oBal && contrBal == amount,
                      "Balance mis-match after Donate");

  /* ------------------------------------------------------------------- */

  // Do another donation from donor2
  ScillaTestUtil::ScillaTest t2;
  if (!ScillaTestUtil::GetScillaTest(t2, "crowdfunding", 2)) {
    LOG_GENERAL(WARNING, "Unable to fetch test crowdfunding_2.");
    return;
  }

  uint64_t bnum2 = ScillaTestUtil::GetBlockNumberFromJson(t2.blockchain);
  // Execute message_2, the Donate transaction.
  bytes dataDonate2;
  uint64_t amount2 =
      ScillaTestUtil::PrepareMessageData(t2.message, dataDonate2);

  Transaction tx2(1, nonce, contrAddr, donor2, amount2, PRECISION_MIN_VALUE,
                  5000, {}, dataDonate2);
  TransactionReceipt tr2;
  if (AccountStore::GetInstance().UpdateAccounts(bnum2, 1, true, tx2, tr2)) {
    nonce++;
  }

  uint128_t contrBal2 = AccountStore::GetInstance().GetBalance(contrAddr);
  uint128_t oBal2 = ScillaTestUtil::GetBalanceFromOutput();

  LOG_GENERAL(INFO, "[Call2] Owner balance: "
                        << AccountStore::GetInstance().GetBalance(ownerAddr));
  LOG_GENERAL(INFO, "[Call2] Donor1 balance: "
                        << AccountStore::GetInstance().GetBalance(donor1Addr));
  LOG_GENERAL(INFO, "[Call2] Donor2 balance: "
                        << AccountStore::GetInstance().GetBalance(donor2Addr));
  LOG_GENERAL(INFO, "[Call2] Contract balance (scilla): " << contrBal2);
  LOG_GENERAL(INFO, "[Call2] Contract balance (blockchain): " << oBal2);
  BOOST_CHECK_MESSAGE(contrBal2 == oBal2 && contrBal2 == amount + amount2,
                      "Balance mis-match after Donate2");

  /* ------------------------------------------------------------------- */

  // Let's try donor1 donating again, it shouldn't have an impact.
  // Execute message_3, the unsuccessful Donate transaction.
  Transaction tx3(1, nonce, contrAddr, donor1, amount, PRECISION_MIN_VALUE,
                  5000, {}, dataDonate);
  TransactionReceipt tr3;
  if (AccountStore::GetInstance().UpdateAccounts(bnum, 1, true, tx3, tr3)) {
    nonce++;
  }
  uint128_t contrBal3 = AccountStore::GetInstance().GetBalance(contrAddr);
  uint128_t oBal3 = ScillaTestUtil::GetBalanceFromOutput();

  LOG_GENERAL(INFO, "[Call3] Owner balance: "
                        << AccountStore::GetInstance().GetBalance(ownerAddr));
  LOG_GENERAL(INFO, "[Call3] Donor1 balance: "
                        << AccountStore::GetInstance().GetBalance(donor1Addr));
  LOG_GENERAL(INFO, "[Call3] Donor2 balance: "
                        << AccountStore::GetInstance().GetBalance(donor2Addr));
  LOG_GENERAL(INFO, "[Call3] Contract balance (scilla): " << contrBal3);
  LOG_GENERAL(INFO, "[Call3] Contract balance (blockchain): " << oBal3);
  BOOST_CHECK_MESSAGE(contrBal3 == contrBal2,
                      "Balance mis-match after Donate3");

  /* ------------------------------------------------------------------- */

  // Owner tries to get fund, fails
  ScillaTestUtil::ScillaTest t4;
  if (!ScillaTestUtil::GetScillaTest(t4, "crowdfunding", 4)) {
    LOG_GENERAL(WARNING, "Unable to fetch test crowdfunding_4.");
    return;
  }

  uint64_t bnum4 = ScillaTestUtil::GetBlockNumberFromJson(t4.blockchain);
  // Execute message_4, the Donate transaction.
  bytes data4;
  uint64_t amount4 = ScillaTestUtil::PrepareMessageData(t4.message, data4);

  Transaction tx4(1, nonce, contrAddr, owner, amount4, PRECISION_MIN_VALUE,
                  5000, {}, data4);
  TransactionReceipt tr4;
  if (AccountStore::GetInstance().UpdateAccounts(bnum4, 1, true, tx4, tr4)) {
    nonce++;
  }

  uint128_t contrBal4 = AccountStore::GetInstance().GetBalance(contrAddr);
  uint128_t oBal4 = ScillaTestUtil::GetBalanceFromOutput();

  LOG_GENERAL(INFO, "[Call4] Owner balance: "
                        << AccountStore::GetInstance().GetBalance(ownerAddr));
  LOG_GENERAL(INFO, "[Call4] Donor1 balance: "
                        << AccountStore::GetInstance().GetBalance(donor1Addr));
  LOG_GENERAL(INFO, "[Call4] Donor2 balance: "
                        << AccountStore::GetInstance().GetBalance(donor2Addr));
  LOG_GENERAL(INFO, "[Call4] Contract balance (scilla): " << contrBal4);
  LOG_GENERAL(INFO, "[Call4] Contract balance (blockchain): " << oBal4);
  BOOST_CHECK_MESSAGE(contrBal4 == contrBal3 && contrBal4 == oBal4,
                      "Balance mis-match after GetFunds");

  /* ------------------------------------------------------------------- */

  // Donor1 ClaimsBack his funds. Succeeds.
  ScillaTestUtil::ScillaTest t5;
  if (!ScillaTestUtil::GetScillaTest(t5, "crowdfunding", 5)) {
    LOG_GENERAL(WARNING, "Unable to fetch test crowdfunding_5.");
    return;
  }

  uint64_t bnum5 = ScillaTestUtil::GetBlockNumberFromJson(t5.blockchain);
  // Execute message_5, the Donate transaction.
  bytes data5;
  uint64_t amount5 = ScillaTestUtil::PrepareMessageData(t5.message, data5);

  Transaction tx5(1, nonce, contrAddr, donor1, amount5, PRECISION_MIN_VALUE,
                  5000, {}, data5);
  TransactionReceipt tr5;
  if (AccountStore::GetInstance().UpdateAccounts(bnum5, 1, true, tx5, tr5)) {
    nonce++;
  }

  uint128_t contrBal5 = AccountStore::GetInstance().GetBalance(contrAddr);
  uint128_t oBal5 = ScillaTestUtil::GetBalanceFromOutput();

  LOG_GENERAL(INFO, "[Call5] Owner balance: "
                        << AccountStore::GetInstance().GetBalance(ownerAddr));
  LOG_GENERAL(INFO, "[Call5] Donor1 balance: "
                        << AccountStore::GetInstance().GetBalance(donor1Addr));
  LOG_GENERAL(INFO, "[Call5] Donor2 balance: "
                        << AccountStore::GetInstance().GetBalance(donor2Addr));
  LOG_GENERAL(INFO, "[Call5] Contract balance (scilla): " << contrBal4);
  LOG_GENERAL(INFO, "[Call5] Contract balance (blockchain): " << oBal4);
  BOOST_CHECK_MESSAGE(contrBal5 == oBal5 && contrBal5 == contrBal4 - amount,
                      "Balance mis-match after GetFunds");

  /* ------------------------------------------------------------------- */
}

BOOST_AUTO_TEST_CASE(testPingPong) {
  KeyPair owner(priv1, {priv1}), ping(priv2, {priv2}), pong(priv3, {priv3});
  Address ownerAddr, pingAddr, pongAddr;
  uint64_t nonce = 0;

  INIT_STDOUT_LOGGER();

  LOG_MARKER();

  if (SCILLA_ROOT.empty()) {
    LOG_GENERAL(WARNING, "SCILLA_ROOT not set to run Test_Contract");
    return;
  }

  AccountStore::GetInstance().Init();

  ownerAddr = Account::GetAddressFromPublicKey(owner.second);
  AccountStore::GetInstance().AddAccount(ownerAddr, {2000000, nonce});

  pingAddr = Account::GetAddressForContract(ownerAddr, nonce);
  pongAddr = Account::GetAddressForContract(ownerAddr, nonce + 1);

  LOG_GENERAL(INFO,
              "Ping Address: " << pingAddr << " ; PongAddress: " << pongAddr);

  /* ------------------------------------------------------------------- */

  // Deploying the contract can use data from the 0th Scilla test.
  ScillaTestUtil::ScillaTest t0ping;
  if (!ScillaTestUtil::GetScillaTest(t0ping, "ping", 0)) {
    LOG_GENERAL(WARNING, "Unable to fetch test ping_0.");
    return;
  }

  uint64_t bnumPing = ScillaTestUtil::GetBlockNumberFromJson(t0ping.blockchain);
  ScillaTestUtil::RemoveCreationBlockFromInit(t0ping.init);

  // Transaction to deploy ping.
  std::string initStrPing = JSONUtils::convertJsontoStr(t0ping.init);
  bytes dataPing(initStrPing.begin(), initStrPing.end());
  Transaction tx0(1, nonce, NullAddress, owner, 0, PRECISION_MIN_VALUE, 5000,
                  t0ping.code, dataPing);
  TransactionReceipt tr0;
  AccountStore::GetInstance().UpdateAccounts(bnumPing, 1, true, tx0, tr0);
  Account* accountPing = AccountStore::GetInstance().GetAccount(pingAddr);
  // We should now have a new account.
  BOOST_CHECK_MESSAGE(accountPing != nullptr,
                      "Error with creation of ping account");
  nonce++;

  // Deploying the contract can use data from the 0th Scilla test.
  ScillaTestUtil::ScillaTest t0pong;
  if (!ScillaTestUtil::GetScillaTest(t0pong, "pong", 0)) {
    LOG_GENERAL(WARNING, "Unable to fetch test pong_0.");
    return;
  }

  uint64_t bnumPong = ScillaTestUtil::GetBlockNumberFromJson(t0pong.blockchain);
  ScillaTestUtil::RemoveCreationBlockFromInit(t0pong.init);

  // Transaction to deploy pong.
  std::string initStrPong = JSONUtils::convertJsontoStr(t0pong.init);
  bytes dataPong(initStrPong.begin(), initStrPong.end());
  Transaction tx1(1, nonce, NullAddress, owner, 0, PRECISION_MIN_VALUE, 5000,
                  t0pong.code, dataPong);
  TransactionReceipt tr1;
  AccountStore::GetInstance().UpdateAccounts(bnumPong, 1, true, tx1, tr1);
  Account* accountPong = AccountStore::GetInstance().GetAccount(pongAddr);
  // We should now have a new account.
  BOOST_CHECK_MESSAGE(accountPong != nullptr,
                      "Error with creation of pong account");
  nonce++;

  LOG_GENERAL(INFO, "Deployed ping and pong contracts.");

  /* ------------------------------------------------------------------- */

  // Set addresses of ping and pong in pong and ping respectively.
  bytes data;
  // Replace pong address in parameter of message.
  for (auto it = t0ping.message["params"].begin();
       it != t0ping.message["params"].end(); it++) {
    if ((*it)["vname"] == "pongAddr") {
      (*it)["value"] = "0x" + pongAddr.hex();
    }
  }
  uint64_t amount = ScillaTestUtil::PrepareMessageData(t0ping.message, data);
  Transaction tx2(1, nonce, pingAddr, owner, amount, PRECISION_MIN_VALUE, 5000,
                  {}, data);
  TransactionReceipt tr2;
  if (AccountStore::GetInstance().UpdateAccounts(bnumPing, 1, true, tx2, tr2)) {
    nonce++;
  }

  // Replace ping address in paramter of message.
  for (auto it = t0pong.message["params"].begin();
       it != t0pong.message["params"].end(); it++) {
    if ((*it)["vname"] == "pingAddr") {
      (*it)["value"] = "0x" + pingAddr.hex();
    }
  }
  amount = ScillaTestUtil::PrepareMessageData(t0pong.message, data);
  Transaction tx3(1, nonce, pongAddr, owner, amount, PRECISION_MIN_VALUE, 5000,
                  {}, data);
  TransactionReceipt tr3;
  if (AccountStore::GetInstance().UpdateAccounts(bnumPong, 1, true, tx3, tr3)) {
    nonce++;
  }

  LOG_GENERAL(INFO, "Finished setting ping-pong addresses in both contracts.");

  /* ------------------------------------------------------------------- */

  // Let's just ping now and see the ping-pong bounces.
  ScillaTestUtil::ScillaTest t1ping;
  if (!ScillaTestUtil::GetScillaTest(t1ping, "ping", 1)) {
    LOG_GENERAL(WARNING, "Unable to fetch test ping_1.");
    return;
  }

  ScillaTestUtil::PrepareMessageData(t1ping.message, data);
  Transaction tx4(1, nonce, pingAddr, owner, amount, PRECISION_MIN_VALUE, 5000,
                  {}, data);
  TransactionReceipt tr4;
  if (AccountStore::GetInstance().UpdateAccounts(bnumPing, 1, true, tx4, tr4)) {
    nonce++;
  }

  // Fetch the states of both ping and pong and verify "count" is 0.
  Json::Value pingState = accountPing->GetStorageJson();
  int pingCount = -1;
  for (auto& it : pingState) {
    if (it["vname"] == "count") {
      pingCount = atoi(it["value"].asCString());
    }
  }
  Json::Value pongState = accountPing->GetStorageJson();
  int pongCount = -1;
  for (auto& it : pongState) {
    if (it["vname"] == "count") {
      pongCount = atoi(it["value"].asCString());
    }
  }
  BOOST_CHECK_MESSAGE(pingCount == 0 && pongCount == 0,
                      "Ping / Pong did not reach count 0.");

  LOG_GENERAL(INFO, "Ping and pong bounced back to reach 0. Successful.");

  /* ------------------------------------------------------------------- */
}

BOOST_AUTO_TEST_CASE(testFungibleToken) {
  // 1. Bootstrap our test case.
  KeyPair owner(priv1, {priv1});
  Address ownerAddr, contrAddr;
  uint64_t nonce = 0;

  INIT_STDOUT_LOGGER();

  LOG_MARKER();

  if (SCILLA_ROOT.empty()) {
    LOG_GENERAL(WARNING, "SCILLA_ROOT not set to run Test_Contract");
    return;
  }

  AccountStore::GetInstance().Init();

  const uint128_t bal{std::numeric_limits<uint128_t>::max()};

  ownerAddr = Account::GetAddressFromPublicKey(owner.second);
  AccountStore::GetInstance().AddAccount(ownerAddr, {bal, nonce});

  const unsigned int numHodlers[] = {100000, 200000, 300000, 400000, 500000};

  for (auto hodlers : numHodlers) {
    contrAddr = Account::GetAddressForContract(ownerAddr, nonce);
    LOG_GENERAL(INFO, "FungibleToken Address: " << contrAddr.hex());

    // Deploy the contract using data from the 2nd Scilla test.
    ScillaTestUtil::ScillaTest t2;
    if (!ScillaTestUtil::GetScillaTest(t2, "fungible-token", 2)) {
      LOG_GENERAL(WARNING, "Unable to fetch test fungible-token_2.");
      return;
    }

    // Replace owner address in init.json.
    for (auto& it : t2.init) {
      if (it["vname"] == "owner") {
        it["value"] = "0x" + ownerAddr.hex();
      }
    }
    // and remove _creation_block (automatic insertion later).
    ScillaTestUtil::RemoveCreationBlockFromInit(t2.init);

    uint64_t bnum = ScillaTestUtil::GetBlockNumberFromJson(t2.blockchain);

    // Transaction to deploy contract.
    std::string initStr = JSONUtils::convertJsontoStr(t2.init);
    bytes data(initStr.begin(), initStr.end());
    Transaction tx0(1, nonce, NullAddress, owner, 0, PRECISION_MIN_VALUE,
                    500000, t2.code, data);
    TransactionReceipt tr0;
    auto startTimeDeployment = r_timer_start();
    AccountStore::GetInstance().UpdateAccounts(bnum, 1, true, tx0, tr0);
    auto timeElapsedDeployment = r_timer_end(startTimeDeployment);
    Account* account = AccountStore::GetInstance().GetAccount(contrAddr);

    // We should now have a new account.
    BOOST_CHECK_MESSAGE(account != nullptr,
                        "Error with creation of contract account");

    LOG_GENERAL(INFO, "Contract size = "
                          << ScillaTestUtil::GetFileSize("input.scilla"));
    LOG_GENERAL(INFO, "Gas used (deployment) = " << tr0.GetCumGas());
    LOG_GENERAL(INFO, "UpdateAccounts (usec) = " << timeElapsedDeployment);
    nonce++;

    // 2. Pre-generate and save a large map and save it to LDB
    std::string initOwnerBalance;
    for (unsigned int i = 0; i < hodlers; i++) {
      std::vector<unsigned char> hodler(ACC_ADDR_SIZE);
      RAND_bytes(hodler.data(), ACC_ADDR_SIZE);
      std::string hodlerNumTokens = "1";

      Json::Value kvPair;
      kvPair["key"] = "0x" + DataConversion::Uint8VecToHexStr(hodler);
      kvPair["val"] = hodlerNumTokens;

      for (auto& it : t2.state) {
        if (it["vname"] == "balances") {
          // we have to artifically insert the owner here
          if (i == 0) {
            Json::Value ownerBal;
            ownerBal["key"] =
                "0x" + DataConversion::Uint8VecToHexStr(ownerAddr.asBytes());
            ownerBal["val"] = "88888888";
            it["value"][i] = ownerBal;
            continue;
          }

          it["value"][i] = kvPair;
        }
      }
    }

    // save the state
    for (auto& s : t2.state) {
      // skip _balance
      if (s["vname"].asString() == "_balance") {
        continue;
      }

      std::string vname = s["vname"].asString();
      std::string type = s["type"].asString();
      std::string value = s["value"].isString()
                              ? s["value"].asString()
                              : JSONUtils::convertJsontoStr(s["value"]);

      account->SetStorage(vname, type, value);
    }

    // 3. Create a call to Transfer from one account to another
    bytes dataTransfer;
    uint64_t amount =
        ScillaTestUtil::PrepareMessageData(t2.message, dataTransfer);

    Transaction tx1(1, nonce, contrAddr, owner, amount, PRECISION_MIN_VALUE,
                    88888888, {}, dataTransfer);
    TransactionReceipt tr1;
    auto startTimeCall = r_timer_start();
    AccountStore::GetInstance().UpdateAccounts(bnum, 1, true, tx1, tr1);
    auto timeElapsedCall = r_timer_end(startTimeCall);
    LOG_GENERAL(
        INFO, "Size of output = " << ScillaTestUtil::GetFileSize("output.json"))
    LOG_GENERAL(INFO, "Size of map (balances) = " << hodlers);
    LOG_GENERAL(INFO, "Gas used (invocation) = " << tr1.GetCumGas());
    LOG_GENERAL(INFO, "UpdateAccounts (usec) = " << timeElapsedCall);
    nonce++;
  }
}

BOOST_AUTO_TEST_CASE(testNonFungibleToken) {
  // 1. Bootstrap test case
  const unsigned int numOperators = 5;
  const unsigned int numHodlers[] = {50000, 75000, 100000, 125000, 150000};
  std::string numTokensOwned = "1";

  KeyPair owner(priv1, {priv1});
  KeyPair sender;  // also an operator, assigned later.
  Address ownerAddr, senderAddr, contrAddr;

  vector<KeyPair> operators;
  vector<Address> operatorAddrs;

  uint64_t ownerNonce = 0;
  uint64_t senderNonce = 0;

  // generate operator keypairs
  for (unsigned int i = 0; i < numOperators; i++) {
    KeyPair oprtr = Schnorr::GetInstance().GenKeyPair();
    Address operatorAddr = Account::GetAddressFromPublicKey(oprtr.second);
    operators.emplace_back(oprtr);
    operatorAddrs.emplace_back(operatorAddr);

    if (i == 0) {
      sender = oprtr;
    }
  }

  INIT_STDOUT_LOGGER();

  LOG_MARKER();

  if (SCILLA_ROOT.empty()) {
    LOG_GENERAL(WARNING, "SCILLA_ROOT not set to run Test_Contract");
    return;
  }

  AccountStore::GetInstance().Init();

  const uint128_t bal{std::numeric_limits<uint128_t>::max()};

  ownerAddr = Account::GetAddressFromPublicKey(owner.second);
  AccountStore::GetInstance().AddAccount(ownerAddr, {bal, ownerNonce});

  senderAddr = Account::GetAddressFromPublicKey(sender.second);
  AccountStore::GetInstance().AddAccount(senderAddr, {bal, senderNonce});

  for (auto hodlers : numHodlers) {
    contrAddr = Account::GetAddressForContract(ownerAddr, ownerNonce);
    LOG_GENERAL(INFO, "NonFungibleToken Address: " << contrAddr.hex());

    // Deploy the contract using data from the 10th Scilla test.
    ScillaTestUtil::ScillaTest t10;
    if (!ScillaTestUtil::GetScillaTest(t10, "nonfungible-token", 10)) {
      LOG_GENERAL(WARNING, "Unable to fetch test nonfungible-token_10;.");
      return;
    }

    // Replace owner address in init.json.
    for (auto& it : t10.init) {
      if (it["vname"] == "owner") {
        it["value"] = "0x" + ownerAddr.hex();
      }
    }
    // and remove _creation_block (automatic insertion later).
    ScillaTestUtil::RemoveCreationBlockFromInit(t10.init);

    uint64_t bnum = ScillaTestUtil::GetBlockNumberFromJson(t10.blockchain);

    // Transaction to deploy contract.
    std::string initStr = JSONUtils::convertJsontoStr(t10.init);
    bytes data(initStr.begin(), initStr.end());
    Transaction tx0(1, ownerNonce, NullAddress, owner, 0, PRECISION_MIN_VALUE,
                    500000, t10.code, data);
    TransactionReceipt tr0;
    AccountStore::GetInstance().UpdateAccounts(bnum, 1, true, tx0, tr0);
    Account* account = AccountStore::GetInstance().GetAccount(contrAddr);
    // We should now have a new account.
    BOOST_CHECK_MESSAGE(account != nullptr,
                        "Error with creation of contract account");
    ownerNonce++;

    // 2. Insert n owners of 1 token each, with 5 operator approvals.
    //  Map Uint256 ByStr20
    Json::Value tokenOwnerMap(Json::arrayValue);
    // Map ByStr20 Uint256
    Json::Value ownedTokenCount(Json::arrayValue);
    // Map ByStr20 (Map ByStr20 Bool)
    Json::Value operatorApprovals(Json::arrayValue);

    Json::Value adtBoolTrue;
    adtBoolTrue["constructor"] = "True",
    adtBoolTrue["argtypes"] = Json::arrayValue;
    adtBoolTrue["arguments"] = Json::arrayValue;

    Json::Value approvedOperators(Json::arrayValue);
    for (auto& operatorAddr : operatorAddrs) {
      Json::Value operatorApprovalEntry;
      operatorApprovalEntry["key"] = "0x" + operatorAddr.hex();
      operatorApprovalEntry["val"] = adtBoolTrue;
      approvedOperators.append(operatorApprovalEntry);
    }

    for (unsigned int i = 0; i < hodlers; i++) {
      Address hodler;
      RAND_bytes(hodler.data(), ACC_ADDR_SIZE);

      // contract owner gets the first token
      if (i == 0) {
        hodler = Account::GetAddressFromPublicKey(owner.second);
      }

      // set ownership
      Json::Value tokenOwnerEntry;
      tokenOwnerEntry["key"] = to_string(i + 1);
      tokenOwnerEntry["val"] = "0x" + hodler.hex();
      tokenOwnerMap[i] = tokenOwnerEntry;

      // set token count
      Json::Value tokenCountEntry;
      tokenCountEntry["key"] = "0x" + hodler.hex();
      tokenCountEntry["val"] = numTokensOwned;
      ownedTokenCount[i] = tokenCountEntry;

      // set operator approval
      Json::Value ownerApprovalEntry;
      ownerApprovalEntry["key"] = "0x" + hodler.hex();
      ownerApprovalEntry["val"] = approvedOperators;
      operatorApprovals[i] = ownerApprovalEntry;
    }

    for (auto& it : t10.state) {
      std::string vname(it["vname"].asString());

      if (vname == "tokenOwnerMap") {
        it["value"] = tokenOwnerMap;
        continue;
      }

      if (vname == "ownedTokenCount") {
        it["value"] = ownedTokenCount;
      }

      if (vname == "operatorApprovals") {
        it["value"] = operatorApprovals;
        continue;
      }
    }

    for (auto& s : t10.state) {
      // skip _balance
      if (s["vname"].asString() == "_balance") {
        continue;
      }

      std::string vname = s["vname"].asString();
      std::string type = s["type"].asString();
      std::string value = s["value"].isString()
                              ? s["value"].asString()
                              : JSONUtils::convertJsontoStr(s["value"]);

      account->SetStorage(vname, type, value);
    }

    // 3. Execute transferFrom as an operator
    boost::random::mt19937 rng;
    boost::random::uniform_int_distribution<> ownerDist(0, int(hodlers - 1));
    Json::Value randomReceiver = tokenOwnerMap[ownerDist(rng)];

    // modify t3.message
    for (auto& p : t10.message["params"]) {
      if (p["vname"] == "tokenId") {
        p["value"] = "1";
      }

      if (p["vname"] == "from") {
        p["value"] = "0x" + ownerAddr.hex();
      }

      if (p["vname"] == "to") {
        p["value"] = randomReceiver["val"];
      }
    }

    bytes dataTransfer;
    uint64_t amount =
        ScillaTestUtil::PrepareMessageData(t10.message, dataTransfer);

    Transaction tx1(1, senderNonce, contrAddr, sender, amount,
                    PRECISION_MIN_VALUE, 88888888, {}, dataTransfer);
    TransactionReceipt tr1;
    auto t = r_timer_start();

    AccountStore::GetInstance().UpdateAccounts(bnum, 1, true, tx1, tr1);

    LOG_GENERAL(INFO, "UpdateAccounts (usec) = " << r_timer_end(t));
    LOG_GENERAL(INFO, "Number of Operators = " << numOperators);
    LOG_GENERAL(INFO, "Number of Hodlers = " << hodlers);
    LOG_GENERAL(INFO, "Gas used = " << tr1.GetCumGas());
    senderNonce++;
  }
}

BOOST_AUTO_TEST_SUITE_END()
