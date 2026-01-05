import 'package:flutter/material.dart';
import '../widgets/header/my_header.dart';
import '../services/auth_service.dart';
import 'package:firebase_auth/firebase_auth.dart';

class SignInScreen extends StatefulWidget {
  const SignInScreen({super.key});

  @override
  State<SignInScreen> createState() => _SignInScreenState();
}

class _SignInScreenState extends State<SignInScreen> {
  final _emailController = TextEditingController();
  final _passwordController = TextEditingController();
  final _authService = AuthService();

  bool _isLoading = false;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.black,
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 25),
          child: Column(
            children: [
              // HEADER AT TOP
              const MyHeader(title: 'Signin', isBackButton: true),

              // CONTENT
              Expanded(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    const SizedBox(height: 100),

                    _inputField(controller: _emailController, hint: 'Email'),
                    const SizedBox(height: 40),

                    _inputField(
                      controller: _passwordController,
                      hint: 'Password',
                      obscure: true,
                    ),
                    const SizedBox(height: 70),

                    _primaryButton(
                      text: _isLoading ? 'Signing in...' : 'Signin',
                      onTap: _isLoading
                          ? () {}
                          : () async {
                              setState(() => _isLoading = true);

                              try {
                                await _authService.signIn(
                                  email: _emailController.text.trim(),
                                  password: _passwordController.text.trim(),
                                );

                                if (!mounted) return;
                               Navigator.pushNamedAndRemoveUntil(context, '/', (route) => false);

                                
                              } on FirebaseAuthException catch (e) {
                                _showError(e.message ?? 'Signin failed');
                              } catch (_) {
                                _showError('Something went wrong');
                              } finally {
                                if (mounted) setState(() => _isLoading = false);
                              }
                            },
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _inputField({
    required TextEditingController controller,
    required String hint,
    bool obscure = false,
  }) {
    return TextField(
      controller: controller,
      obscureText: obscure,
      style: const TextStyle(color: Colors.white),
      decoration: InputDecoration(
        hintText: hint,
        hintStyle: const TextStyle(color: Colors.white54),
        enabledBorder: const UnderlineInputBorder(
          borderSide: BorderSide(color: Colors.white38),
        ),
        focusedBorder: const UnderlineInputBorder(
          borderSide: BorderSide(color: Colors.blue),
        ),
      ),
    );
  }

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

  void _showError(String message) {
    showDialog(
      context: context,
      builder: (_) => AlertDialog(
        backgroundColor: Colors.black,
        title: const Text('Error', style: TextStyle(color: Colors.white)),
        content: Text(message, style: const TextStyle(color: Colors.white70)),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('OK'),
          ),
        ],
      ),
    );
  }
}
