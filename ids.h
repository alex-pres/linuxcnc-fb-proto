// File identifiers, one per direction. See ids.ts for the rule.
//   client -> server:  fbb.Finish(offset, Lcfb::CLIENT_ID) and
//                      flatbuffers::GetRoot<Lcfb::ClientMessage>(buf)
//   server -> client:  fbb.Finish(offset, Lcfb::SERVER_ID) and
//                      flatbuffers::GetRoot<Lcfb::ServerMessage>(buf)
// Check on receive with flatbuffers::BufferHasIdentifier(buf, id).
#pragma once
namespace Lcfb {
inline constexpr const char *CLIENT_ID = "LCNC";
inline constexpr const char *SERVER_ID = "LCNS";
}  // namespace Lcfb
