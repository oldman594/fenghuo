import 'dart:async';

import 'package:flutter/material.dart';

import 'pages/battle_page.dart';
import 'pages/home_page.dart';
import 'pages/me_page.dart';
import 'pages/room_page.dart';
import 'state/app_state.dart';

class FenghuoApp extends StatelessWidget {
  const FenghuoApp({super.key});

  @override
  Widget build(BuildContext context) {
    const seed = Color(0xFFE0564A);
    return MaterialApp(
      title: 'Fenghuo App V1',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: seed,
          brightness: Brightness.dark,
        ),
        useMaterial3: true,
        scaffoldBackgroundColor: const Color(0xFF0E1217),
        cardTheme: const CardThemeData(
          color: Color(0xFF171C23),
          margin: EdgeInsets.zero,
        ),
        appBarTheme: const AppBarTheme(
          backgroundColor: Color(0xFF0E1217),
          foregroundColor: Colors.white,
          elevation: 0,
        ),
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
      HomePage(state: _state),
      RoomPage(state: _state),
      BattlePage(state: _state),
      MePage(state: _state),
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
                tooltip: 'Refresh',
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
              NavigationDestination(icon: Icon(Icons.home_outlined), label: 'Home'),
              NavigationDestination(icon: Icon(Icons.groups_outlined), label: 'Room'),
              NavigationDestination(icon: Icon(Icons.map_outlined), label: 'Battle'),
              NavigationDestination(icon: Icon(Icons.person_outline), label: 'Me'),
            ],
          ),
        );
      },
    );
  }
}
