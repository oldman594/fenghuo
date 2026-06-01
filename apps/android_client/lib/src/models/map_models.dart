class PlayerPosition {
  PlayerPosition({
    required this.playerId,
    required this.sourceDeviceId,
    required this.x,
    required this.y,
    required this.headingDeg,
    required this.velocityMps,
    required this.updatedAtMs,
  });

  final String playerId;
  final String? sourceDeviceId;
  final double x;
  final double y;
  final double headingDeg;
  final double velocityMps;
  final int? updatedAtMs;

  factory PlayerPosition.fromJson(Map<String, dynamic> json) {
    double asDouble(Object? value) => (value as num?)?.toDouble() ?? 0;
    return PlayerPosition(
      playerId: json['player_id'] as String? ?? '',
      sourceDeviceId: json['source_device_id'] as String?,
      x: asDouble(json['x']),
      y: asDouble(json['y']),
      headingDeg: asDouble(json['heading_deg']),
      velocityMps: asDouble(json['velocity_mps']),
      updatedAtMs: json['updated_at_ms'] as int?,
    );
  }
}

class RoomMapSnapshot {
  RoomMapSnapshot({
    required this.roomId,
    required this.phase,
    required this.positions,
  });

  final String roomId;
  final String phase;
  final Map<String, PlayerPosition> positions;

  factory RoomMapSnapshot.fromJson(Map<String, dynamic> json) {
    final rawPositions = (json['positions'] as Map<String, dynamic>? ?? {});
    return RoomMapSnapshot(
      roomId: json['room_id'] as String? ?? '',
      phase: json['phase'] as String? ?? '',
      positions: rawPositions.map((key, value) => MapEntry(
            key,
            PlayerPosition.fromJson((value as Map).cast<String, dynamic>()),
          )),
    );
  }
}
