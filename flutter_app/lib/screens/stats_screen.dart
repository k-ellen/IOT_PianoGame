import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:firebase_auth/firebase_auth.dart';
import 'package:flutter/material.dart';

import '../widgets/footer/bottom_navigation_bar.dart';

class StatsScreen extends StatelessWidget {
  const StatsScreen({super.key});

  String _formatSeconds(int totalSeconds) {
    final int minutes = totalSeconds ~/ 60;
    final int hours = minutes ~/ 60;

    final int remMinutes = minutes % 60;
    final int remSeconds = totalSeconds % 60;

    if (hours > 0) {
      return '${hours}h ${remMinutes}m';
    }
    if (minutes > 0) {
      return '${minutes}m ${remSeconds}s';
    }
    return '${remSeconds}s';
  }

  @override
  Widget build(BuildContext context) {
    final user = FirebaseAuth.instance.currentUser;
    if (user == null) {
      return const Scaffold(
        backgroundColor: Colors.black,
        body: Center(
          child: Text('Not signed in', style: TextStyle(color: Colors.white)),
        ),
      );
    }

    final ref = FirebaseFirestore.instance
        .collection('users')
        .doc(user.uid)
        .collection('stats')
        .doc('general');

    return Scaffold(
      backgroundColor: Colors.black,
      appBar: AppBar(
        backgroundColor: Colors.black,
        foregroundColor: Colors.white,
        title: const Text('Statistics'),
      ),
      body: StreamBuilder<DocumentSnapshot<Map<String, dynamic>>>(
        stream: ref.snapshots(),
        builder: (context, snapshot) {
          if (!snapshot.hasData) {
            return const Center(child: CircularProgressIndicator());
          }

          final data = snapshot.data!.data() ?? <String, dynamic>{};

          final int totalPracticeSeconds =
              (data['totalPracticeSeconds'] ?? 0) is int
                  ? data['totalPracticeSeconds'] as int
                  : (data['totalPracticeSeconds'] as num?)?.toInt() ?? 0;

          final int totalPlaysCount = (data['totalPlaysCount'] ?? 0) is int
              ? data['totalPlaysCount'] as int
              : (data['totalPlaysCount'] as num?)?.toInt() ?? 0;

          final int currentStreakDays = (data['currentStreakDays'] ?? 0) is int
              ? data['currentStreakDays'] as int
              : (data['currentStreakDays'] as num?)?.toInt() ?? 0;

          final String lastPracticeDate = (data['lastPracticeDate'] ?? '') as String;
          final String lastPlayedSongId = (data['lastPlayedSongId'] ?? '') as String;

          return Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              children: [
                _StatCard(
                  title: 'Total practice time',
                  value: _formatSeconds(totalPracticeSeconds),
                ),
                const SizedBox(height: 12),
                _StatCard(
                  title: 'Total plays',
                  value: '$totalPlaysCount',
                ),
                const SizedBox(height: 12),
                _StatCard(
                  title: 'Current streak',
                  value: '$currentStreakDays days',
                ),
                const SizedBox(height: 12),
                _StatCard(
                  title: 'Last practice date',
                  value: lastPracticeDate.isEmpty ? '-' : lastPracticeDate,
                ),
                const SizedBox(height: 12),
                _StatCard(
                  title: 'Last played song',
                  value: lastPlayedSongId.isEmpty ? '-' : lastPlayedSongId,
                ),
              ],
            ),
          );
        },
      ),
      bottomNavigationBar: MyBottomNavigationBar(
        currentIndex: 3, 
        onTap: (i) {
          if (i == 0) Navigator.pushReplacementNamed(context, '/home');
          if (i == 1) Navigator.pushReplacementNamed(context, '/search');
          if (i == 2) Navigator.pushReplacementNamed(context, '/upload');
          if (i == 3) Navigator.pushReplacementNamed(context, '/stats');
        },
      ),
    );
  }
}

class _StatCard extends StatelessWidget {
  const _StatCard({required this.title, required this.value});

  final String title;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: const Color(0xFF1E1E1E),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: Colors.white12),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(title, style: const TextStyle(color: Colors.white70, fontSize: 14)),
          const SizedBox(height: 6),
          Text(value, style: const TextStyle(color: Colors.white, fontSize: 20, fontWeight: FontWeight.bold)),
        ],
      ),
    );
  }
}
