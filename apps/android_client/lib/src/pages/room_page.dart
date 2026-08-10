import 'package:flutter/material.dart';

import '../models/room_models.dart';
import '../state/app_state.dart';
import '../widgets/section_card.dart';

class RoomPage extends StatelessWidget {
  const RoomPage({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    final room = state.selectedRoom;
    if (room == null) {
      return const _EmptyState(
        icon: Icons.groups_outlined,
        title: 'No room selected',
        message: 'Choose a room from Home to view the lobby and join a team.',
      );
    }

    final teamIds = room.players.values.map((player) => player.teamId).toSet().toList()..sort();

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        SectionCard(
          title: room.room.name.isEmpty ? room.room.roomId : room.room.name,
          trailing: Chip(label: Text(room.room.phase)),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(room.room.mode),
              const SizedBox(height: 6),
              Text('Room ID: ${room.room.roomId}'),
              if (room.room.battleId != null) ...[
                const SizedBox(height: 6),
                Text('Battle: ${room.room.battleId}'),
              ],
            ],
          ),
        ),
        const SizedBox(height: 12),
        for (final teamId in teamIds) ...[
          _TeamSection(
            state: state,
            teamId: teamId,
            players: room.players.values.where((player) => player.teamId == teamId).toList(),
          ),
          const SizedBox(height: 12),
        ],
        SectionCard(
          title: 'Actions',
          child: Column(
            children: [
              if (!state.hasJoinedSelectedRoom)
                Row(
                  children: [
                    Expanded(
                      child: FilledButton(
                        onPressed: state.loading ? null : () => state.joinSelectedRoom('red'),
                        child: const Text('Join Red'),
                      ),
                    ),
                    const SizedBox(width: 12),
                    Expanded(
                      child: FilledButton(
                        onPressed: state.loading ? null : () => state.joinSelectedRoom('blue'),
                        child: const Text('Join Blue'),
                      ),
                    ),
                  ],
                ),
              if (state.hasJoinedSelectedRoom) ...[
                SizedBox(
                  width: double.infinity,
                  child: FilledButton(
                    onPressed: state.loading
                        ? null
                        : () => state.setCurrentPlayerReady(!(state.currentPlayerInSelectedRoom?.ready ?? false)),
                    child: Text((state.currentPlayerInSelectedRoom?.ready ?? false) ? 'Set Not Ready' : 'Ready'),
                  ),
                ),
                const SizedBox(height: 12),
                SizedBox(
                  width: double.infinity,
                  child: OutlinedButton(
                    onPressed: state.loading ? null : state.leaveSelectedRoom,
                    child: const Text('Leave Room'),
                  ),
                ),
              ],
            ],
          ),
        ),
      ],
    );
  }
}

class _TeamSection extends StatelessWidget {
  const _TeamSection({
    required this.state,
    required this.teamId,
    required this.players,
  });

  final AppState state;
  final String teamId;
  final List<RoomPlayer> players;

  @override
  Widget build(BuildContext context) {
    final color = teamId == 'red' ? Colors.redAccent : Colors.blueAccent;
    return SectionCard(
      title: teamId == 'red' ? 'Red Team' : teamId == 'blue' ? 'Blue Team' : teamId,
      trailing: Chip(
        label: Text('${players.length} players'),
        backgroundColor: color.withValues(alpha: 0.12),
        side: BorderSide(color: color.withValues(alpha: 0.32)),
      ),
      child: players.isEmpty
          ? const Text('No players yet.')
          : Column(
              children: [
                for (final player in players)
                  ListTile(
                    contentPadding: EdgeInsets.zero,
                    leading: CircleAvatar(
                      radius: 16,
                      backgroundColor: player.playerId == state.localPlayerId
                          ? color.withValues(alpha: 0.18)
                          : Theme.of(context).colorScheme.surfaceContainerHighest,
                      child: Text(
                        player.displayName.isNotEmpty ? player.displayName.characters.first : '?',
                        style: TextStyle(color: color),
                      ),
                    ),
                    title: Text(player.displayName),
                    subtitle: Text(player.playerId),
                    trailing: Text(player.ready ? 'ready' : 'waiting'),
                  ),
              ],
            ),
    );
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
