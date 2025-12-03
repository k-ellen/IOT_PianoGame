import 'package:flutter/material.dart';
import 'package:firebase_core/firebase_core.dart';  
import 'firebase_options.dart'; 
import 'screens/home_screen.dart';
import 'screens/search_screen.dart';


void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  await Firebase.initializeApp(
    options: DefaultFirebaseOptions.currentPlatform,
  );

  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,  //disables flutter's default of displaying debug on the screen
      title: 'Piano Teacher App',
      home: const HomeScreen(), //when the app opens we will see the home screen
      //home: const SearchScreen(),
    );
  }
}