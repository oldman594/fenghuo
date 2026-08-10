import 'package:flutter/material.dart';

import '../state/app_state.dart';
import '../widgets/section_card.dart';

class MePage extends StatefulWidget {
  const MePage({super.key, required this.state});

  final AppState state;

  @override
  State<MePage> createState() => _MePageState();
}

class _MePageState extends State<MePage> {
  late final TextEditingController _baseUrlController;
  late final TextEditingController _playerIdController;
  late final TextEditingController _displayNameController;

  @override
  void initState() {
    super.initState();
    _baseUrlController = TextEditingController(text: widget.state.baseUrl);
    _playerIdController = TextEditingController(text: widget.state.localPlayerId);
    _displayNameController = TextEditingController(text: widget.state.localDisplayName);
  }

  @override
  void dispose() {
    _baseUrlController.dispose();
    _playerIdController.dispose();
    _displayNameController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final state = widget.state;
    final player = state.currentPlayerStatus;
    final roomPlayer = state.currentPlayerInSelectedRoom;

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        SectionCard(
          title: 'Player',
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              TextField(
                controller: _displayNameController,
                decoration: const InputDecoration(labelText: 'Display Name'),
              ),
              const SizedBox(height: 8),
              TextField(
                controller: _playerIdController,
                decoration: const InputDecoration(labelText: 'Player ID'),
              ),
              const SizedBox(height: 12),
              SizedBox(
                width: double.infinity,
                child: FilledButton(
                  onPressed: () {
                    state.updateProfile(
                      playerId: _playerIdController.text.trim(),
                      displayName: _displayNameController.text.trim(),
                    );
                  },
                  child: const Text('Apply Player Profile'),
                ),
              ),
              const SizedBox(height: 12),
              Text('Current team: ${player?.teamId ?? roomPlayer?.teamId ?? 'none'}'),
              Text('Ready: ${(player?.ready ?? roomPlayer?.ready ?? false) ? 'yes' : 'no'}'),
              Text('Alive: ${player?.alive == null ? 'unknown' : (player!.alive! ? 'yes' : 'no')}'),
              Text('HP: ${player?.health?.toString() ?? 'unknown'}'),
            ],
          ),
        ),
        const SizedBox(height: 12),
        SectionCard(
          title: 'Connection',
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              TextField(
                controller: _baseUrlController,
                decoration: const InputDecoration(labelText: 'Backend Base URL'),
              ),
              const SizedBox(height: 12),
              SizedBox(
                width: double.infinity,
                child: FilledButton(
                  onPressed: () => state.updateBaseUrl(_baseUrlController.text.trim()),
                  child: const Text('Apply and Reconnect'),
                ),
              ),
              const SizedBox(height: 12),
              Text('Live: ${state.liveConnected ? 'connected' : 'disconnected'}'),
              Text('Last event: ${state.lastLiveType ?? 'none'}'),
            ],
          ),
        ),
        const SizedBox(height: 12),
        SectionCard(
          title: 'Device',
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text('Device ID: ${player?.deviceId ?? 'unbound'}'),
              Text('Device online: ${player?.deviceOnline == null ? 'unknown' : (player!.deviceOnline! ? 'yes' : 'no')}'),
            ],
          ),
        ),
      ],
    );
  }
}
