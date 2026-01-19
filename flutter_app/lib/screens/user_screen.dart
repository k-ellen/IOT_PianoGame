import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:firebase_auth/firebase_auth.dart';
import 'package:flutter/material.dart';

import '../services/auth_service.dart';
import '../services/stats_service.dart';
import '../widgets/footer/bottom_navigation_bar.dart';

// =====================
// User Screen
// =====================
class UserScreen extends StatelessWidget {
  const UserScreen({super.key});

  static const LinearGradient _bgGradient = LinearGradient(
    begin: Alignment.topCenter,
    end: Alignment.bottomCenter,
    colors: [
      Color(0xFFB3E5FC), // light blue
      Color(0xFF1E88E5), // blue
      Color(0xFF0D1B3D), // dark blue
    ],
  );

  // ===== Cards/UI palette =====
  static const Color _cardDark = Color(0xFF161823);
  static const Color _borderDark = Color(0xFF23263A);
  static const Color _blueAccent = Color(0xFF4DA3FF);
  static const Color _blueSoft = Color(0xFF1C2A44);
  static const Color _textSecondary = Color(0xFF9AA4C7);

  Future<void> _confirmLogout(BuildContext context) async {
    final bool? shouldLogout = await showDialog<bool>(
      context: context,
      barrierDismissible: true,
      builder: (ctx) => AlertDialog(
        backgroundColor: _cardDark,
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
        title: const Text(
          'Log out?',
          style: TextStyle(color: Colors.white, fontWeight: FontWeight.w800),
        ),
        content: const Text(
          'Are you sure you want to log out?',
          style: TextStyle(color: _textSecondary),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: const Text('Cancel', style: TextStyle(color: _textSecondary)),
          ),
          TextButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text(
              'Log out',
              style: TextStyle(color: _blueAccent, fontWeight: FontWeight.w800),
            ),
          ),
        ],
      ),
    );

    if (shouldLogout == true) {
      await AuthService().signOut();
      if (!context.mounted) return;
      Navigator.pushNamedAndRemoveUntil(context, '/', (route) => false);
    }
  }

  int _toInt(dynamic v) {
    if (v == null) return 0;
    if (v is int) return v;
    if (v is num) return v.toInt();
    if (v is String) return int.tryParse(v) ?? 0;
    return 0;
  }

  String _toStr(dynamic v) {
    if (v == null) return '';
    if (v is String) return v;
    return v.toString();
  }

  String _formatSeconds(int totalSeconds) {
    final int minutes = totalSeconds ~/ 60;
    final int hours = minutes ~/ 60;
    final int remMinutes = minutes % 60;
    final int remSeconds = totalSeconds % 60;

    if (hours > 0) return '${hours}h ${remMinutes}m';
    if (minutes > 0) return '${minutes}m ${remSeconds}s';
    return '${remSeconds}s';
  }

  @override
  Widget build(BuildContext context) {
    final user = FirebaseAuth.instance.currentUser;

    if (user == null) {
      return Scaffold(
        body: Container(
          decoration: const BoxDecoration(gradient: _bgGradient),
          child: const SafeArea(
            child: Center(
              child: Text('Not signed in', style: TextStyle(color: Colors.white)),
            ),
          ),
        ),
        bottomNavigationBar: const MyBottomNavigationBar(currentIndex: 2),
      );
    }

    // ✅ IMPORTANT: ensure stats doc exists (otherwise you'll see 0s)
    StatsService(FirebaseFirestore.instance).ensureGeneralStats(user.uid);

    final userRef = FirebaseFirestore.instance.collection('users').doc(user.uid);
    final statsRef = userRef.collection('stats').doc('general');

    return Scaffold(
      body: Container(
        decoration: const BoxDecoration(gradient: _bgGradient),
        child: SafeArea(
          child: Column(
            children: [
              // ===== TOP BAR =====
              Padding(
                padding: const EdgeInsets.fromLTRB(16, 16, 16, 8),
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  crossAxisAlignment: CrossAxisAlignment.center,
                  children: [
                    Row(
                      children: [
                        const Text(
                          'User',
                          style: TextStyle(
                            color: Colors.black,
                            fontSize: 32,
                            fontWeight: FontWeight.bold,
                          ),
                        ),
                        const SizedBox(width: 8),
                        IconButton(
                          icon: const Icon(Icons.logout, color: Colors.black, size: 30),
                          padding: EdgeInsets.zero,
                          constraints: const BoxConstraints(),
                          tooltip: 'Log out',
                          onPressed: () => _confirmLogout(context),
                        ),
                      ],
                    ),
                    ClipRRect(
                      borderRadius: BorderRadius.circular(12),
                      child: Image.asset(
                        'assets/images/logo.png',
                        width: 40,
                        height: 40,
                        fit: BoxFit.cover,
                      ),
                    ),
                  ],
                ),
              ),

              const SizedBox(height: 4),

              Expanded(
                child: StreamBuilder<DocumentSnapshot<Map<String, dynamic>>>(
                  stream: statsRef.snapshots(),
                  builder: (context, statsSnap) {
                    if (!statsSnap.hasData) {
                      return const Center(child: CircularProgressIndicator());
                    }

                    final stats = statsSnap.data!.data() ?? <String, dynamic>{};

                    final int totalPracticeSeconds = _toInt(stats['totalPracticeSeconds']);
                    final int totalPlaysCount = _toInt(stats['totalPlaysCount']);
                    final int currentStreakDays = _toInt(stats['currentStreakDays']);

                    final String lastPracticeDate = _toStr(stats['lastPracticeDate']);
                    final String lastPlayedSongId = _toStr(stats['lastPlayedSongId']);

                    final int learnedSongsCount = _toInt(stats['learnedSongsCount']);
                    final int hardPlaysCount = _toInt(stats['hardPlaysCount']);

                    return FutureBuilder<DocumentSnapshot<Map<String, dynamic>>>(
                      future: userRef.get(),
                      builder: (context, userSnap) {
                        final userData = userSnap.data?.data() ?? <String, dynamic>{};
                        final String firstName = _toStr(userData['firstName']);
                        final String lastName = _toStr(userData['lastName']);

                        final String displayName =
                            (firstName.trim().isEmpty && lastName.trim().isEmpty)
                                ? (user.email ?? 'User')
                                : '${firstName.trim()} ${lastName.trim()}';

                        return SingleChildScrollView(
                          child: Column(
                            children: [
                              _TopHeader(
                                displayName: displayName,
                                subtitle: '',
                                learnedSongsCount: learnedSongsCount,
                                currentStreakDays: currentStreakDays,
                                hardLearnedSongsCount: hardPlaysCount,
                                totalPlaysCount: totalPlaysCount,
                              ),
                              const SizedBox(height: 24),
                              Padding(
                                padding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
                                child: Column(
                                  children: [
                                    
                                    _DarkStatCard(
                                      title: 'Last practice date',
                                      value: lastPracticeDate.trim().isEmpty ? '-' : lastPracticeDate,
                                      icon: Icons.calendar_today_outlined,
                                    ),
                                    const SizedBox(height: 12),
                                    _DarkStatCard(
                                      title: 'Last played song',
                                      value: lastPlayedSongId.trim().isEmpty ? '-' : lastPlayedSongId,
                                      icon: Icons.music_note_outlined,
                                    ),
                                  ],
                                ),
                              ),
                            ],
                          ),
                        );
                      },
                    );
                  },
                ),
              ),
            ],
          ),
        ),
      ),
      bottomNavigationBar: const MyBottomNavigationBar(currentIndex: 2),
    );
  }
}

// =====================
// TOP HEADER
// =====================
class _TopHeader extends StatelessWidget {
  const _TopHeader({
    required this.displayName,
    required this.subtitle,
    required this.learnedSongsCount,
    required this.currentStreakDays,
    required this.hardLearnedSongsCount,
    required this.totalPlaysCount,
  });

  final String displayName;
  final String subtitle;
  final int learnedSongsCount;
  final int currentStreakDays;
  final int hardLearnedSongsCount;
  final int totalPlaysCount;

  static const Color _cardDark = UserScreen._cardDark;
  static const Color _borderDark = UserScreen._borderDark;
  static const Color _blueAccent = UserScreen._blueAccent;
  static const Color _blueSoft = UserScreen._blueSoft;
  static const Color _textSecondary = UserScreen._textSecondary;

  @override
  Widget build(BuildContext context) {
    const double headerTopHeight = 170;
    const double avatarSize = 84;

    const double estimatedCardHeight = 200;
    final double cardTop = headerTopHeight - (estimatedCardHeight / 2);
    final double avatarTop = cardTop - (avatarSize / 2);
    final double totalHeaderHeight = headerTopHeight + (estimatedCardHeight / 2) + 20;

    return SizedBox(
      height: totalHeaderHeight,
      child: Stack(
        clipBehavior: Clip.none,
        children: [
          SizedBox(
            width: double.infinity,
            height: headerTopHeight + (estimatedCardHeight / 2),
          ),
          Positioned(
            left: 16,
            right: 16,
            top: cardTop,
            child: Container(
              padding: const EdgeInsets.fromLTRB(16, 62, 16, 18),
              decoration: BoxDecoration(
                color: _cardDark,
                borderRadius: BorderRadius.circular(22),
                border: Border.all(color: _borderDark),
                boxShadow: [
                  BoxShadow(
                    blurRadius: 28,
                    offset: const Offset(0, 14),
                    color: Colors.black.withOpacity(0.55),
                  ),
                ],
              ),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Text(
                    displayName,
                    textAlign: TextAlign.center,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                      color: Colors.white,
                      fontSize: 22,
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                  const SizedBox(height: 6),
                  Text(
                    subtitle,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                      color: _textSecondary,
                      fontSize: 12,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                  const SizedBox(height: 18),
                  Row(
                    children: [
                      Expanded(child: _TopMiniStatDark(label: 'Played', value: '$totalPlaysCount')),
                      _divider(),
                      Expanded(child: _TopMiniStatDark(label: 'Streak', value: '$currentStreakDays')),
                      _divider(),
                      Expanded(child: _TopMiniStatDark(label: 'Hard', value: '$hardLearnedSongsCount')),
                    ],
                  ),
                ],
              ),
            ),
          ),
          Positioned(
            top: avatarTop,
            left: 0,
            right: 0,
            child: Center(
              child: Container(
                width: avatarSize,
                height: avatarSize,
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  color: _cardDark,
                  border: Border.all(color: _borderDark, width: 2),
                  boxShadow: [
                    BoxShadow(
                      blurRadius: 18,
                      offset: const Offset(0, 10),
                      color: Colors.black.withOpacity(0.6),
                    ),
                  ],
                ),
                child: Container(
                  margin: const EdgeInsets.all(6),
                  decoration: BoxDecoration(
                    shape: BoxShape.circle,
                    color: _blueSoft,
                    border: Border.all(color: _borderDark),
                  ),
                  child: const Icon(Icons.person, size: 44, color: _blueAccent),
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }

  static Widget _divider() {
    return Container(
      width: 1,
      height: 34,
      color: _borderDark,
      margin: const EdgeInsets.symmetric(horizontal: 8),
    );
  }
}

class _TopMiniStatDark extends StatelessWidget {
  const _TopMiniStatDark({required this.label, required this.value});

  final String label;
  final String value;

  static const Color _textSecondary = UserScreen._textSecondary;

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        Text(
          value,
          style: const TextStyle(fontSize: 24, fontWeight: FontWeight.w800, color: Colors.white),
        ),
        const SizedBox(height: 4),
        Text(
          label,
          style: const TextStyle(fontSize: 16, color: _textSecondary, fontWeight: FontWeight.w600),
        ),
      ],
    );
  }
}

// =====================
// Stat Card (Dark)
// =====================
class _DarkStatCard extends StatelessWidget {
  const _DarkStatCard({
    required this.title,
    required this.value,
    required this.icon,
  });

  final String title;
  final String value;
  final IconData icon;

  static const Color _cardDark = UserScreen._cardDark;
  static const Color _borderDark = UserScreen._borderDark;
  static const Color _blueAccent = UserScreen._blueAccent;
  static const Color _blueSoft = UserScreen._blueSoft;
  static const Color _textSecondary = UserScreen._textSecondary;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: _cardDark,
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: _borderDark),
        boxShadow: [
          BoxShadow(
            blurRadius: 16,
            offset: const Offset(0, 10),
            color: Colors.black.withOpacity(0.28),
          ),
        ],
      ),
      child: Row(
        children: [
          Container(
            width: 40,
            height: 40,
            decoration: BoxDecoration(
              color: _blueSoft,
              borderRadius: BorderRadius.circular(12),
              border: Border.all(color: _borderDark),
            ),
            child: Icon(icon, color: _blueAccent),
          ),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  title,
                  style: const TextStyle(
                    color: _textSecondary,
                    fontSize: 13,
                    fontWeight: FontWeight.w600,
                  ),
                ),
                const SizedBox(height: 6),
                Text(
                  value,
                  style: const TextStyle(
                    color: Colors.white,
                    fontSize: 18,
                    fontWeight: FontWeight.w800,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}