import 'package:flutter/material.dart';
import 'login_screen.dart';
import 'signup_screen.dart';

class WelcomeScreen extends StatelessWidget {
  const WelcomeScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.black,
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 24),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              // Logo
              ClipRRect(
                //Shape with rounded corners
                borderRadius: BorderRadius.circular(47),
                child: Image.asset(
                  'assets/images/logo.png',
                  width: 200,
                  height: 200,
                  fit: BoxFit.cover,
                ),
              ),
              const SizedBox(height: 16),

              // App name
              const Text(
                'Piano Teacher',
                style: TextStyle(
                  color: Colors.white,
                  fontSize: 26,
                  fontWeight: FontWeight.w600,
                ),
              ),

              const SizedBox(height: 48),

              // Signup button (primary)
              _primaryButton(
                text: 'Signup',
                onTap: () {
                  Navigator.push(
                    context,
                    MaterialPageRoute(builder: (_) => const SignUpScreen()),
                  );
                },
              ),

              const SizedBox(height: 16),

              // Login button (outlined)
              _outlinedButton(
                text: 'Login',
                onTap: () {
                  Navigator.push(
                    context,
                    MaterialPageRoute(builder: (_) => const SignInScreen()),
                  );
                },
              ),
            ],
          ),
        ),
      ),
    );
  }

  // ==========================
  // Buttons
  // ==========================

  Widget _primaryButton({required String text, required VoidCallback onTap}) {
    return SizedBox(
      width: double.infinity,
      height: 48,
      child: ElevatedButton(
        onPressed: onTap,
        style: ElevatedButton.styleFrom(
          backgroundColor: Colors.blue,
          foregroundColor: Colors.black,
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(6)),
        ),
        child: Text(text, style: const TextStyle(fontSize: 16)),
      ),
    );
  }

  Widget _outlinedButton({required String text, required VoidCallback onTap}) {
    return SizedBox(
      width: double.infinity,
      height: 48,
      child: OutlinedButton(
        onPressed: onTap,
        style: OutlinedButton.styleFrom(
          side: const BorderSide(color: Colors.blue),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(6)),
        ),
        child: Text(
          text,
          style: const TextStyle(fontSize: 16, color: Colors.blue),
        ),
      ),
    );
  }
}
