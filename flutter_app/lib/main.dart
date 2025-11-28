import 'package:flutter/material.dart';
import 'screens/home_screen.dart';
import 'screens/search_screen.dart';


void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,  //disables flutter's default of displaying debug on the screen
      title: 'Piano Teacher App',
      //home: const HomeScreen(), //when the app opens we will see the home screen
      home: const SearchScreen(),
    );
  }
}