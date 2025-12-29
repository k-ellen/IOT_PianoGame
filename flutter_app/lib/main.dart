import 'package:flutter/material.dart';
import 'package:firebase_core/firebase_core.dart';
import 'package:flutter_app/screens/upload_screen.dart';
import 'package:flutter_app/screens/search_screen.dart';
import 'package:flutter_app/screens/welcome_screen.dart';
import 'firebase_options.dart';
import 'screens/home_screen.dart';
import 'package:firebase_auth/firebase_auth.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await Firebase.initializeApp(options: DefaultFirebaseOptions.currentPlatform);
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner:
          false, //disables flutter's default of displaying debug on the screen
      title: 'Piano Teacher App',
      home: FirebaseAuth.instance.currentUser == null
          ? const WelcomeScreen()
          : const HomeScreen(),
      routes: {
        '/upload': (context) => const UploadScreen(),
        '/search': (context) => const SearchScreen(),
        '/home': (context) => const HomeScreen(),
      },
    );
  }
}
