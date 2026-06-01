import 'package:flutter/material.dart';

import '../state/app_state.dart';
import '../widgets/section_card.dart';

class BattlePage extends StatelessWidget {
  const BattlePage({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    final battle = state.selectedBattle;

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        SectionCard(
          title: 'Battle Summary',
          child: battle == null
              ? const Text('No battle selected.')
              : Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text('battle: ${battle.battleId}'),
                    Text('phase: ${battle.phase}'),
                    Text('mode: ${battle.mode ?? 'unknown'}'),
                    const SizedBox(height: 8),
                    Text('red score: ${battle.teams['red']?.score ?? 0}'),
                    Text('blue score: ${battle.teams['blue']?.score ?? 0}'),
                  ],
                ),
        ),
        const SizedBox(height: 12),
        SectionCard(
          title: 'Battle Players',
          child: battle == null
              ? const Text('No battle selected.')
              : Column(
                  children: [
                    for (final player in battle.players.values)
                      ListTile(
                        contentPadding: EdgeInsets.zero,
                        title: Text(player.displayName),
                        subtitle: Text('${player.playerId} · ${player.teamId}'),
                        trailing: Text('${player.health} hp · ${player.shotCount}/${player.hitCount}'),
                      ),
                  ],
                ),
        ),
      ],
    );
  }
}
