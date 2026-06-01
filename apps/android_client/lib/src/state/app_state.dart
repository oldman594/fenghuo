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
  List<RoomSummary> rooms = [];
  RoomDetail? selectedRoom;
  RoomMapSnapshot? selectedMap;
  BattleSnapshot? selectedBattle;
  final Map<String, PlayerStatus> playerStatuses = {};
  final List<LiveMessage> liveMessages = [];
  bool loading = false;
  String? error;

  WebSocketChannel? _channel;
  StreamSubscription? _subscription;

  ApiService get _api => ApiService(baseUrl: baseUrl);
  LiveService get _live => LiveService(baseUrl: baseUrl);

  Future<void> refreshRooms() async {
    loading = true;
    error = null;
    notifyListeners();
    try {
      rooms = await _api.fetchRooms();
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
      playerStatuses.clear();
      for (final player in selectedRoom!.players.values) {
        playerStatuses[player.playerId] = await _api.fetchPlayerStatus(player.playerId);
      }
      final battleId = selectedRoom!.room.battleId;
      if (battleId != null && battleId.isNotEmpty) {
        selectedBattle = await _api.fetchBattle(battleId);
      } else {
        selectedBattle = null;
      }
    } catch (e) {
      error = e.toString();
    } finally {
      loading = false;
      notifyListeners();
    }
  }

  Future<void> createRoom({
    required String roomId,
    required String name,
    required String mode,
    required int maxPlayers,
  }) async {
    loading = true;
    error = null;
    notifyListeners();
    try {
      selectedRoom = await _api.createRoom(
        roomId: roomId,
        name: name,
        mode: mode,
        maxPlayers: maxPlayers,
      );
      await refreshRooms();
      await selectRoom(selectedRoom!.room.roomId);
    } catch (e) {
      error = e.toString();
    } finally {
      loading = false;
      notifyListeners();
    }
  }

  Future<void> refreshSelected() async {
    final roomId = selectedRoom?.room.roomId;
    if (roomId != null) {
      await selectRoom(roomId);
    } else {
      await refreshRooms();
    }
  }

  void connectLive() {
    _subscription?.cancel();
    _channel?.sink.close();
    _channel = _live.connect();
    _subscription = _channel!.stream.listen((message) {
      final live = _live.decode(message);
      liveMessages.insert(0, live);
      if (liveMessages.length > 100) {
        liveMessages.removeRange(100, liveMessages.length);
      }
      notifyListeners();
    }, onError: (Object err) {
      error = 'live error: $err';
      notifyListeners();
    });
  }

  @override
  void dispose() {
    _subscription?.cancel();
    _channel?.sink.close();
    super.dispose();
  }
}
