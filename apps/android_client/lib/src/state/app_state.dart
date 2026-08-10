import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

import '../models/battle_models.dart';
import '../models/live_models.dart';
import '../models/map_models.dart';
import '../models/player_models.dart';
import '../models/room_models.dart';
import '../services/api_service.dart';
import '../services/live_service.dart';

class AppState extends ChangeNotifier {
  String baseUrl = 'http://192.168.1.100:8080';
  String localPlayerId = 'player-android-01';
  String localDisplayName = 'Player 01';

  List<RoomSummary> rooms = [];
  RoomDetail? selectedRoom;
  RoomMapSnapshot? selectedMap;
  BattleSnapshot? selectedBattle;
  final Map<String, PlayerStatus> playerStatuses = {};
  final List<LiveMessage> liveMessages = [];

  bool loading = false;
  bool liveConnected = false;
  String? lastLiveType;
  String? error;

  WebSocketChannel? _channel;
  StreamSubscription? _subscription;
  Timer? _battleRefreshTimer;

  ApiService get _api => ApiService(baseUrl: baseUrl);
  LiveService get _live => LiveService(baseUrl: baseUrl);

  RoomPlayer? get currentPlayerInSelectedRoom => selectedRoom?.players[localPlayerId];

  PlayerStatus? get currentPlayerStatus => playerStatuses[localPlayerId];

  BattlePlayer? get currentBattlePlayer => selectedBattle?.players[localPlayerId];

  bool get hasJoinedSelectedRoom => currentPlayerInSelectedRoom != null;

  Future<void> refreshRooms() async {
    loading = true;
    error = null;
    notifyListeners();
    try {
      rooms = await _api.fetchRooms();
      _mergeSelectedRoomSummary();
    } catch (e) {
      error = e.toString();
    } finally {
      loading = false;
      notifyListeners();
    }
  }

  Future<void> selectRoom(String roomId) async {
    loading = true;
    error = null;
    notifyListeners();
    try {
      selectedRoom = await _api.fetchRoomDetail(roomId);
      selectedMap = await _api.fetchRoomMap(roomId);
      await _refreshPlayerStatuses();
      await refreshBattle(notify: false);
      _mergeSelectedRoomSummary();
    } catch (e) {
      error = e.toString();
    } finally {
      loading = false;
      notifyListeners();
    }
  }

  Future<void> refreshBattle({bool notify = true}) async {
    final battleId = selectedRoom?.room.battleId;
    if (battleId == null || battleId.isEmpty) {
      selectedBattle = null;
      if (notify) {
        notifyListeners();
      }
      return;
    }

    try {
      selectedBattle = await _api.fetchBattle(battleId);
    } catch (e) {
      error = e.toString();
    } finally {
      if (notify) {
        notifyListeners();
      }
    }
  }

  Future<void> joinSelectedRoom(String teamId) async {
    final roomId = selectedRoom?.room.roomId;
    if (roomId == null || roomId.isEmpty) {
      return;
    }

    loading = true;
    error = null;
    notifyListeners();
    try {
      selectedRoom = await _api.joinRoom(
        roomId: roomId,
        playerId: localPlayerId,
        displayName: localDisplayName,
        teamId: teamId,
      );
      await selectRoom(roomId);
    } catch (e) {
      error = e.toString();
      loading = false;
      notifyListeners();
    }
  }

  Future<void> leaveSelectedRoom() async {
    final roomId = selectedRoom?.room.roomId;
    if (roomId == null || roomId.isEmpty) {
      return;
    }

    loading = true;
    error = null;
    notifyListeners();
    try {
      selectedRoom = await _api.leaveRoom(roomId: roomId, playerId: localPlayerId);
      await selectRoom(roomId);
    } catch (e) {
      error = e.toString();
      loading = false;
      notifyListeners();
    }
  }

  Future<void> setCurrentPlayerReady(bool ready) async {
    final roomId = selectedRoom?.room.roomId;
    final playerId = currentPlayerInSelectedRoom?.playerId;
    if (roomId == null || playerId == null) {
      return;
    }

    loading = true;
    error = null;
    notifyListeners();
    try {
      selectedRoom = await _api.setPlayerReady(
        roomId: roomId,
        playerId: playerId,
        ready: ready,
      );
      await selectRoom(roomId);
    } catch (e) {
      error = e.toString();
      loading = false;
      notifyListeners();
    }
  }

  Future<void> refreshSelected() async {
    final roomId = selectedRoom?.room.roomId;
    if (roomId != null && roomId.isNotEmpty) {
      await selectRoom(roomId);
    } else {
      await refreshRooms();
    }
  }

  void updateProfile({
    required String playerId,
    required String displayName,
  }) {
    localPlayerId = playerId;
    localDisplayName = displayName;
    notifyListeners();
  }

  Future<void> updateBaseUrl(String value) async {
    baseUrl = value;
    connectLive();
    await refreshSelected();
  }

  void connectLive() {
    _subscription?.cancel();
    _channel?.sink.close();
    liveConnected = false;
    notifyListeners();

    _channel = _live.connect();
    _subscription = _channel!.stream.listen(
      (message) => _handleLiveMessage(_live.decode(message)),
      onError: (Object err) {
        liveConnected = false;
        error = 'live error: $err';
        notifyListeners();
      },
      onDone: () {
        liveConnected = false;
        notifyListeners();
      },
    );
  }

  void _handleLiveMessage(LiveMessage live) {
    liveConnected = true;
    lastLiveType = live.type;
    liveMessages.insert(0, live);
    if (liveMessages.length > 50) {
      liveMessages.removeRange(50, liveMessages.length);
    }

    switch (live.type) {
      case 'room_summary_updated':
        final rawRoom = live.raw['room'];
        if (rawRoom is Map<String, dynamic>) {
          _mergeRoomSummary(RoomSummary.fromJson(rawRoom));
        } else if (rawRoom is Map) {
          _mergeRoomSummary(RoomSummary.fromJson(rawRoom.cast<String, dynamic>()));
        }
        break;
      case 'room_detail_updated':
        final detail = RoomDetail.fromJson(live.raw);
        if (selectedRoom?.room.roomId == detail.room.roomId) {
          selectedRoom = detail;
          selectedMap = _mapFromRoomDetail(detail);
          _scheduleBattleRefresh();
        }
        break;
      case 'map_updated':
        final map = RoomMapSnapshot.fromJson(live.raw);
        if (selectedRoom?.room.roomId == map.roomId) {
          selectedMap = map;
        }
        break;
      case 'player_status_updated':
        final rawStatus = live.raw['status'];
        if (rawStatus is Map<String, dynamic>) {
          final status = PlayerStatus.fromJson(rawStatus);
          playerStatuses[status.playerId] = status;
          if (selectedRoom?.room.roomId == status.roomId) {
            _scheduleBattleRefresh();
          }
        } else if (rawStatus is Map) {
          final status = PlayerStatus.fromJson(rawStatus.cast<String, dynamic>());
          playerStatuses[status.playerId] = status;
          if (selectedRoom?.room.roomId == status.roomId) {
            _scheduleBattleRefresh();
          }
        }
        break;
    }

    notifyListeners();
  }

  void _scheduleBattleRefresh() {
    _battleRefreshTimer?.cancel();
    _battleRefreshTimer = Timer(const Duration(milliseconds: 250), () {
      unawaited(refreshBattle());
    });
  }

  Future<void> _refreshPlayerStatuses() async {
    playerStatuses.clear();
    final detail = selectedRoom;
    if (detail == null) {
      return;
    }

    for (final player in detail.players.values) {
      playerStatuses[player.playerId] = await _api.fetchPlayerStatus(player.playerId);
    }
  }

  void _mergeSelectedRoomSummary() {
    final roomId = selectedRoom?.room.roomId;
    if (roomId == null) {
      return;
    }
    RoomSummary? summary;
    for (final room in rooms) {
      if (room.roomId == roomId) {
        summary = room;
        break;
      }
    }
    if (summary == null) {
      return;
    }
    selectedRoom = RoomDetail(
      room: RoomCore(
        roomId: selectedRoom!.room.roomId,
        name: summary.name,
        mode: summary.mode,
        phase: summary.phase,
        maxPlayers: summary.maxPlayers,
        battleId: summary.battleId,
      ),
      players: selectedRoom!.players,
      devices: selectedRoom!.devices,
      positions: selectedRoom!.positions,
    );
  }

  void _mergeRoomSummary(RoomSummary summary) {
    final index = rooms.indexWhere((room) => room.roomId == summary.roomId);
    if (index >= 0) {
      rooms[index] = summary;
    } else {
      rooms = [...rooms, summary];
    }
    if (selectedRoom?.room.roomId == summary.roomId) {
      selectedRoom = RoomDetail(
        room: RoomCore(
          roomId: selectedRoom!.room.roomId,
          name: summary.name,
          mode: summary.mode,
          phase: summary.phase,
          maxPlayers: summary.maxPlayers,
          battleId: summary.battleId,
        ),
        players: selectedRoom!.players,
        devices: selectedRoom!.devices,
        positions: selectedRoom!.positions,
      );
    }
  }

  RoomMapSnapshot _mapFromRoomDetail(RoomDetail detail) {
    final rawPositions = detail.positions.map((key, value) {
      if (value is Map<String, dynamic>) {
        return MapEntry(key, value);
      }
      if (value is Map) {
        return MapEntry(key, value.cast<String, dynamic>());
      }
      return MapEntry(key, <String, dynamic>{});
    });

    return RoomMapSnapshot.fromJson({
      'room_id': detail.room.roomId,
      'phase': detail.room.phase,
      'positions': rawPositions,
    });
  }

  @override
  void dispose() {
    _battleRefreshTimer?.cancel();
    _subscription?.cancel();
    _channel?.sink.close();
    super.dispose();
  }
}
