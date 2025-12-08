import 'package:flutter/material.dart';
import 'package:flutter_app/widgets/header/my_header.dart';
import 'package:flutter_app/widgets/my_button.dart';
import '../widgets/footer/bottom_navigation_bar.dart';
import 'package:firebase_database/firebase_database.dart';
import 'dart:async';

// Import the default previous screen (change this to your actual SearchScreen import)
import 'search_screen.dart';

class SongScreen extends StatefulWidget {
  final String storagePath;
  final String title;
  final String artist;
  final Widget? previousScreen; // optional previous screen

  const SongScreen({
    super.key,
    required this.storagePath,
    required this.title,
    required this.artist,
    this.previousScreen, // default will be SearchScreen
  });

  @override
  _SongScreenState createState() => _SongScreenState();
}

class _SongScreenState extends State<SongScreen> {
  bool isPlaying = false; // track playback status

  late final DatabaseReference ref;
  late final StreamSubscription<DatabaseEvent> subscription;

  @override
  void initState() {
    super.initState();

    ref = FirebaseDatabase.instance.ref("esp32API/playCommand");

    // Listen for changes in Realtime Database
    subscription = ref.onValue.listen((DatabaseEvent event) {
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
    subscription.cancel(); // stop listening when widget is disposed
    super.dispose();
  }

  Future<void> sendPlaybackCommandRTDB({required bool play}) async {
    final snapshot = await ref.get();
    int currentCount = 0;

    if (snapshot.exists) {
      final data = snapshot.value as Map<dynamic, dynamic>;
      currentCount = (data["commandsCounter"] ?? 0) as int;
    }

    await ref.set({
      "commandsCounter": currentCount + 1,
      "fileToPlay": widget.storagePath,
      "playMode": 0,
      "status": play ? "playing" : "stopped",
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF1E1E1E),
      body: SingleChildScrollView(
        child: Padding(
          padding: const EdgeInsets.all(16.0),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.center,
            children: [
              // Top back button + header
              MyHeader(title: 'Song', isBackButton: true),

              const SizedBox(height: 24),
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
                style: const TextStyle(
                  color: Colors.white70,
                  fontSize: 18,
                  fontWeight: FontWeight.normal,
                ),
              ),
              const SizedBox(height: 60),
              ElevatedButton(
                onPressed: () async {
                  try {
                    await sendPlaybackCommandRTDB(play: !isPlaying);
                    ScaffoldMessenger.of(context).showSnackBar(
                      SnackBar(
                        content: Text(
                          isPlaying ? "Playback stopped!" : "Playback started!",
                        ),
                      ),
                    );
                  } catch (e) {
                    ScaffoldMessenger.of(
                      context,
                    ).showSnackBar(SnackBar(content: Text("Error: $e")));
                  }
                },
                style: ElevatedButton.styleFrom(
                  backgroundColor: isPlaying
                      ? Colors.redAccent
                      : Colors.blueAccent,
                  padding: const EdgeInsets.symmetric(
                    horizontal: 40,
                    vertical: 14,
                  ),
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(4),
                  ),
                ),
                child: Text(
                  isPlaying ? "Stop" : "Play",
                  style: const TextStyle(color: Colors.white, fontSize: 19),
                ),
              ),
            ],
          ),
        ),
      ),
      bottomNavigationBar: const MyBottomNavigationBar(currentIndex: 0),
    );
  }
}
