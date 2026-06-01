class BattlePlayer {
  BattlePlayer({
    required this.playerId,
    required this.displayName,
    required this.teamId,
    required this.health,
    required this.alive,
    required this.shotCount,
    required this.hitCount,
  });

  final String playerId;
  final String displayName;
  final String teamId;
  final int health;
  final bool alive;
  final int shotCount;
  final int hitCount;

  factory BattlePlayer.fromJson(Map<String, dynamic> json) {
    return BattlePlayer(
      playerId: json['player_id'] as String? ?? '',
      displayName: json['display_name'] as String? ?? '',
      teamId: json['team_id'] as String? ?? '',
      health: json['health'] as int? ?? 0,
      alive: json['alive'] as bool? ?? false,
      shotCount: json['shot_count'] as int? ?? 0,
      hitCount: json['hit_count'] as int? ?? 0,
    );
  }
}

class TeamBattleState {
  TeamBattleState({
    required this.teamId,
    required this.score,
  });

  final String teamId;
  final int score;

  factory TeamBattleState.fromJson(String teamId, Map<String, dynamic> json) {
    return TeamBattleState(
      teamId: teamId,
      score: json['score'] as int? ?? 0,
    );
  }
}

class BattleSnapshot {
  BattleSnapshot({
    required this.battleId,
    required this.phase,
    required this.mode,
    required this.players,
    required this.teams,
  });

  final String battleId;
  final String phase;
  final String? mode;
  final Map<String, BattlePlayer> players;
  final Map<String, TeamBattleState> teams;

  factory BattleSnapshot.fromJson(Map<String, dynamic> json) {
    final playersJson = (json['players'] as Map<String, dynamic>? ?? {});
    final teamsJson = (json['teams'] as Map<String, dynamic>? ?? {});
    return BattleSnapshot(
      battleId: json['battle_id'] as String? ?? '',
      phase: json['phase'] as String? ?? '',
      mode: json['mode'] as String?,
      players: playersJson.map((key, value) => MapEntry(
            key,
            BattlePlayer.fromJson((value as Map).cast<String, dynamic>()),
          )),
      teams: teamsJson.map((key, value) => MapEntry(
            key,
            TeamBattleState.fromJson(key, (value as Map).cast<String, dynamic>()),
          )),
    );
  }
}
