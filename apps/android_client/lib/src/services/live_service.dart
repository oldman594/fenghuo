import 'dart:convert';

import 'package:web_socket_channel/web_socket_channel.dart';

import '../models/live_models.dart';

class LiveService {
  LiveService({required this.baseUrl, this.wsPath = '/api/v0/live'});

  final String baseUrl;
  final String wsPath;

  WebSocketChannel connect() {
    final httpUri = Uri.parse(baseUrl);
    final wsUri = httpUri.replace(
      scheme: httpUri.scheme == 'https' ? 'wss' : 'ws',
      path: wsPath,
    );
    return WebSocketChannel.connect(wsUri);
  }

  LiveMessage decode(dynamic message) {
    final json = (jsonDecode(message as String) as Map).cast<String, dynamic>();
    return LiveMessage.fromJson(json);
  }
}
