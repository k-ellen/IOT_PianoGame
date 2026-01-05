import 'package:flutter/material.dart';
import 'package:firebase_auth/firebase_auth.dart';
import 'package:cloud_firestore/cloud_firestore.dart';

import '../services/stats_service.dart';
import 'welcome_screen.dart';
import 'search_screen.dart';

class AuthGate extends StatelessWidget {
  const AuthGate({super.key});

  @override
  Widget build(BuildContext context) {
    return StreamBuilder<User?>(
      stream: FirebaseAuth.instance.authStateChanges(),
      builder: (context, snapshot) {
        if (snapshot.connectionState == ConnectionState.waiting) {
          return const Scaffold(
            backgroundColor: Colors.black,
            body: Center(child: CircularProgressIndicator()),
          );
        }

        // ❗ לא מחובר → מסך התחלה/התחברות
        if (!snapshot.hasData) {
          return const WelcomeScreen();
        }

        // ✅ מחובר → דואגים לסטטיסטיקות ואז נכנסים לחיפוש
        final user = snapshot.data!;
        return FutureBuilder(
          future: StatsService(FirebaseFirestore.instance)
              .ensureGeneralStats(user.uid),
          builder: (context, statsSnapshot) {
            if (statsSnapshot.hasError) {
              return Scaffold(
                backgroundColor: Colors.black,
                body: Center(
                  child: Text(
                    'Stats init error:\n${statsSnapshot.error}',
                    style: const TextStyle(color: Colors.red),
                    textAlign: TextAlign.center,
                  ),
                ),
              );
            }

            if (statsSnapshot.connectionState != ConnectionState.done) {
              return const Scaffold(
                backgroundColor: Colors.black,
                body: Center(child: CircularProgressIndicator()),
              );
            }

            return const SearchScreen();
          },
        );
      },
    );
  }
}
