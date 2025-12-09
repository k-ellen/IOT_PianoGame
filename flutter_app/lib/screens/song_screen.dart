import 'package:flutter/material.dart';
import 'package:firebase_database/firebase_database.dart';
import '../widgets/header/my_header.dart';
import '../widgets/footer/bottom_navigation_bar.dart';
import '../widgets/my_button.dart';
import 'dart:async';

class SongScreen extends StatefulWidget {
  final String storagePath;
  final String title;
  final String artist;

  const SongScreen({
    super.key,
    required this.storagePath,
    required this.title,
    required this.artist,
  });

  @override
  State<SongScreen> createState() => _SongScreenState();
}

class _SongScreenState extends State<SongScreen> {
  bool isPlaying = false;
  bool _canPop = false; // initially prevent pop
  int? _pendingNavIndex;
  late final DatabaseReference ref;
  late final StreamSubscription<DatabaseEvent> subscription;

  @override
  void initState() {
    super.initState();

    ref = FirebaseDatabase.instance.ref("esp32API/playCommand");

    // Listen for live status updates
    subscription = ref.onValue.listen((event) {
      final data = event.snapshot.value as Map<dynamic, dynamic>?;
      if (data != null && mounted) {
        setState(() {
          isPlaying = (data["status"] ?? "stopped") == "playing";
        });
      }
    });
  }

  @override
  void dispose() {
    subscription.cancel();
    super.dispose();
  }

  Future<void> sendPlaybackCommand(bool play) async {
    final snapshot = await ref.get();
    int count = 0;

    if (snapshot.exists) {
      final data = snapshot.value as Map<dynamic, dynamic>;
      count = (data["commandsCounter"] ?? 0) as int;
    }

    await ref.set({
      "commandsCounter": count + 1,
      "fileToPlay": widget.storagePath,
      "playMode": 0,
      "status": play ? "playing" : "stopped",
    });
  }

  Future<void> _showExitDialog() async {
    final shouldLeave = await showDialog<bool>(
      context: context,
      barrierDismissible: false,
      builder: (context) => AlertDialog(
        title: const Text("Stop playing?"),
        content: const Text("Do you want to stop the song before leaving?"),
        backgroundColor: const Color.fromARGB(255, 23, 23, 23),
        contentTextStyle: const TextStyle(color: Colors.white),
        titleTextStyle: TextStyle(
          color: Colors.white,
          fontSize: 25,
          fontWeight: FontWeight.bold,
        ),
        actions: [
          TextButton(
            style: TextButton.styleFrom(foregroundColor: Colors.white),
            onPressed: () => Navigator.pop(context, false),
            child: const Text("Stay"),
          ),
          TextButton(
            style: TextButton.styleFrom(foregroundColor: Colors.white),
            onPressed: () async {
              await sendPlaybackCommand(false); // stop song
              Navigator.pop(context, true); // close dialog
            },
            child: const Text("Stop & Leave"),
          ),
        ],
      ),
    );

    if (shouldLeave == true) {
      // User chose Stop & Leave
      if (_pendingNavIndex != null) {
        _navigateToTab(_pendingNavIndex!);
        return;
      }

      // Default pop if not coming from bottom nav
      setState(() => _canPop = true);
      Navigator.of(context).pop();
    }
  }

  /// When bottom nav is tapped
  Future<void> _handleNavLeave(int index) async {
    if (isPlaying) {
      _pendingNavIndex = index;
      await _showExitDialog();
    } else {
      _navigateToTab(index);
    }
  }

  void _navigateToTab(int index) {
    if (!mounted) return;

    if (index == 0) {
      Navigator.pushReplacementNamed(context, "/home");
    } else if (index == 1) {
      Navigator.pushReplacementNamed(context, "/search");
    } else if (index == 2) {
      Navigator.pushReplacementNamed(context, "/upload");
    }
  }

  @override
  Widget build(BuildContext context) {
    return PopScope(
      canPop: _canPop,
      onPopInvoked: (didPop) {
        if (didPop) return;

        if (isPlaying) {
          _showExitDialog();
        } else {
          setState(() => _canPop = true);
          Navigator.pop(context);
        }
      },
      child: Scaffold(
        backgroundColor: const Color(0xFF1E1E1E),

        // header with back button
        appBar: PreferredSize(
          preferredSize: const Size.fromHeight(60),
          child: MyHeader(
            title: "Song",
            isBackButton: true,
            onBack: () async {
              if (isPlaying) {
                await _showExitDialog();
              } else {
                Navigator.pop(context);
              }
            },
          ),
        ),

        body: SingleChildScrollView(
          padding: const EdgeInsets.all(16),
          child: Column(
            children: [
              Container(
                width: double.infinity,
                height: MediaQuery.of(context).size.height * 0.45,
                decoration: BoxDecoration(
                  color: const Color(0xFFFFD54F),
                  borderRadius: BorderRadius.circular(6),
                ),
                child: const Icon(
                  Icons.music_note,
                  size: 128,
                  color: Colors.black,
                ),
              ),

              const SizedBox(height: 40),

              Text(
                widget.title,
                style: const TextStyle(
                  color: Colors.white,
                  fontSize: 24,
                  fontWeight: FontWeight.bold,
                ),
              ),
              const SizedBox(height: 8),
              Text(
                widget.artist,
                style: const TextStyle(color: Colors.white70, fontSize: 18),
              ),
              const SizedBox(height: 60),

              // Play / Stop button
              MyButton(
                title: isPlaying ? "Stop Song" : "Learn Song",
                color: isPlaying ? Colors.redAccent : Colors.blueAccent,
                onPressed: () {
                  sendPlaybackCommand(!isPlaying);
                },
              ),
            ],
          ),
        ),

        bottomNavigationBar: MyBottomNavigationBar(
          currentIndex: 1,
          onTap: _handleNavLeave,
        ),
      ),
    );
  }
}
