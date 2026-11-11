#include "Session.h"

#include <limits.h>     // for CHAR_BIT
#include <cstdint>      // for uint8_t
#include <functional>   // for __base
#include <memory>       // for shared_ptr, unique_ptr, make_unique
#include <random>       // for default_random_engine, independent_bi...
#include <type_traits>  // for remove_extent_t
#include <utility>      // for move

#include "ApResolve.h"          // for ApResolve, cspot
#include "AuthChallenges.h"     // for AuthChallenges
#include "Diagnostics.h"        // for setLastError
#include "BellLogger.h"         // for AbstractLogger
#include "Logger.h"             // for CSPOT_LOG
#include "LoginBlob.h"          // for LoginBlob
#include "Packet.h"             // for Packet
#include "PlainConnection.h"    // for PlainConnection, timeoutCallback
#include "ShannonConnection.h"  // for ShannonConnection

#include "NanoPBHelper.h"  // for pbPutString, pbEncode, pbDecode
#include "pb_decode.h"
#include "protobuf/authentication.pb.h"

using random_bytes_engine =
    std::independent_bits_engine<std::default_random_engine, CHAR_BIT, uint8_t>;

using namespace cspot;

namespace {
// APLoginFailed is not in our protos: error_code is field 10 (varint),
// error_description field 14 (string). Codes follow Spotify's ErrorCode enum.
const char* loginErrorName(uint32_t code) {
  switch (code) {
    case 0x0: return "ProtocolError";
    case 0x2: return "TryAnotherAP";
    case 0x5: return "BadConnectionId";
    case 0x9: return "TravelRestriction";
    case 0xb: return "PremiumAccountRequired";
    case 0xc: return "BadCredentials";
    case 0xd: return "CouldNotValidateCredentials";
    case 0xe: return "AccountExists";
    case 0xf: return "ExtraVerificationRequired";
    case 0x10: return "InvalidAppKey";
    case 0x11: return "ApplicationBanned";
    default: return "Unknown";
  }
}

bool readVarint(const std::vector<uint8_t>& d, size_t& i, uint64_t& out) {
  out = 0;
  for (int shift = 0; i < d.size() && shift < 64; shift += 7) {
    uint8_t b = d[i++];
    out |= (uint64_t)(b & 0x7f) << shift;
    if (!(b & 0x80)) return true;
  }
  return false;
}

std::string describeLoginFailure(const std::vector<uint8_t>& d) {
  int64_t code = -1;
  std::string description;
  size_t i = 0;
  uint64_t key, value;
  while (readVarint(d, i, key)) {
    uint32_t field = key >> 3, wire = key & 7;
    if (wire == 0) {
      if (!readVarint(d, i, value)) break;
      if (field == 10) code = (int64_t)value;
    } else if (wire == 2) {
      if (!readVarint(d, i, value) || value > d.size() - i) break;
      if (field == 14) description.assign(d.begin() + i, d.begin() + i + value);
      i += value;
    } else {
      break;
    }
  }
  std::string text = "Spotify login declined: ";
  text += code < 0 ? "no error code" : loginErrorName((uint32_t)code);
  if (code >= 0) text += " (" + std::to_string(code) + ")";
  if (!description.empty()) text += " - " + description;
  return text;
}
}  // namespace

Session::Session() {
  this->challenges = std::make_unique<cspot::AuthChallenges>();
}

Session::~Session() {}

void Session::connect(std::unique_ptr<cspot::PlainConnection> connection) {
  this->conn = std::move(connection);
  conn->timeoutHandler = [this]() {
    return this->triggerTimeout();
  };
  auto helloPacket = this->conn->sendPrefixPacket(
      {0x00, 0x04}, this->challenges->prepareClientHello());
  auto apResponse = this->conn->recvPacket();
  CSPOT_LOG(info, "Received APHello response");

  auto solvedHello = this->challenges->solveApHello(helloPacket, apResponse);

  conn->sendPrefixPacket({}, solvedHello);
  CSPOT_LOG(debug, "Received shannon keys");

  // Generates the public and priv key
  this->shanConn = std::make_shared<ShannonConnection>();

  // Init shanno-encrypted connection
  this->shanConn->wrapConnection(this->conn, challenges->shanSendKey,
                                 challenges->shanRecvKey);
}

void Session::connectWithRandomAp() {
  auto apResolver = std::make_unique<ApResolve>("");
  auto conn = std::make_unique<cspot::PlainConnection>();
  conn->timeoutHandler = [this]() {
    return this->triggerTimeout();
  };

  auto apAddr = apResolver->fetchFirstApAddress();

  CSPOT_LOG(debug, "Connecting with AP <%s>", apAddr.c_str());
  conn->connect(apAddr);

  this->connect(std::move(conn));
}

std::vector<uint8_t> Session::authenticate(std::shared_ptr<LoginBlob> blob) {
  // save auth blob for reconnection purposes
  authBlob = blob;
  // prepare authentication request proto
  auto data = challenges->prepareAuthPacket(blob->authData, blob->authType,
                                            deviceId, blob->username);

  // Send login request
  this->shanConn->sendPacket(LOGIN_REQUEST_COMMAND, data);

  auto packet = this->shanConn->recvPacket();
  switch (packet.command) {
    case AUTH_SUCCESSFUL_COMMAND: {
      APWelcome welcome;
      CSPOT_LOG(debug, "Authorization successful");
      pbDecode(welcome, APWelcome_fields, packet.data);
      return std::vector<uint8_t>(welcome.reusable_auth_credentials.bytes,
                                  welcome.reusable_auth_credentials.bytes +
                                      welcome.reusable_auth_credentials.size);
      break;
    }
    case AUTH_DECLINED_COMMAND: {
      auto reason = describeLoginFailure(packet.data);
      CSPOT_LOG(error, "%s", reason.c_str());
      setLastError(reason);
      break;
    }
    default:
      CSPOT_LOG(error, "Unknown auth fail code %d", packet.command);
  }

  return std::vector<uint8_t>(0);
}

void Session::close() {
  this->conn->close();
}
