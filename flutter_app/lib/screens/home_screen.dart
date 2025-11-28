import 'package:flutter/material.dart';

class HomeScreen extends StatelessWidget {  //HomeScreen is a screen that doesnt change
  const HomeScreen({super.key});  //constructor

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF1E1E1E), 
      body: SafeArea(
        child: Center(
          child: Column(
            mainAxisSize: MainAxisSize.min, //A column will only hold what it needs
            children: [
              ClipRRect(
                borderRadius: BorderRadius.circular(32),
                child: Image.asset(
                  'assets/images/logo.png',
                  width: 180,
                  height: 180,
                  fit: BoxFit.cover,
                ),
              ),
              const SizedBox(height: 24), //24 pixel space between image and text
              const Text(
                'Piano Teacher',
                style: TextStyle(
                  color: Colors.white,
                  fontSize: 28,
                  fontWeight: FontWeight.bold,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}