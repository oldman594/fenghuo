class TeamSummary {
  TeamSummary({
    required this.teamId,
    required this.displayName,
    required this.playerCount,
    required this.maxPlayers,
  });

  final String teamId;
  final String displayName;
  final int playerCount;
  final int maxPlayers;

  factory TeamSummary.fromJson(Map<String, dynamic> json) {
    return TeamSummary(
      teamId: json['team_id'] as String? ?? '',
      displayName: json['display_name'] as String? ?? '',
      playerCount: json['player_count'] as int? ?? 0,
      maxPlayers: json['max_players'] as int? ?? 0,
    );
  }
}

class RoomSummary {
  RoomSummary({
    required this.roomId,
    required this.name,
    required this.mode,
    required this.phase,
    required this.playerCount,
    required this.maxPlayers,
    required this.battleId,
    required this.teamSummaries,
  });

  final String roomId;
  final String name;
  final String mode;
  final String phase;
  final int playerCount;
  final int maxPlayers;
  final String? battleId;
  final List<TeamSummary> teamSummaries;

  factory RoomSummary.fromJson(Map<String, dynamic> json) {
    final teamJson = (json['team_summaries'] as List<dynamic>? ?? [])
        .map((item) => TeamSummary.fromJson((item as Map).cast<String, dynamic>()))
        .toList();
    return RoomSummary(
      roomId: json['room_id'] as String? ?? '',
      name: json['name'] as String? ?? '',
      mode: json['mode'] as String? ?? '',
      phase: json['phase'] as String? ?? '',
      playerCount: json['player_count'] as int? ?? 0,
      maxPlayers: json['max_players'] as int? ?? 0,
      battleId: json['battle_id'] as String?,
      teamSummaries: teamJson,
    );
  }
}

class RoomPlayer {
  RoomPlayer({
    required this.playerId,
    required this.displayName,
    required this.teamId,
    required this.ready,
    required this.moduleId,
  });

  final String playerId;
  final String displayName;
  final String teamId;
  final bool ready;
  final String? moduleId;

  factory RoomPlayer.fromJson(Map<String, dynamic> json) {
    return RoomPlayer(
      playerId: json['player_id'] as String? ?? '',
      displayName: json['display_name'] as String? ?? '',
      teamId: json['team_id'] as String? ?? '',
      ready: json['ready'] as bool? ?? false,
      moduleId: json['module_id'] as String?,
    );
  }
}

class RoomDevice {
  RoomDevice({
    required this.deviceId,
    required this.displayName,
    required this.deviceKind,
    required this.boundPlayerId,
    required this.online,
  });

  final String deviceId;
  final String? displayName;
  final String? deviceKind;
  final String? boundPlayerId;
  final bool? online;

  factory RoomDevice.fromJson(Map<String, dynamic> json) {
    return RoomDevice(
      deviceId: json['device_id'] as String? ?? '',
      displayName: json['display_name'] as String?,
      deviceKind: json['device_kind'] as String?,
      boundPlayerId: json['bound_player_id'] as String?,
      online: json['online'] as bool?,
    );
  }
}

class RoomCore {
  RoomCore({
    required this.roomId,
    required this.name,
    required this.mode,
    required this.phase,
    required this.maxPlayers,
    required this.battleId,
  });

  final String roomId;
  final String name;
  final String mode;
  final String phase;
  final int maxPlayers;
  final String? battleId;

  factory RoomCore.fromJson(Map<String, dynamic> json) {
    return RoomCore(
      roomId: json['room_id'] as String? ?? '',
      name: json['name'] as String? ?? '',
      mode: json['mode'] as String? ?? '',
      phase: json['phase'] as String? ?? '',
      maxPlayers: json['max_players'] as int? ?? 0,
      battleId: json['battle_id'] as String?,
    );
  }
}

class RoomDetail {
  RoomDetail({
    required this.room,
    required this.players,
    required this.devices,
    required this.positions,
  });

  final RoomCore room;
  final Map<String, RoomPlayer> players;
  final Map<String, RoomDevice> devices;
  final Map<String, dynamic> positions;

  factory RoomDetail.fromJson(Map<String, dynamic> json) {
    final playersJson = (json['players'] as Map<String, dynamic>? ?? {});
    final devicesJson = (json['devices'] as Map<String, dynamic>? ?? {});
    return RoomDetail(
      room: RoomCore.fromJson((json['room'] as Map<String, dynamic>? ?? {})),
      players: playersJson.map((key, value) => MapEntry(
            key,
            RoomPlayer.fromJson((value as Map).cast<String, dynamic>()),
          )),
      devices: devicesJson.map((key, value) => MapEntry(
            key,
            RoomDevice.fromJson((value as Map).cast<String, dynamic>()),
          )),
      positions: (json['positions'] as Map<String, dynamic>? ?? {}),
    );
  }
}
