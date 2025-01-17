#pragma once

X_NAMESPACE_BEGIN(net)

//
//	Connection process:
//
//		send -> OpenConnectionRequest
//				protoColVersion
//
//		receive <- OpenConnectionResponse
//				remoteGuid
//				mtu
//
//		send -> OpenConnectionRequestStage2
//				clientGuid
//				bidingAdd
//				mtu
//
//				we try to add the client as a remote system here.
//				remoteMode -> UnverifiedSender
//
//		receive <- OpenConnectionResponseStage2
//				remoteGuid
//				bindingAdd
//				mtu
//
//				we try to add the client as a remote (marking it as a connection we opened)
//				remoteMode -> UnverifiedSender
//
//		send -> ConnectionRequest
//				guid
//				timeStamp
//				password?
//				remoteMode -> RequestedConnection
//
//		receive <- ConnectionRequestAccepted
//				systemAddres server see's for us
//				systemIndex
//				listofLocalBindings
//				ConnectionRequest::timeStamp
//				serverTimeStamp
//				remoteMode -> HandlingConnectionRequest
//
//		send -> ConnectionRequestHandshake
//				systemAddres of server from our point of view
//				listOfLocalBindings
//				ConnectionRequestAccepted::serverTimeStamp
//				timeStamp
//				remoteMode -> Connected
//
//		Server receives -> ConnectionRequestHandshake
//				updates remote info
//				remoteMode -> Connected
//
//		connection is complete.
//		Ping pong commences
//
//
//	Messages accepted for various remote modes:
//
//		UnverifiedSender:
//			ConnectionRequest
//
//		RequestedConnection:
//			ConnectionRequestAccepted
//
//		HandlingConnectionRequest:
//			ConnectionRequestHandshake
//
//		Connected:
//
//
//
//
//
//

X_DECLARE_ENUM8(MessageID)
(

    // the remote systems protocol version is incompatible
    // action: read remote clients protocol version display in error msg.
    ProtcolVersionIncompatible,

    // ----- Connection related -----

    // a Ping from a connected system
    // action: update timestamps
    ConnectedPing,
    // a Pong from a connected system
    // action: update timestamps
    ConnectedPong,
    // a Ping from a un-connected system
    // action: reply, skip timestamp update.
    UnConnectedPing,
    // a Ping from a un-connected system
    // action: reply if we have open connections, skip timestamp update.
    UnConnectedPingOpenConnections,
    // a Pong from a un-connected system
    // action: reply, skip timestamp update.
    UnConnectedPong,
    // request to OPEN connection.
    // action: respond with OpenConnectionResponse
    OpenConnectionRequest,
    // request to OPEN connection stage2.
    // action:  respond with OpenConnectionResponseStage2
    OpenConnectionRequestStage2,
    // a connection OPEN response.
    // action: send a OpenConnectionRequestStage2
    OpenConnectionResponse,
    // a connection OPEN response stage 2.
    // action: send ConnectionRequest
    OpenConnectionResponseStage2,

    // send after we have performed openConnection stages 1 && 2
    // we can send this to finally open the connection.
    // action: respond with ConnectionRequestAccepted || ConnectionRequestFailed
    ConnectionRequest,
    // connection to server accepted.
    // action: respond with ConnectionRequestHandShake
    ConnectionRequestAccepted,
    // client responded to connection accepted
    // action: mark client as connected
    ConnectionRequestHandShake,
    // connection to server failed. (reasons this can happen: ..?)
    // action:
    ConnectionRequestFailed,

    // you are already connected to this remote system
    // action:
    AlreadyConnected,

    // we requested the connection to be closed.
    // action:
    ConnectionClosed,
    // banned from connecting to target remove system
    // action: cry.
    ConnectionBanned,
    // the remote system has not accepting any more connections at this time.
    // action: give up or try again later
    ConnectionNoFreeSlots,
    // the remote system has rejected the connection for a given amount of time.
    // action: read the ratelimit time, and try again after elapsed.
    ConnectionRateLimited,

    // the remote system has disconnected from us. if we client server has closed. if server, client has left.
    // action: panic!
    DisconnectNotification,

    InvalidPassword,

    // ----------------

    SndReceiptAcked,
    SndReceiptLost,

    // ----- Misc -----

    // a remote system reports stu as still missing
    // action: keep searching.
    StuNotFnd,

    // a timestamp from remote system.
    // action:
    TimeStamp,
    // send test on socket.
    // action: ignore
    SendTest,

    // ----- Nat -----

    NatPunchthroughRequest,
    NatPunchthroughFailed,
    NatPunchthroughSucceeded,
    NatTypeDetectionRequest,
    NatTypeDetectionResult,

    // --- Lobby ---

    LobbyJoinRequest,       
    LobbyJoinAccepted,      // your request to join the lobby was accepted.
    LobbyJoinNoFreeSlots,
    LobbyJoinRejected,

    LobbyUsersConnected,
    LobbyUsersDiconnected,

    LobbyGameParams,        // Game meta.

    LobbyConnectAndMove,    // The host of a party told us to join this party.
    LobbyLeaveGameLobby,    // The host suggests you leave with him :)

    LobbyPingValues,        // from host has all the users pings.
    LobbyChatMsg,

    // --- Session ---

    LoadingStart,   // tell peer to start loading
    LoadingDone,    // peer finished loading assets
    InGame,         // peer is in game (aka synced world state)
    EndGame,

    SnapShot,
    SnapShotAck,

    UserCmd,

    // --- Game ---

    GameChatMsg,
    GameEvent

);

X_NAMESPACE_END
