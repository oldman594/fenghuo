import 'package:flutter_test/flutter_test.dart';

import 'package:fenghuo_android_client/main.dart';

void main() {
  testWidgets('app shell renders', (WidgetTester tester) async {
    await tester.pumpWidget(const FenghuoApp());

    expect(find.text('Fenghuo App V1'), findsOneWidget);
    expect(find.text('Rooms'), findsWidgets);
  });
}
