import 'dart:convert';

import 'package:http/http.dart' as http;

import '../models/battle_models.dart';
import '../models/map_models.dart';
import '../models/player_models.dart';
import '../models/room_models.dart';

class ApiService {
  ApiService({required this.baseUrl});

  final String baseUrl;

  Uri _uri(String path) => Uri.parse(baseUrl).replace(path: path);

  Future<Map<String, dynamic>> _getJson(String path) async {
    final response = await http.get(_uri(path));
    if (response.statusCode < 200 || response.statusCode >= 300) {
      throw Exception('GET $path failed: ${response.statusCode} ${response.body}');
    }
    return (jsonDecode(response.body) as Map).cast<String, dynamic>();
  }

  Future<Map<String, dynamic>> _postJson(String path, Map<String, dynamic> body) async {
    final response = await http.post(
      _uri(path),
      headers: {'content-type': 'application/json'},
      body: jsonEncode(body),
    );
    if (response.statusCode < 200 || response.statusCode >= 300) {
      throw Exception('POST $path failed: ${response.statusCode} ${response.body}');
    }
    return (jsonDecode(response.body) as Map).cast<String, dynamic>();
  }

  Future<List<RoomSummary>> fetchRooms() async {
    final json = await _getJson('/api/v1/rooms');
    return (json['rooms'] as List<dynamic>? ?? [])
        .map((item) => RoomSummary.fromJson((item as Map).cast<String, dynamic>()))
        .toList();
  }

  Future<RoomDetail> fetchRoomDetail(String roomId) async {
    final json = await _getJson('/api/v1/rooms/$roomId');
    return RoomDetail.fromJson(json);
  }

  Future<RoomMapSnapshot> fetchRoomMap(String roomId) async {
    final json = await _getJson('/api/v1/rooms/$roomId/map');
    return RoomMapSnapshot.fromJson(json);
  }

  Future<PlayerStatus> fetchPlayerStatus(String playerId) async {
    final json = await _getJson('/api/v1/players/$playerId/status');
    return PlayerStatus.fromJson(json);
  }

  Future<BattleSnapshot> fetchBattle(String battleId) async {
    final json = await _getJson('/api/v1/battles/$battleId');
    return BattleSnapshot.fromJson(json);
  }

  Future<RoomDetail> joinRoom({
    required String roomId,
    required String playerId,
    required String displayName,
    required String teamId,
  }) async {
    final json = await _postJson('/api/v1/rooms/$roomId/join', {
      'event_id': 'app-room-join-${DateTime.now().millisecondsSinceEpoch}',
      'source_id': 'android-client',
      'sequence': DateTime.now().millisecondsSinceEpoch,
      'occurred_at_ms': DateTime.now().millisecondsSinceEpoch,
      'player_id': playerId,
      'display_name': displayName,
      'team_id': teamId,
      'module_id': 'module-$playerId',
    });
    return RoomDetail.fromJson(json);
  }

  Future<RoomDetail> leaveRoom({
    required String roomId,
    required String playerId,
  }) async {
    final json = await _postJson('/api/v1/rooms/$roomId/leave', {
      'event_id': 'app-room-leave-${DateTime.now().millisecondsSinceEpoch}',
      'source_id': 'android-client',
      'sequence': DateTime.now().millisecondsSinceEpoch,
      'occurred_at_ms': DateTime.now().millisecondsSinceEpoch,
      'player_id': playerId,
    });
    return RoomDetail.fromJson(json);
  }

  Future<RoomDetail> setPlayerReady({
    required String roomId,
    required String playerId,
    required bool ready,
  }) async {
    final json = await _postJson('/api/v1/rooms/$roomId/players/$playerId/ready', {
      'event_id': 'app-room-ready-${DateTime.now().millisecondsSinceEpoch}',
      'source_id': 'android-client',
      'sequence': DateTime.now().millisecondsSinceEpoch,
      'occurred_at_ms': DateTime.now().millisecondsSinceEpoch,
      'ready': ready,
    });
    return RoomDetail.fromJson(json);
  }

  Future<RoomDetail> createRoom({
    required String roomId,
    required String name,
    required String mode,
    required int maxPlayers,
  }) async {
    final half = (maxPlayers / 2).floor();
    final json = await _postJson('/api/v1/rooms', {
      'event_id': 'app-room-create-${DateTime.now().millisecondsSinceEpoch}',
      'source_id': 'android-client',
      'sequence': DateTime.now().millisecondsSinceEpoch,
      'occurred_at_ms': DateTime.now().millisecondsSinceEpoch,
      'room_id': roomId,
      'name': name,
      'mode': mode,
      'max_players': maxPlayers,
      'teams': [
        {
          'team_id': 'red',
          'display_name': 'Red',
          'max_players': half,
        },
        {
          'team_id': 'blue',
          'display_name': 'Blue',
          'max_players': maxPlayers - half,
        },
      ],
    });
    return RoomDetail.fromJson(json);
  }
}
