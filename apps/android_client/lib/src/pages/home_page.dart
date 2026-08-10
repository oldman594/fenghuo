import 'package:flutter/material.dart';

import '../models/room_models.dart';
import '../state/app_state.dart';
import '../widgets/section_card.dart';

class HomePage extends StatelessWidget {
  const HomePage({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        SectionCard(
          title: 'Connection',
          trailing: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(
                state.liveConnected ? Icons.wifi : Icons.wifi_off,
                size: 18,
                color: state.liveConnected ? Colors.green : Colors.orange,
              ),
              const SizedBox(width: 6),
              Text(state.liveConnected ? 'Live' : 'Polling'),
            ],
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(state.baseUrl),
              const SizedBox(height: 6),
              Text('Player: ${state.localDisplayName} (${state.localPlayerId})'),
              if (state.lastLiveType != null) ...[
                const SizedBox(height: 6),
                Text('Last event: ${state.lastLiveType}'),
              ],
            ],
          ),
        ),
        const SizedBox(height: 12),
        SectionCard(
          title: 'Rooms',
          trailing: state.loading
              ? const SizedBox(width: 18, height: 18, child: CircularProgressIndicator(strokeWidth: 2))
              : IconButton(
                  onPressed: state.refreshRooms,
                  icon: const Icon(Icons.refresh),
                  tooltip: 'Refresh rooms',
                ),
          child: state.rooms.isEmpty
              ? const Text('No rooms available.')
              : Column(
                  children: [
                    for (final room in state.rooms) _RoomRow(state: state, room: room),
                  ],
                ),
        ),
      ],
    );
  }
}

class _RoomRow extends StatelessWidget {
  const _RoomRow({
    required this.state,
    required this.room,
  });

  final AppState state;
  final RoomSummary room;

  Color _phaseColor(BuildContext context) {
    switch (room.phase) {
      case 'active':
        return Colors.redAccent;
      case 'open':
        return Colors.green;
      case 'closed':
        return Theme.of(context).colorScheme.outline;
      default:
        return Colors.orange;
    }
  }

  @override
  Widget build(BuildContext context) {
    return ListTile(
      contentPadding: EdgeInsets.zero,
      title: Text(room.name.isEmpty ? room.roomId : room.name),
      subtitle: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('${room.mode} · ${room.playerCount}/${room.maxPlayers}'),
          const SizedBox(height: 4),
          Wrap(
            spacing: 8,
            runSpacing: 6,
            children: [
              Chip(
                label: Text(room.phase),
                labelStyle: const TextStyle(fontSize: 12),
                backgroundColor: _phaseColor(context).withValues(alpha: 0.12),
                side: BorderSide(color: _phaseColor(context).withValues(alpha: 0.32)),
              ),
              for (final team in room.teamSummaries)
                Chip(
                  label: Text('${team.displayName} ${team.playerCount}/${team.maxPlayers}'),
                  labelStyle: const TextStyle(fontSize: 12),
                ),
            ],
          ),
        ],
      ),
      trailing: const Icon(Icons.chevron_right),
      onTap: () => state.selectRoom(room.roomId),
    );
  }
}
