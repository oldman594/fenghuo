import 'package:flutter/material.dart';

import '../state/app_state.dart';
import '../widgets/section_card.dart';

class RoomsPage extends StatefulWidget {
  const RoomsPage({super.key, required this.state});

  final AppState state;

  @override
  State<RoomsPage> createState() => _RoomsPageState();
}

class _RoomsPageState extends State<RoomsPage> {
  final _roomIdController = TextEditingController(text: 'room-android-001');
  final _nameController = TextEditingController(text: 'Android Debug Room');
  final _modeController = TextEditingController(text: 'team_deathmatch');
  final _maxPlayersController = TextEditingController(text: '8');

  @override
  void dispose() {
    _roomIdController.dispose();
    _nameController.dispose();
    _modeController.dispose();
    _maxPlayersController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final state = widget.state;
    final selected = state.selectedRoom;

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        SectionCard(
          title: 'Create Room',
          child: Column(
            children: [
              TextField(
                controller: _roomIdController,
                decoration: const InputDecoration(labelText: 'Room ID'),
              ),
              const SizedBox(height: 8),
              TextField(
                controller: _nameController,
                decoration: const InputDecoration(labelText: 'Name'),
              ),
              const SizedBox(height: 8),
              TextField(
                controller: _modeController,
                decoration: const InputDecoration(labelText: 'Mode'),
              ),
              const SizedBox(height: 8),
              TextField(
                controller: _maxPlayersController,
                keyboardType: TextInputType.number,
                decoration: const InputDecoration(labelText: 'Max Players'),
              ),
              const SizedBox(height: 12),
              SizedBox(
                width: double.infinity,
                child: FilledButton(
                  onPressed: state.loading
                      ? null
                      : () => state.createRoom(
                            roomId: _roomIdController.text.trim(),
                            name: _nameController.text.trim(),
                            mode: _modeController.text.trim(),
                            maxPlayers: int.tryParse(_maxPlayersController.text) ?? 8,
                          ),
                  child: const Text('Create Room'),
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 12),
        SectionCard(
          title: 'Rooms',
          trailing: state.loading
              ? const SizedBox(width: 18, height: 18, child: CircularProgressIndicator(strokeWidth: 2))
              : null,
          child: Column(
            children: [
              for (final room in state.rooms)
                ListTile(
                  contentPadding: EdgeInsets.zero,
                  title: Text(room.name.isEmpty ? room.roomId : room.name),
                  subtitle: Text('${room.roomId} · ${room.phase} · ${room.mode}'),
                  trailing: Text('${room.playerCount}/${room.maxPlayers}'),
                  onTap: () => state.selectRoom(room.roomId),
                ),
              if (state.rooms.isEmpty) const Text('No rooms loaded.'),
            ],
          ),
        ),
        if (selected != null) ...[
          const SizedBox(height: 12),
          SectionCard(
            title: 'Selected Room',
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('${selected.room.name} (${selected.room.roomId})'),
                Text('phase: ${selected.room.phase} · mode: ${selected.room.mode}'),
                Text('battle: ${selected.room.battleId ?? 'none'}'),
              ],
            ),
          ),
          const SizedBox(height: 12),
          SectionCard(
            title: 'Players',
            child: Column(
              children: [
                for (final player in selected.players.values)
                  ListTile(
                    contentPadding: EdgeInsets.zero,
                    title: Text(player.displayName),
                    subtitle: Text('${player.playerId} · ${player.teamId}'),
                    trailing: Text(player.ready ? 'ready' : 'not ready'),
                  ),
                if (selected.players.isEmpty) const Text('No players.'),
              ],
            ),
          ),
        ],
      ],
    );
  }
}
