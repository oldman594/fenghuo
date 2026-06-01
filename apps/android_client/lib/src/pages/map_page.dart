import 'package:flutter/material.dart';

import '../state/app_state.dart';
import '../widgets/section_card.dart';

class MapPage extends StatelessWidget {
  const MapPage({super.key, required this.state});

  final AppState state;

  @override
  Widget build(BuildContext context) {
    final map = state.selectedMap;

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        SectionCard(
          title: 'Map Summary',
          child: map == null
              ? const Text('No map selected.')
              : Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text('room: ${map.roomId}'),
                    Text('phase: ${map.phase}'),
                    Text('positions: ${map.positions.length}'),
                  ],
                ),
        ),
        const SizedBox(height: 12),
        SectionCard(
          title: 'Positions',
          child: map == null
              ? const Text('No map selected.')
              : Column(
                  children: [
                    for (final position in map.positions.values)
                      ListTile(
                        contentPadding: EdgeInsets.zero,
                        title: Text(position.playerId),
                        subtitle: Text(
                          'x=${position.x.toStringAsFixed(1)} '
                          'y=${position.y.toStringAsFixed(1)} '
                          'heading=${position.headingDeg.toStringAsFixed(0)}',
                        ),
                        trailing: Text('${position.velocityMps.toStringAsFixed(1)} m/s'),
                      ),
                  ],
                ),
        ),
      ],
    );
  }
}
