import 'dart:async';

import 'package:flutter/material.dart';

import 'pages/battle_page.dart';
import 'pages/map_page.dart';
import 'pages/rooms_page.dart';
import 'pages/settings_page.dart';
import 'state/app_state.dart';

class FenghuoApp extends StatelessWidget {
  const FenghuoApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Fenghuo App V1',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.red),
        useMaterial3: true,
      ),
      home: const AppShell(),
    );
  }
}

class AppShell extends StatefulWidget {
  const AppShell({super.key});

  @override
  State<AppShell> createState() => _AppShellState();
}

class _AppShellState extends State<AppShell> {
  final AppState _state = AppState();
  int _index = 0;

  @override
  void initState() {
    super.initState();
    unawaited(_state.refreshRooms());
    _state.connectLive();
  }

  @override
  void dispose() {
    _state.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final tabs = [
      RoomsPage(state: _state),
      BattlePage(state: _state),
      MapPage(state: _state),
      SettingsPage(state: _state),
    ];

    return AnimatedBuilder(
      animation: _state,
      builder: (context, _) {
        return Scaffold(
          appBar: AppBar(
            title: const Text('Fenghuo App V1'),
            actions: [
              IconButton(
                onPressed: _state.refreshSelected,
                icon: const Icon(Icons.refresh),
              ),
            ],
          ),
          body: Column(
            children: [
              if (_state.error != null)
                MaterialBanner(
                  content: Text(_state.error!),
                  actions: [
                    TextButton(
                      onPressed: () => setState(() => _state.error = null),
                      child: const Text('Dismiss'),
                    ),
                  ],
                ),
              Expanded(child: tabs[_index]),
            ],
          ),
          bottomNavigationBar: NavigationBar(
            selectedIndex: _index,
            onDestinationSelected: (value) => setState(() => _index = value),
            destinations: const [
              NavigationDestination(icon: Icon(Icons.meeting_room), label: 'Rooms'),
              NavigationDestination(icon: Icon(Icons.shield), label: 'Battle'),
              NavigationDestination(icon: Icon(Icons.map), label: 'Map'),
              NavigationDestination(icon: Icon(Icons.settings), label: 'Settings'),
            ],
          ),
        );
      },
    );
  }
}
