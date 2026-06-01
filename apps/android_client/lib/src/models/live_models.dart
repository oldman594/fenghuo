class LiveMessage {
  LiveMessage({
    required this.type,
    required this.raw,
  });

  final String type;
  final Map<String, dynamic> raw;

  factory LiveMessage.fromJson(Map<String, dynamic> json) {
    return LiveMessage(
      type: json['type'] as String? ?? 'unknown',
      raw: json,
    );
  }
}
