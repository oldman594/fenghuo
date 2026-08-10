import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../models/map_models.dart';
import '../state/app_state.dart';
import '../widgets/section_card.dart';

class BattlePage extends StatelessWidget {
  const BattlePage({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    final battle = state.selectedBattle;
    final map = state.selectedMap;

    if (battle == null || map == null) {
      return const _EmptyState(
        icon: Icons.map_outlined,
        title: 'No active battle',
        message: 'Join a room and wait for the room to enter battle.',
      );
    }

    final redScore = battle.teams['red']?.score ?? 0;
    final blueScore = battle.teams['blue']?.score ?? 0;
    final player = state.currentBattlePlayer;
    final hp = player?.health ?? state.currentPlayerStatus?.health ?? 0;
    final alive = player?.alive ?? state.currentPlayerStatus?.alive ?? false;

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        SectionCard(
          title: battle.battleId,
          trailing: Chip(label: Text(battle.phase)),
          child: Column(
            children: [
              Row(
                children: [
                  _ScorePill(label: 'Red', value: redScore, color: Colors.redAccent),
                  const SizedBox(width: 12),
                  _ScorePill(label: 'Blue', value: blueScore, color: Colors.blueAccent),
                ],
              ),
              const SizedBox(height: 12),
              Row(
                children: [
                  Expanded(child: Text('HP: $hp')),
                  Expanded(child: Text('State: ${alive ? 'alive' : 'down'}')),
                  Expanded(child: Text('Mode: ${battle.mode ?? 'unknown'}')),
                ],
              ),
            ],
          ),
        ),
        const SizedBox(height: 12),
        SectionCard(
          title: 'Battle Map',
          child: _MapCanvas(
            positions: map.positions.values.toList(),
            localPlayerId: state.localPlayerId,
            teamLookup: {
              for (final battlePlayer in battle.players.values) battlePlayer.playerId: battlePlayer.teamId,
            },
          ),
        ),
        const SizedBox(height: 12),
        SectionCard(
          title: 'Players',
          child: Column(
            children: [
              for (final battlePlayer in battle.players.values)
                ListTile(
                  contentPadding: EdgeInsets.zero,
                  leading: CircleAvatar(
                    radius: 16,
                    backgroundColor: _teamColor(battlePlayer.teamId).withValues(alpha: 0.14),
                    child: Text(
                      battlePlayer.displayName.isNotEmpty ? battlePlayer.displayName.substring(0, 1) : '?',
                      style: TextStyle(color: _teamColor(battlePlayer.teamId)),
                    ),
                  ),
                  title: Text(battlePlayer.displayName),
                  subtitle: Text('${battlePlayer.teamId} · ${battlePlayer.hitCount} hits'),
                  trailing: Text('${battlePlayer.health} hp'),
                ),
            ],
          ),
        ),
      ],
    );
  }
}

class _MapCanvas extends StatelessWidget {
  const _MapCanvas({
    required this.positions,
    required this.localPlayerId,
    required this.teamLookup,
  });

  final List<PlayerPosition> positions;
  final String localPlayerId;
  final Map<String, String> teamLookup;

  @override
  Widget build(BuildContext context) {
    return AspectRatio(
      aspectRatio: 1.15,
      child: Container(
        decoration: BoxDecoration(
          color: const Color(0xFF12161D),
          borderRadius: BorderRadius.circular(8),
          border: Border.all(color: Theme.of(context).colorScheme.outlineVariant),
        ),
        child: CustomPaint(
          painter: _BattleMapPainter(
            positions: positions,
            localPlayerId: localPlayerId,
            teamLookup: teamLookup,
          ),
          child: const SizedBox.expand(),
        ),
      ),
    );
  }
}

class _BattleMapPainter extends CustomPainter {
  _BattleMapPainter({
    required this.positions,
    required this.localPlayerId,
    required this.teamLookup,
  });

  final List<PlayerPosition> positions;
  final String localPlayerId;
  final Map<String, String> teamLookup;

  @override
  void paint(Canvas canvas, Size size) {
    final background = Paint()..color = const Color(0xFF12161D);
    canvas.drawRect(Offset.zero & size, background);

    final grid = Paint()
      ..color = const Color(0xFF24303B)
      ..strokeWidth = 1;
    for (int index = 1; index < 6; index += 1) {
      final dx = size.width * index / 6;
      final dy = size.height * index / 6;
      canvas.drawLine(Offset(dx, 0), Offset(dx, size.height), grid);
      canvas.drawLine(Offset(0, dy), Offset(size.width, dy), grid);
    }

    if (positions.isEmpty) {
      final painter = TextPainter(
        text: const TextSpan(
          text: 'Waiting for positions',
          style: TextStyle(color: Colors.white70, fontSize: 14),
        ),
        textDirection: TextDirection.ltr,
      )..layout(maxWidth: size.width);
      painter.paint(
        canvas,
        Offset((size.width - painter.width) / 2, (size.height - painter.height) / 2),
      );
      return;
    }

    final minX = positions.map((item) => item.x).reduce(math.min);
    final maxX = positions.map((item) => item.x).reduce(math.max);
    final minY = positions.map((item) => item.y).reduce(math.min);
    final maxY = positions.map((item) => item.y).reduce(math.max);
    final spanX = math.max(1.0, maxX - minX);
    final spanY = math.max(1.0, maxY - minY);

    for (final position in positions) {
      final normalizedX = (position.x - minX) / spanX;
      final normalizedY = (position.y - minY) / spanY;
      final point = Offset(
        20 + normalizedX * (size.width - 40),
        20 + normalizedY * (size.height - 40),
      );
      final isLocal = position.playerId == localPlayerId;
      final teamId = teamLookup[position.playerId] ?? '';
      final color = _teamColor(teamId);
      final marker = Paint()..color = color;
      canvas.drawCircle(point, isLocal ? 10 : 7, marker);

      if (isLocal) {
        final ring = Paint()
          ..color = Colors.white
          ..style = PaintingStyle.stroke
          ..strokeWidth = 2;
        canvas.drawCircle(point, 14, ring);
      }

      final label = TextPainter(
        text: TextSpan(
          text: isLocal ? 'YOU' : position.playerId,
          style: const TextStyle(color: Colors.white, fontSize: 10),
        ),
        textDirection: TextDirection.ltr,
      )..layout(maxWidth: 80);
      label.paint(canvas, point + const Offset(10, -18));
    }
  }

  @override
  bool shouldRepaint(covariant _BattleMapPainter oldDelegate) {
    return oldDelegate.positions != positions ||
        oldDelegate.localPlayerId != localPlayerId ||
        oldDelegate.teamLookup != teamLookup;
  }
}

class _ScorePill extends StatelessWidget {
  const _ScorePill({
    required this.label,
    required this.value,
    required this.color,
  });

  final String label;
  final int value;
  final Color color;

  @override
  Widget build(BuildContext context) {
    return Expanded(
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
        decoration: BoxDecoration(
          color: color.withValues(alpha: 0.12),
          borderRadius: BorderRadius.circular(8),
          border: Border.all(color: color.withValues(alpha: 0.35)),
        ),
        child: Column(
          children: [
            Text(label, style: TextStyle(color: color, fontWeight: FontWeight.w600)),
            const SizedBox(height: 4),
            Text(
              '$value',
              style: Theme.of(context).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w700),
            ),
          ],
        ),
      ),
    );
  }
}

Color _teamColor(String teamId) {
  switch (teamId) {
    case 'red':
      return Colors.redAccent;
    case 'blue':
      return Colors.blueAccent;
    default:
      return Colors.grey;
  }
}

class _EmptyState extends StatelessWidget {
  const _EmptyState({
    required this.icon,
    required this.title,
    required this.message,
  });

  final IconData icon;
  final String title;
  final String message;

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(icon, size: 48, color: Theme.of(context).colorScheme.outline),
            const SizedBox(height: 16),
            Text(title, style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 8),
            Text(message, textAlign: TextAlign.center),
          ],
        ),
      ),
    );
  }
}
