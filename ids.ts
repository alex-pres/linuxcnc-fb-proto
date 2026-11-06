// File identifiers, one per direction. Hand-written: the schema declares no
// root so that one generated tree serves both sides.
//   client -> server:  builder.finish(offset, CLIENT_ID); ClientMessage.getRootAsClientMessage(bb)
//   server -> client:  builder.finish(offset, SERVER_ID); ServerMessage.getRootAsServerMessage(bb)
// Check on receive with bb.__has_identifier(<the other side's id>).
export const CLIENT_ID = 'LCNC';
export const SERVER_ID = 'LCNS';
