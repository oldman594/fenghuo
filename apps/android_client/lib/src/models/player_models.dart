class PlayerStatus {
  PlayerStatus({
    required this.playerId,
    required this.displayName,
    required this.roomId,
    required this.roomPhase,
    required this.battleId,
    required this.teamId,
    required this.ready,
    required this.deviceId,
    required this.deviceOnline,
    required this.position,
    required this.alive,
    required this.health,
  });

  final String playerId;
  final String displayName;
  final String roomId;
  final String roomPhase;
  final String? battleId;
  final String teamId;
  final bool ready;
  final String? deviceId;
  final bool? deviceOnline;
  final Map<String, dynamic>? position;
  final bool? alive;
  final int? health;

  factory PlayerStatus.fromJson(Map<String, dynamic> json) {
    return PlayerStatus(
      playerId: json['player_id'] as String? ?? '',
      displayName: json['display_name'] as String? ?? '',
      roomId: json['room_id'] as String? ?? '',
      roomPhase: json['room_phase'] as String? ?? '',
      battleId: json['battle_id'] as String?,
      teamId: json['team_id'] as String? ?? '',
      ready: json['ready'] as bool? ?? false,
      deviceId: json['device_id'] as String?,
      deviceOnline: json['device_online'] as bool?,
      position: (json['position'] as Map?)?.cast<String, dynamic>(),
      alive: json['alive'] as bool?,
      health: json['health'] as int?,
    );
  }
}
