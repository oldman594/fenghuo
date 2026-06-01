import 'package:flutter/material.dart';

import '../state/app_state.dart';
import '../widgets/section_card.dart';

class SettingsPage extends StatefulWidget {
  const SettingsPage({super.key, required this.state});

  final AppState state;

  @override
  State<SettingsPage> createState() => _SettingsPageState();
}

class _SettingsPageState extends State<SettingsPage> {
  late final TextEditingController _baseUrlController;

  @override
  void initState() {
    super.initState();
    _baseUrlController = TextEditingController(text: widget.state.baseUrl);
  }

  @override
  void dispose() {
    _baseUrlController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final state = widget.state;

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        SectionCard(
          title: 'Backend',
          child: Column(
            children: [
              TextField(
                controller: _baseUrlController,
                decoration: const InputDecoration(labelText: 'Base URL'),
              ),
              const SizedBox(height: 12),
              SizedBox(
                width: double.infinity,
                child: FilledButton(
                  onPressed: () {
                    state.baseUrl = _baseUrlController.text.trim();
                    state.connectLive();
                    state.refreshSelected();
                  },
                  child: const Text('Apply and Reconnect'),
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 12),
        SectionCard(
          title: 'Live Messages',
          child: Column(
            children: [
              for (final message in state.liveMessages.take(20))
                ListTile(
                  contentPadding: EdgeInsets.zero,
                  title: Text(message.type),
                  subtitle: Text(
                    message.raw.toString(),
                    maxLines: 3,
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
              if (state.liveMessages.isEmpty) const Text('No live messages yet.'),
            ],
          ),
        ),
      ],
    );
  }
}
